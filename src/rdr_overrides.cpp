// Game overrides do Red Dead Revolver (SLUS-20500 v1.03).
// Aplicados pelo ps2xRuntime logo apos o loadELF, so para o ELF descomprimido
// (game/SLUS_205.00.unpacked.elf copiado como game/disc/SLUS_205.00, CRC32 EF570C27).
// Todo item aqui deve estar registrado em docs/FUNCOES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_mpeg_options.h"
#include "runtime/gs/gs_draw_stats.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <system_error>
#include <utility>
#include <vector>

bool rdrDrawQuitDialog(const float game[4], int selected, float items[2][4]); // rdr_quit_dialog.cpp
void rdrControlsLoad();                                                         // rdr_controls.cpp
void rdrKeyIconsInstall(PS2Runtime &runtime);                                  // rdr_keyicons.cpp
void rdrFmvInstall(PS2Runtime &runtime);                                         // rdr_fmv.cpp
void rdrSettingsInstall(PS2Runtime &runtime);                                                      // rdr_settings.cpp
void rdrSettingsSetMainMenu(bool mainMenu);                                     // rdr_settings.cpp
std::filesystem::path rdrDocumentsDirectory();                                 // rdr_win32.cpp

namespace
{
    constexpr uint32_t kRdrUnpackedElfCrc32 = 0xEF570C27u;

    // O jogo le o disco por LSN (sceCdRead/sceCdReadChain sem sceCdSearchFile), entao o HLE de CD
    // precisa da imagem do disco; o runtime nao tem opcao de linha de comando para isso.
    // Ordem: variavel RDR_CD_IMAGE; senao o primeiro .iso em <raiz do projeto>/ROM
    // (cdrom0: = game/disc, entao a raiz e dois niveis acima).
    std::filesystem::path findCdImage(const std::filesystem::path &elfDirectory)
    {
        if (const char *env = std::getenv("RDR_CD_IMAGE"); env && env[0] != '\0')
        {
            return std::filesystem::path(env);
        }

        // Versao de distribuicao: disco virtual montado dos arquivos instalados (rdr_install.cpp)
        if (std::error_code vec; std::filesystem::exists(elfDirectory / "disc.vdisc", vec))
        {
            return elfDirectory / "disc.vdisc";
        }

        std::error_code ec;
        const std::filesystem::path romDir = elfDirectory.parent_path().parent_path() / "ROM";
        for (const auto &entry : std::filesystem::directory_iterator(romDir, ec))
        {
            std::string ext = entry.path().extension().string();
            for (char &c : ext)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (entry.is_regular_file(ec) && ext == ".iso")
            {
                return entry.path();
            }
        }
        return {};
    }

    // Experimento (FUNCOES.md #47): RDR_FORCE_VISIBLE=1 faz o teste de frustum por malha
    // (FUN_00153608: VCLIP contra os planos da camera) sempre responder "dentro" (1).
    void forceMeshVisible(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t out = GPR_U32(ctx, 6);
        if (out != 0u)
            WRITE32(out, 0u);
        SET_GPR_U32(ctx, 2, 1u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Avisos de memory card no boot (FUNCOES.md #91): FUN_00101BA8 (boot) mostra popups do Flash popups_01
    // (autosave "Insertion of a memory card (8MB)...", falta de espaco etc.) conforme o estado do memory card e
    // espera "playerResponse" (0x515348) >= 0, lido por FUN_00103428(popup, nome, &valor). So essa funcao le
    // essa variavel; respondendo 0 (primeiro botao, aceitar) na mesma leitura o popup nunca e desenhado e o save
    // segue direto, como se gravasse na memoria do PC. RDR_SHOW_MC_POPUPS=1 mantem o original.
    PS2Runtime::RecompiledFunction g_origPopupGetVar = nullptr;

    void popupAutoAccept(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t name = GPR_U32(ctx, 5), out = GPR_U32(ctx, 6);
        g_origPopupGetVar(rdram, ctx, runtime);
        if (name == 0x00515348u && out != 0u)
        {
            int32_t v = 0;
            std::memcpy(&v, rdram + (out & 0x1FFFFFFu), 4);
            if (v < 0)
            {
                static int s_logged = 0;
                if (s_logged++ < 4)
                    std::fprintf(stderr, "[rdr] aviso de memory card respondido automaticamente\n");
                v = 0;
                std::memcpy(rdram + (out & 0x1FFFFFFu), &v, 4);
            }
        }
    }

    // O aviso de autosave ("Insertion of a memory card (8MB)... MEMORY CARD slot 1-A") e um quadro do Flash de
    // abertura logo01 (FUN_00101BA8, variavel "slotName" = "slot 1-A,"). A variavel "skip" (0x51539D) desse Flash
    // vale 1 quando o save carregou (o Flash pula o aviso) e 0 sem save. Gravando sempre 1, o aviso nao aparece.
    PS2Runtime::RecompiledFunction g_origPopupSetVar = nullptr;

    void popupSetVarNoMcNotice(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (GPR_U32(ctx, 5) == 0x0051539Du && GPR_U32(ctx, 6) == 0u)
        {
            static int s_logged = 0;
            if (s_logged++ < 4)
                std::fprintf(stderr, "[rdr] aviso de autosave da abertura pulado\n");
            SET_GPR_U32(ctx, 6, 1u);
        }
        g_origPopupSetVar(rdram, ctx, runtime);
    }

    // Controle de PS2 dos tutoriais (FUNCOES.md #90): o widget de HUD (vtable 0x503880, construtor FUN_0015AEF0,
    // 17 partes "leftTwoButton".."controller" da tabela 0x4F7318) desenha o controle 3D no slot 8, FUN_0015B6C0.
    // Sem desenho, o resto do tutorial (texto, logica) segue igual. RDR_SHOW_PAD=1 mantem o original.
    void skipTutorialPadDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Diagnostico (FUNCOES.md #47): RDR_TRACE_FRUSTUM=<pular> registra entradas/saida do teste de
    // frustum por malha (esfera, matrizes M1/M2 na scratchpad 0x1E0/0x220, vf12/vf13, clip, resultado).
    PS2Runtime::RecompiledFunction g_origFrustum = nullptr;
    long g_frustumSkip = 0;
    long g_frustumLogs = 0;

    void traceFrustum(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t sphereAddr = GPR_U32(ctx, 4);
        float sphere[4] = {};
        std::memcpy(sphere, rdram + (sphereAddr & 0x1FFFFFFu), sizeof(sphere));
        const uint8_t *spr = runtime->memory().getScratchpad();
        float m[8][4] = {};
        float view[4][4] = {};
        if (spr)
        {
            std::memcpy(m, spr + 0x1E0, sizeof(m));
            std::memcpy(view, spr + 0xA0, sizeof(view));
        }
        g_origFrustum(rdram, ctx, runtime);
        if (!ps2_gs_stats::primTraceActive()) // so durante o rastreio do F9
            return;
        if (g_frustumLogs++ >= 300)
            return;
        float v12[4], v13[4];
        std::memcpy(v12, &ctx->vu0_vf[12], sizeof(v12));
        std::memcpy(v13, &ctx->vu0_vf[13], sizeof(v13));
        std::fprintf(stderr, "[frustum] s=%.3f,%.3f,%.3f,%.3f vf12=%.3f,%.3f,%.3f vf13=%.3f,%.3f,%.3f clip=%03x res=%u M=",
                     sphere[0], sphere[1], sphere[2], sphere[3], v12[0], v12[1], v12[2], v13[0], v13[1], v13[2],
                     ctx->vu0_clip_flags & 0xFFFu, GPR_U32(ctx, 2));
        for (int r = 0; r < 8; ++r)
            std::fprintf(stderr, "%s%.4f,%.4f,%.4f,%.4f", r ? "|" : "", m[r][0], m[r][1], m[r][2], m[r][3]);
        std::fprintf(stderr, " V=");
        for (int r = 0; r < 4; ++r)
            std::fprintf(stderr, "%s%.4f,%.4f,%.4f,%.4f", r ? "|" : "", view[r][0], view[r][1], view[r][2], view[r][3]);
        std::fprintf(stderr, "\n");
    }

    // Diagnostico (FUNCOES.md #48): RDR_TRACE_CALLS=0xADDR[,0xADDR...] registra as primeiras chamadas de
    // cada funcao (ra, a0..a3 na entrada e v0 na saida). Ate kMaxTracedCalls funcoes.
    constexpr int kMaxTracedCalls = 16;
    uint32_t g_tracedAddr[kMaxTracedCalls] = {};
    PS2Runtime::RecompiledFunction g_tracedOrig[kMaxTracedCalls] = {};
    long g_tracedCount[kMaxTracedCalls] = {};

    template <int N>
    void traceCall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const long n = g_tracedCount[N]++;
        const uint32_t ra = GPR_U32(ctx, 31);
        if (n < 20)
            std::fprintf(stderr, "[call] 0x%06x #%ld ra=0x%06x a0=0x%x a1=0x%x a2=0x%x a3=0x%x\n", g_tracedAddr[N], n, ra,
                         GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7));
        else if ((n & (n - 1)) == 0)
            std::fprintf(stderr, "[call] 0x%06x total %ld (ra=0x%06x a0=0x%x)\n", g_tracedAddr[N], n, ra, GPR_U32(ctx, 4));
        g_tracedOrig[N](rdram, ctx, runtime);
        if (n < 20)
            std::fprintf(stderr, "[call] 0x%06x #%ld -> v0=0x%x\n", g_tracedAddr[N], n, GPR_U32(ctx, 2));
    }

    template <int... I>
    constexpr PS2Runtime::RecompiledFunction tracedWrapper(int n, std::integer_sequence<int, I...>)
    {
        constexpr PS2Runtime::RecompiledFunction table[] = {&traceCall<I>...};
        return table[n];
    }

    void installCallTraces(PS2Runtime &runtime, const char *list)
    {
        int slot = 0;
        for (const char *p = list; *p && slot < kMaxTracedCalls;)
        {
            char *end = nullptr;
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(p, &end, 16));
            if (end == p)
                break;
            PS2Runtime::RecompiledFunction orig = runtime.lookupFunction(addr);
            if (orig)
            {
                g_tracedAddr[slot] = addr;
                g_tracedOrig[slot] = orig;
                runtime.replaceFunction(addr, tracedWrapper(slot, std::make_integer_sequence<int, kMaxTracedCalls>{}));
                std::fprintf(stderr, "[rdr] RDR_TRACE_CALLS: 0x%06x\n", addr);
                ++slot;
            }
            p = (*end == ',') ? end + 1 : end;
        }
    }

    // Diagnostico (FUNCOES.md #50): RDR_WATCH_ADDR=0xADDR + RDR_WATCH_FUNCS=0xF1,0xF2,... registra qual
    // das funcoes listadas muda a palavra em ADDR (valor antes/depois da chamada, inclusive filhas).
    // Ate kMaxWatchFuncs funcoes; 30 registros por funcao.
    constexpr int kMaxWatchFuncs = 256;
    uint32_t g_watchAddr = 0;
    uint32_t g_watchFuncAddr[kMaxWatchFuncs] = {};
    PS2Runtime::RecompiledFunction g_watchOrig[kMaxWatchFuncs] = {};
    long g_watchHits[kMaxWatchFuncs] = {};

    template <int N>
    void watchCall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t before = 0, after = 0;
        std::memcpy(&before, rdram + (g_watchAddr & 0x1FFFFFFu), 4);
        const uint32_t a0 = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
        g_watchOrig[N](rdram, ctx, runtime);
        std::memcpy(&after, rdram + (g_watchAddr & 0x1FFFFFFu), 4);
        if (before != after && g_watchHits[N]++ < 30)
        {
            float fb, fa;
            std::memcpy(&fb, &before, 4);
            std::memcpy(&fa, &after, 4);
            std::fprintf(stderr, "[wwatch] 0x%06x ra=0x%06x a0=0x%x %08x(%g) -> %08x(%g)\n", g_watchFuncAddr[N], ra, a0,
                         before, fb, after, fa);
        }
    }

    template <int... I>
    constexpr PS2Runtime::RecompiledFunction watchWrapper(int n, std::integer_sequence<int, I...>)
    {
        constexpr PS2Runtime::RecompiledFunction table[] = {&watchCall<I>...};
        return table[n];
    }

    void installWriteWatch(PS2Runtime &runtime, uint32_t addr, const char *list)
    {
        g_watchAddr = addr;
        int slot = 0;
        for (const char *p = list; *p && slot < kMaxWatchFuncs;)
        {
            char *end = nullptr;
            const uint32_t fn = static_cast<uint32_t>(std::strtoul(p, &end, 16));
            if (end == p)
                break;
            if (PS2Runtime::RecompiledFunction orig = runtime.lookupFunction(fn))
            {
                g_watchFuncAddr[slot] = fn;
                g_watchOrig[slot] = orig;
                runtime.replaceFunction(fn, watchWrapper(slot, std::make_integer_sequence<int, kMaxWatchFuncs>{}));
                ++slot;
            }
            p = (*end == ',') ? end + 1 : end;
        }
        std::fprintf(stderr, "[rdr] RDR_WATCH_ADDR 0x%x: %d funcoes\n", addr, slot);
    }

    // Diagnostico (FUNCOES.md #50): RDR_TRACE_PROBE=<n> registra as n primeiras chamadas da sonda de chao
    // do personagem FUN_002C7610(f12=raio, a0=personagem, a1=saida, a2=posicao nova, a3=posicao antiga)
    // e, uma vez, os ponteiros do mundo de colisao (DAT_004F80AC, +0x84 e os metodos +0xEC/+0xF0/+0xF8).
    PS2Runtime::RecompiledFunction g_origProbe = nullptr;
    long g_probeLimit = 0;
    long g_probeCount = 0;

    void traceProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto rd32 = [rdram](uint32_t a) { uint32_t v = 0; std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4); return v; };
        auto vec = [rdram](uint32_t a, float *v) { std::memcpy(v, rdram + (a & 0x1FFFFFFu), 12); };
        const long n = g_probeCount++;
        if (n == 0)
        {
            const uint32_t world = rd32(0x004F80ACu);
            const uint32_t coll = world ? rd32(world + 0x84u) : 0u;
            const uint32_t vt = coll ? rd32(coll) : 0u;
            std::fprintf(stderr, "[probe] world=0x%x coll=0x%x vtable=0x%x +ec=0x%x +f0=0x%x +f8=0x%x\n", world, coll, vt,
                         vt ? rd32(vt + 0xECu) : 0u, vt ? rd32(vt + 0xF0u) : 0u, vt ? rd32(vt + 0xF8u) : 0u);
        }
        const uint32_t obj = GPR_U32(ctx, 4), out = GPR_U32(ctx, 5), np = GPR_U32(ctx, 6), op = GPR_U32(ctx, 7);
        float radius = ctx->f[12], blend = ctx->f[13], nv[3], ov[3], rv[3];
        vec(np, nv);
        vec(op, ov);
        g_origProbe(rdram, ctx, runtime);
        if (n >= g_probeLimit)
            return;
        vec(out, rv);
        uint8_t ground = 0;
        std::memcpy(&ground, rdram + ((obj + 0x20Cu) & 0x1FFFFFFu), 1);
        float normal[3];
        vec(obj + 0x224u, normal);
        std::fprintf(stderr, "[probe] #%ld obj=0x%x r=%.3f b=%.3f old=%.3f,%.3f,%.3f new=%.3f,%.3f,%.3f -> %.3f,%.3f,%.3f ground=%u n=%.3f,%.3f,%.3f\n",
                     n, obj, radius, blend, ov[0], ov[1], ov[2], nv[0], nv[1], nv[2], rv[0], rv[1], rv[2], ground, normal[0],
                     normal[1], normal[2]);
    }

    // Correcao (FUNCOES.md #51): o passo de tempo do jogo (DAT_004F7DF0, calculado em FUN_0025F960) vem do
    // COP0 Count, que no runtime segue o relogio real. Na fase rodamos a ~7 qps; o dt ficava no teto de
    // 0,1 s (3x o do PS2 a 30 qps) e a sonda de chao nao segurava o personagem, que caia pelo terreno.
    // O proprio motor tem passo maximo em DAT_004F7E00 (o jogo zera no boot = passo livre): fixamos 1/30 s
    // antes de cada calculo. Com emulacao lenta o jogo fica em camera lenta, como num PS2 sobrecarregado
    // com passo fixo. Desligado por padrao; RDR_FIXED_DT=<segundos> (ex.: 0.0333) liga.
    PS2Runtime::RecompiledFunction g_origFrameTime = nullptr;
    float g_fixedDt = 0.0f; // desligado: o teste com 1/30 nao impediu a queda (FUNCOES.md #51)

    void fixedFrameTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::memcpy(rdram + 0x004F7E00u, &g_fixedDt, sizeof(g_fixedDt));
        g_origFrameTime(rdram, ctx, runtime);
    }

    // Parametros de linha de comando do motor (FUNCOES.md #74). main (FUN_00100250) passa argc/argv a
    // FUN_0025A070, que guarda "-nome[=valor]" na tabela de parametros (0x737F90); FUN_00100308 depois
    // consulta nomes como "unlockall" (libera capitulos, trapacas, diario, multi, dificuldades: "lvlaccess"
    // etc.), "noenemies" e "level". O PS2 nao recebe argumentos, entao acrescentamos os de
    // config/params.txt (um por linha, # comenta) e de RDR_PARAMS (separados por espaco).
    std::vector<std::string> g_extraArgs;
    PS2Runtime::RecompiledFunction g_origParseArgs = nullptr;
    PS2Runtime::RecompiledFunction g_guestMalloc = nullptr; // FUN_00219E70(tamanho)

    // Diagnostico: RDR_TRACE_PARAMS=1 registra cada nome consultado na tabela de parametros (1a vez).
    PS2Runtime::RecompiledFunction g_origParamLookup = nullptr; // FUN_0025C0C8(tabela, nome)
    std::vector<std::string> g_paramsSeen;

    void traceParamLookup(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        // 0x25A0F4 (parse de argv) e 0x25A394 (FUN_0025A360) so procuram antes de gravar.
        if (GPR_U32(ctx, 4) == 0x00747F90u && ra != 0x0025A0F4u && ra != 0x0025A394u)
        {
            const char *p = reinterpret_cast<const char *>(rdram + (GPR_U32(ctx, 5) & 0x1FFFFFFu));
            std::string name(p, strnlen(p, 64));
            if (std::find(g_paramsSeen.begin(), g_paramsSeen.end(), name) == g_paramsSeen.end())
            {
                g_paramsSeen.push_back(name);
                g_origParamLookup(rdram, ctx, runtime);
                // Os consultores (FUN_0025A518 etc.) guardam o ra do chamador em 0x8($sp).
                uint32_t caller = 0;
                std::memcpy(&caller, rdram + ((GPR_U32(ctx, 29) + 8u) & 0x1FFFFFFu), 4);
                std::fprintf(stderr, "[param] %s ra=0x%06x chamador=0x%06x -> %s\n", name.c_str(), ra, caller,
                             GPR_U32(ctx, 2) ? "sim" : "nao");
                return;
            }
        }
        g_origParamLookup(rdram, ctx, runtime);
    }

    void loadExtraArgs()
    {
        auto add = [](std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.pop_back();
            size_t b = 0;
            while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
                ++b;
            s.erase(0, b);
            if (s.empty() || s[0] == '#')
                return;
            g_extraArgs.push_back(s[0] == '-' ? s : "-" + s);
        };
        // RDR_PARAMS_FILE=none ignora o arquivo (bissecao do heap overrun no loading, FUNCOES.md #88)
        const char *fileEnv = std::getenv("RDR_PARAMS_FILE");
        const bool useFile = !(fileEnv && std::strcmp(fileEnv, "none") == 0);
        for (const char *base : {"", "../", "../../"})
        {
            if (!useFile)
                break;
            std::ifstream in(std::string(base) + "config/params.txt");
            if (!in)
                continue;
            for (std::string line; std::getline(in, line);)
                add(line);
            break;
        }
        if (const char *env = std::getenv("RDR_PARAMS"))
        {
            std::string all(env), tok;
            for (char c : all + " ")
            {
                if (c == ' ')
                    add(std::exchange(tok, {}));
                else
                    tok += c;
            }
        }
        // O jogo sempre liga em widescreen (pedido do usuario); "nowidescreen" nos parametros desliga.
        const bool noWide = std::find(g_extraArgs.begin(), g_extraArgs.end(), "-nowidescreen") != g_extraArgs.end();
        if (!noWide && std::find(g_extraArgs.begin(), g_extraArgs.end(), "-widescreen") == g_extraArgs.end())
            g_extraArgs.push_back("-widescreen");
    }

    void parseArgsWithExtras(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t argc = GPR_U32(ctx, 4), argv = GPR_U32(ctx, 5);
        const uint32_t total = argc + static_cast<uint32_t>(g_extraArgs.size());
        uint32_t bytes = (total + 1u) * 4u;
        for (const std::string &a : g_extraArgs)
            bytes += static_cast<uint32_t>(a.size()) + 1u;

        const uint32_t ra = GPR_U32(ctx, 31);
        SET_GPR_U32(ctx, 4, bytes);
        g_guestMalloc(rdram, ctx, runtime);
        const uint32_t block = GPR_U32(ctx, 2);
        SET_GPR_U32(ctx, 31, ra);
        if (block == 0u)
        {
            std::fprintf(stderr, "[rdr] parametros: malloc do jogo falhou, ignorando %zu extras\n", g_extraArgs.size());
            SET_GPR_U32(ctx, 4, argc);
            SET_GPR_U32(ctx, 5, argv);
            g_origParseArgs(rdram, ctx, runtime);
            return;
        }

        auto ptr = [rdram](uint32_t a) { return rdram + (a & 0x1FFFFFFu); };
        uint32_t str = block + (total + 1u) * 4u;
        for (uint32_t i = 0; i < argc; ++i)
            std::memcpy(ptr(block + i * 4u), ptr(argv + i * 4u), 4);
        for (size_t i = 0; i < g_extraArgs.size(); ++i)
        {
            const std::string &a = g_extraArgs[i];
            std::memcpy(ptr(str), a.c_str(), a.size() + 1);
            std::memcpy(ptr(block + (argc + static_cast<uint32_t>(i)) * 4u), &str, 4);
            std::fprintf(stderr, "[rdr] parametro: %s\n", a.c_str());
            str += static_cast<uint32_t>(a.size()) + 1u;
        }
        std::memset(ptr(block + total * 4u), 0, 4);

        SET_GPR_U32(ctx, 4, total);
        SET_GPR_U32(ctx, 5, block);
        g_origParseArgs(rdram, ctx, runtime);
    }

    // Pular fases (FUNCOES.md #75): o livro de capitulos (vtable 0x5024C0, update FUN_0011B0F0) navega pela
    // tabela de capitulos DAT_007436B8 (ate 0x28 ponteiros, termina em 0) e pula as entradas com o byte
    // +0x35 == 0 (capitulo ainda nao liberado). O "lvlaccess" do motor nao e mais consultado no build final;
    // com -unlockall ou -lvlaccess marcamos todas as entradas como liberadas antes de cada update do livro.
    PS2Runtime::RecompiledFunction g_origBookUpdate = nullptr;

    void bookUpdateAllChapters(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (std::getenv("RDR_TRACE_BOOK"))
        {
            static bool listed = false;
            static uint32_t lastBits = 0;
            auto rd = [rdram](uint32_t a) { uint32_t v = 0; std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4); return v; };
            const uint32_t self = GPR_U32(ctx, 4), view = rd(self + 0x44Cu), bits = rd(self + 0x304u);
            if (!listed)
            {
                listed = true;
                std::string flags;
                for (uint32_t i = 0; i < 0x28u && rd(0x007436B8u + i * 4u); ++i)
                    flags += rdram[(rd(0x007436B8u + i * 4u) + 0x35u) & 0x1FFFFFFu] ? '1' : '0';
                std::fprintf(stderr, "[book] capitulos +0x35=%s lista=%u\n", flags.c_str(), rd(self + 0x460u) & 0xFFFFu);
            }
            if (bits != lastBits)
                std::fprintf(stderr, "[book] bits=0x%08x view+0xfc=%u pagina=%u base=%u\n", bits, rd(view + 0xFCu),
                             rd(view + 0x100u), rd(self + 0x464u));
            lastBits = bits;
        }
        // Com DAT_0079E21C != 1 (0 = historia; 1 = BHUNTER, desafios) o update ignora a seta para a direita.
        // Forcamos 1 so durante o update do livro e devolvemos o modo original depois; no Begin (+0x304 & 0x1100000)
        // a fase escolhida carrega no modo original. Com 1 a fase virava desafio do Bounty Hunter ("tentar
        // novamente" ao quebrar a regra do desafio, mesmo sem morrer).
        uint32_t origMode = 0;
        std::memcpy(&origMode, rdram + 0x0079E21Cu, 4);
        const uint32_t selectMode = 1u;
        std::memcpy(rdram + 0x0079E21Cu, &selectMode, 4);
        g_origBookUpdate(rdram, ctx, runtime);
        std::memcpy(rdram + 0x0079E21Cu, &origMode, 4);
    }

    // Liberacao invalida (FUNCOES.md #80): o free do heap (FUN_00218DD8(heap, ptr), chamado por free
    // FUN_00219EC0) confere os vizinhos do bloco e, se nao batem, entra num laco infinito proposital. No
    // loading do capitulo 2 (HNG.DAT) isso travou com o heap principal integro, ou seja, ponteiro que nao e
    // inicio de bloco (liberacao dupla ou ponteiro alheio). Conferimos antes; invalido = registra e ignora
    // (vaza o bloco em vez de travar). Os 6 subpools (FUN_00219A10) sao tentados primeiro, como no original.
    PS2Runtime::RecompiledFunction g_origHeapFree = nullptr;
    PS2Runtime::RecompiledFunction g_subpoolFree = nullptr;

    // Rastreio de alocacoes (RDR_HEAP_TRACK=1): blocos vivos por heap e por quem alocou (ra do alocador
    // FUN_00218860 e, se ele veio dos wrappers de malloc 0x219E70..0x219FFF, o ra salvo em 0(sp)). O FATAL
    // "Heap overrun" imprime os maiores consumidores do heap cheio.
    struct LiveAlloc
    {
        uint32_t heap, size;
        uint64_t site;
    };
    std::mutex g_trackMutex;
    std::unordered_map<uint32_t, LiveAlloc> g_live;
    std::unordered_map<uint32_t, std::string> g_freedBy; // ptr -> cadeia do ultimo free (liberacao dupla)
    bool g_heapTrack = false;
    PS2Runtime::RecompiledFunction g_origHeapAlloc = nullptr;

    // Palavras da pilha que parecem endereco de retorno (codigo 0x100000..0x500000): a cadeia de chamadores.
    std::string stackChain(uint8_t *rdram, uint32_t sp)
    {
        std::string chain;
        for (uint32_t off = 0, n = 0; off < 0x400u && n < 12u; off += 4u)
        {
            uint32_t w = 0;
            std::memcpy(&w, rdram + ((sp + off) & 0x1FFFFFFu), 4);
            if (w >= 0x00100000u && w < 0x00500000u && (w & 3u) == 0u)
            {
                char b[16];
                std::snprintf(b, sizeof(b), " 0x%x", w);
                chain += b;
                ++n;
            }
        }
        return chain;
    }

    void trackedHeapAlloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_heapTrack)
        {
            g_origHeapAlloc(rdram, ctx, runtime);
            return;
        }
        const uint32_t heap = GPR_U32(ctx, 4), size = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
        uint32_t ra2 = 0;
        if (ra >= 0x00219E70u && ra < 0x0021A000u)
            std::memcpy(&ra2, rdram + (GPR_U32(ctx, 29) & 0x1FFFFFFu), 4);
        // malloc alinhado FUN_0025C6A0 (frame 0x10, ra em 8) chama FUN_00219F20 (frame 0x10): o chamador real
        // fica em sp+0x18.
        uint32_t ra3 = 0;
        if (ra2 == 0x0025C6C4u)
            std::memcpy(&ra3, rdram + ((GPR_U32(ctx, 29) + 0x18u) & 0x1FFFFFFu), 4), ra2 = ra3;
        if (size >= 0x100000u)
            std::fprintf(stderr, "[rdr] alocacao grande: heap=0x%x %u bytes ra=0x%x via=0x%x%s pilha:%s\n", heap, size, ra, ra2,
                         ra3 ? " (malloc alinhado)" : "", stackChain(rdram, GPR_U32(ctx, 29)).c_str());
        g_origHeapAlloc(rdram, ctx, runtime);
        const uint32_t ptr = GPR_U32(ctx, 2);
        if (ptr == 0u)
            return;
        std::lock_guard<std::mutex> lock(g_trackMutex);
        g_live[ptr] = {heap, size, (static_cast<uint64_t>(ra2) << 32) | ra};
        g_freedBy.erase(ptr);
        uint32_t levelHeap = 0;
        std::memcpy(&levelHeap, rdram + 0x0079DF1Cu, 4);
        static int s_levelLog = 0;
        if (heap == levelHeap && s_levelLog++ < 400)
            std::fprintf(stderr, "[lh] +%u ptr=0x%x ra=0x%x via=0x%x topo=%u\n", size, ptr, ra, ra2,
                         [&] { uint32_t t = 0; std::memcpy(&t, rdram + ((heap + 0xCu) & 0x1FFFFFFu), 4); return t; }());
    }

    void dumpHeapConsumers(uint32_t heap)
    {
        std::lock_guard<std::mutex> lock(g_trackMutex);
        std::map<uint64_t, std::pair<uint64_t, uint32_t>> bySite;
        for (const auto &[ptr, a] : g_live)
            if (a.heap == heap)
                bySite[a.site].first += a.size, ++bySite[a.site].second;
        std::vector<std::pair<uint64_t, std::pair<uint64_t, uint32_t>>> v(bySite.begin(), bySite.end());
        std::sort(v.begin(), v.end(), [](const auto &x, const auto &y) { return x.second.first > y.second.first; });
        for (size_t i = 0; i < v.size() && i < 15u; ++i)
            std::fprintf(stderr, "[rdr]   vivo: ra=0x%x via=0x%x  %llu bytes em %u blocos\n", static_cast<uint32_t>(v[i].first),
                         static_cast<uint32_t>(v[i].first >> 32), static_cast<unsigned long long>(v[i].second.first), v[i].second.second);
    }

    void checkedHeapFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto rd = [rdram](uint32_t a) { uint32_t v = 0; std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4); return v; };
        const uint32_t heap = GPR_U32(ctx, 4), ptr = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
        std::string prevFree;
        if (g_heapTrack && ptr != 0u)
        {
            std::lock_guard<std::mutex> lock(g_trackMutex);
            auto it = g_live.find(ptr);
            uint32_t levelHeap = 0;
            std::memcpy(&levelHeap, rdram + 0x0079DF1Cu, 4);
            static int s_levelFreeLog = 0;
            if (it != g_live.end() && it->second.heap == levelHeap && s_levelFreeLog++ < 400)
                std::fprintf(stderr, "[lh] -%u ptr=0x%x heap=0x%x ra=0x%x\n", it->second.size, ptr, heap, ra);
            prevFree = it == g_live.end() ? (g_freedBy.count(ptr) ? "ja liberado por" + g_freedBy[ptr] : "nunca alocado") : "vivo";
            if (it != g_live.end())
                g_live.erase(it);
            g_freedBy[ptr] = stackChain(rdram, GPR_U32(ctx, 29));
        }
        const bool checks = rdram[(heap + 0x1Cu) & 0x1FFFFFFu] && rdram[(heap + 0x1Du) & 0x1FFFFFFu] &&
                            !rdram[(heap + 0x1Eu) & 0x1FFFFFFu];
        if (ptr == 0u || !checks)
        {
            g_origHeapFree(rdram, ctx, runtime);
            return;
        }
        // O free de subpool FUN_00219A10 devolve o bloco de um subpool vazio (15424 bytes) por aqui (retorno
        // 0x219AEC). Tentar os subpools com esse ponteiro "liberava" um slot do proprio bloco e ele nunca voltava ao
        // heap: ~15 KB perdidos por ciclo da descompressao zlib, "Heap overrun" no loading do hng.
        const bool fromSubpool = ra == 0x00219AECu;
        for (uint32_t pool = 0xE4u; !fromSubpool && pool <= 0x10Cu; pool += 8u)
        {
            SET_GPR_U32(ctx, 4, heap + pool);
            SET_GPR_U32(ctx, 5, ptr);
            SET_GPR_U32(ctx, 6, heap);
            g_subpoolFree(rdram, ctx, runtime);
            if (GPR_U32(ctx, 2) != 0u)
            {
                SET_GPR_U32(ctx, 31, ra);
                return;
            }
        }
        SET_GPR_U32(ctx, 31, ra);
        SET_GPR_U32(ctx, 4, heap);
        SET_GPR_U32(ctx, 5, ptr);

        const uint32_t base = rd(heap + 4u), end = base + rd(heap + 0xCu), blk = ptr - 0x10u;
        bool ok = blk >= base && blk < end && (rd(blk) & 1u) != 0u; // bit 0 = em uso
        if (ok && blk != base)
        {
            const uint32_t prev = rd(blk) & ~7u;
            ok = prev >= base && prev + ((rd(prev + 4u) + 0xFu) & ~0xFu) + 0x10u == blk;
        }
        if (ok)
        {
            const uint32_t next = blk + ((rd(blk + 4u) + 0xFu) & ~0xFu) + 0x10u;
            ok = next == end || (next < end && (rd(next) & ~7u) == blk);
        }
        if (!ok)
        {
            static int s_logged = 0;
            if (s_logged++ < 32)
                std::fprintf(stderr, "[rdr] free invalido ignorado: ptr=0x%x heap=0x%x chamador=0x%x hdr=0x%x,0x%x\n", ptr,
                             heap, rd(GPR_U32(ctx, 29) + 8u), rd(blk), rd(blk + 4u));
            if (g_heapTrack)
                std::fprintf(stderr, "[rdr]   agora:%s\n[rdr]   antes: %s\n", stackChain(rdram, GPR_U32(ctx, 29)).c_str(),
                             prevFree.c_str());
            return;
        }
        g_origHeapFree(rdram, ctx, runtime);
    }

    // Player de FMV (FUNCOES.md #82): FUN_002361B0(nomes[], n, ...) monta "cdrom0:/pss/<nome>.pss" para cada nome.
    // Registra os videos tocados e, com RDR_FMV_REDIRECT=DE=PARA (ex.: rslogo=bridge01), troca o nome; usado por
    // tools/fmv_audio_test.ps1 para testar o som de cada video sem navegar no jogo.
    PS2Runtime::RecompiledFunction g_origFmvPlay = nullptr;

    void fmvPlayWithRedirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t names = GPR_U32(ctx, 4), count = GPR_U32(ctx, 5);
        const char *rule = std::getenv("RDR_FMV_REDIRECT");
        const std::string ruleStr = rule ? rule : "";
        const size_t eq = ruleStr.find('=');
        for (uint32_t i = 0; i < count && i < 16u; ++i)
        {
            uint32_t ptr = 0;
            std::memcpy(&ptr, rdram + ((names + i * 4u) & 0x1FFFFFFu), 4);
            const std::string name(reinterpret_cast<const char *>(rdram + (ptr & 0x1FFFFFFu)));
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (eq != std::string::npos && lower == ruleStr.substr(0, eq))
            {
                const std::string to = ruleStr.substr(eq + 1u);
                const uint32_t guest = runtime->guestMalloc(static_cast<uint32_t>(to.size() + 1u), 16u);
                if (guest)
                {
                    std::memcpy(rdram + (guest & 0x1FFFFFFu), to.c_str(), to.size() + 1u);
                    std::memcpy(rdram + ((names + i * 4u) & 0x1FFFFFFu), &guest, 4);
                    std::fprintf(stderr, "[rdr] fmv %s -> %s\n", name.c_str(), to.c_str());
                    continue;
                }
            }
            std::fprintf(stderr, "[rdr] fmv %s\n", name.c_str());
        }
        g_origFmvPlay(rdram, ctx, runtime);
    }

    // Carga de fase (FUNCOES.md #83): FUN_00228120(reg) le o nome em reg+4 (reg 0 = fase atual em 0x79DEA0) e abre
    // LVLS_SNG/<NOME>.DAT. Registra cada fase e, com RDR_LEVEL_REDIRECT=DE=PARA (ex.: sher70=bridge), troca o nome
    // antes da carga; usado por tools/level_audio_test.ps1 (Novo jogo -> fase escolhida no lugar do tutorial).
    PS2Runtime::RecompiledFunction g_origLevelLoad = nullptr;

    void levelLoadWithRedirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t rec = GPR_U32(ctx, 4) ? GPR_U32(ctx, 4) : 0x0079DEA0u;
        char *name = reinterpret_cast<char *>(rdram + ((rec + 4u) & 0x1FFFFFFu));
        std::string lower(name, strnlen(name, 16));
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        const char *rule = std::getenv("RDR_LEVEL_REDIRECT");
        const std::string ruleStr = rule ? rule : "";
        const size_t eq = ruleStr.find('=');
        if (eq != std::string::npos && lower == ruleStr.substr(0, eq) && ruleStr.size() - eq - 1u <= 8u)
        {
            const std::string to = ruleStr.substr(eq + 1u);
            std::memcpy(name, to.c_str(), to.size() + 1u);
            std::fprintf(stderr, "[rdr] fase %s -> %s\n", lower.c_str(), to.c_str());
        }
        else
        {
            std::fprintf(stderr, "[rdr] fase %s\n", lower.c_str());
        }
        g_origLevelLoad(rdram, ctx, runtime);
    }

    // Logo da tela de titulo (FUNCOES.md #97): no pacote logo01.pck (SWAT.DAT) o sprite do logo (caractere 0x32,
    // profundidade 29) esta em y = -144 px no palco de 640x480, acima da tela, e nenhum script o move. A posicao nova
    // vem da imagem de referencia do jogo original; RDR_TITLE_LOGO_Y=<px> troca o valor. A busca na RAM so roda no boot:
    // para logo depois de achar (ou depois de 10 minutos).
    // O Flash copia a matriz para o objeto vivo como (x, y) em float, entao corrige tambem essas copias.
    constexpr float kLogoX = 320.6f, kLogoOldY = -144.0f;
    constexpr uint32_t kLogoScanEnd = 0x01F00000u; // antes da pilha (la ha copias temporarias)
    bool g_logoDone = false;

    void titleLogoPump(uint8_t *rdram)
    {
        if (g_logoDone)
            return;
        static const auto s_start = std::chrono::steady_clock::now();
        static int s_framesLeft = -1;
        if (std::chrono::steady_clock::now() - s_start > std::chrono::minutes(10) || s_framesLeft == 0)
        {
            g_logoDone = true;
            return;
        }
        if (s_framesLeft > 0)
            --s_framesLeft;
        const char *env = std::getenv("RDR_TITLE_LOGO_Y");
        const float y = env ? static_cast<float>(std::atof(env)) : 256.0f;
        uint32_t oldBits = 0u, xBits = 0u;
        std::memcpy(&oldBits, &kLogoOldY, 4);
        std::memcpy(&xBits, &kLogoX, 4);
        uint32_t *ram = reinterpret_cast<uint32_t *>(rdram);
        int patched = 0;
        for (size_t i = 0u; i + 1u < kLogoScanEnd / 4u; ++i)
        {
            if (ram[i] == xBits && ram[i + 1u] == oldBits)
            {
                std::memcpy(&ram[i + 1u], &y, 4);
                ++patched;
            }
        }
        if (patched > 0)
        {
            if (s_framesLeft < 0)
                s_framesLeft = 300; // objetos do mesmo sprite criados logo depois
            std::fprintf(stderr, "[rdr] logo do titulo: %d copias, y = %.0f\n", patched, y);
        }
    }

    // Travamento 0x510000 (docs/ESTADO.md): o ra de FUN_00236568 (sp+0x98 do quadro de 0xB0) aparece trocado.
    // Com RDR_GUARD_236568=1 o slot fica vigiado enquanto a funcao esta viva; o runtime registra quem o mudou.
    PS2Runtime::RecompiledFunction g_origFrameUpdate = nullptr;

    void frameUpdateGuarded(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t slot = GPR_U32(ctx, 29) - 0xB0u + 0x98u, ra = GPR_U32(ctx, 31);
        // sp de entrada diferente do anterior (o laco principal chama sempre do mesmo nivel de pilha)
        {
            static uint32_t s_lastSp = 0;
            static int s_spLogs = 0;
            const uint32_t sp = GPR_U32(ctx, 29);
            if (sp != s_lastSp && s_spLogs++ < 40)
            {
                int thread = 0, inv = 0;
                ps2DiagCurrentGuest(thread, inv);
                std::fprintf(stderr, "[guard] FUN_00236568 entrou com sp=0x%x (antes 0x%x) ra=0x%x thread=%d invocacoes=%d\n",
                             sp, s_lastSp, GPR_U32(ctx, 31), thread, inv);
            }
            s_lastSp = sp;
        }
        // Reentrada com a guarda armada: outra execucao do laco principal enquanto a anterior esta suspensa
        if (const uint32_t armed = ps2StackGuardAddr(); armed != 0u)
        {
            static int s_logged = 0;
            if (s_logged++ < 20)
            {
                int thread = 0, inv = 0;
                ps2DiagCurrentGuest(thread, inv);
                std::fprintf(stderr, "[guard] reentrada em FUN_00236568: sp=0x%x ra=0x%x thread=%d invocacoes=%d (guarda ativa em 0x%x)\n",
                             GPR_U32(ctx, 29), ra, thread, inv, armed);
            }
        }
        std::memcpy(rdram + (slot & 0x1FFFFFCu), &ra, 4); // o prologo grava o mesmo valor
        ps2SetSpExpectation(0x00236568u, 0x00236B58u, GPR_U32(ctx, 29) - 0xB0u);
        ps2SetStackGuard(slot, ra);
        g_origFrameUpdate(rdram, ctx, runtime);
        ps2ClearStackGuard();
    }

    // Erro fatal do motor (FUNCOES.md #85): FUN_0019A898(fmt, ...) formata "FATAL: %s" num printf que nao chega
    // ao log e entra num laco infinito (a tela de loading fica parada). Registra a mensagem antes de chamar o original.
    PS2Runtime::RecompiledFunction g_origFatal = nullptr;

    void fatalWithLog(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto str = [rdram](uint32_t a) { return std::string(reinterpret_cast<const char *>(rdram + (a & 0x1FFFFFFu)), strnlen(reinterpret_cast<const char *>(rdram + (a & 0x1FFFFFFu)), 200)); };
        const std::string fmt = str(GPR_U32(ctx, 4));
        const int regs[] = {5, 6, 7, 8, 9, 10, 11};
        std::string out;
        int arg = 0;
        for (size_t i = 0; i < fmt.size(); ++i)
        {
            if (fmt[i] != '%' || i + 1 >= fmt.size())
            {
                out += fmt[i];
                continue;
            }
            size_t j = i + 1;
            while (j < fmt.size() && std::strchr("0123456789.-lh", fmt[j]))
                ++j;
            const char conv = j < fmt.size() ? fmt[j] : 's';
            const uint32_t v = arg < 7 ? GPR_U32(ctx, regs[arg]) : 0u;
            ++arg;
            char buf[32];
            if (conv == 's')
                out += str(v);
            else if (conv == 'x' || conv == 'X' || conv == 'p')
                std::snprintf(buf, sizeof(buf), "%x", v), out += buf;
            else if (conv == '%')
                out += '%', --arg;
            else
                std::snprintf(buf, sizeof(buf), "%d", static_cast<int32_t>(v)), out += buf;
            i = j;
        }
        std::fprintf(stderr, "[rdr] FATAL do jogo: %s (chamador=0x%x) sp=0x%x pilha:%s\n", out.c_str(), GPR_U32(ctx, 31),
                     GPR_U32(ctx, 29), stackChain(rdram, GPR_U32(ctx, 29)).c_str());
        // "Heap overrun" vem do alocador FUN_00218860 (heap: +4 base, +8 tamanho, +0xC topo; listas livres em
        // +0x20 + 4*i encadeadas por bloco[5], tamanho em bloco[1]). O heap fica num registrador salvo (s0..s7):
        // imprime os que parecem heap, com o total livre nas listas (fragmentacao x falta de memoria).
        auto rd = [rdram](uint32_t a) { uint32_t v = 0; std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4); return v; };
        for (int r = 16; r <= 23; ++r)
        {
            const uint32_t h = GPR_U32(ctx, r);
            if (h < 0x00100000u || h >= 0x02000000u)
                continue;
            const uint32_t base = rd(h + 4u), size = rd(h + 8u), top = rd(h + 0xCu);
            if (base < 0x00100000u || base >= 0x02000000u || size == 0u || size > 0x02000000u || top > size + 0x100000u)
                continue;
            uint64_t freeBytes = 0, freeBlocks = 0, largest = 0;
            for (uint32_t i = 0; i < 32u; ++i)
                for (uint32_t b = rd(h + 0x20u + 4u * i), guard = 0; b != 0u && guard < 200000u; b = rd(b + 0x14u), ++guard)
                {
                    const uint32_t sz = rd(b + 4u);
                    freeBytes += sz;
                    ++freeBlocks;
                    largest = std::max<uint64_t>(largest, sz);
                }
            std::fprintf(stderr, "[rdr]   heap? $%d=0x%x base=0x%x tamanho=%u topo=%u livre=%llu em %llu blocos (maior %llu)\n", r, h, base,
                         size, top, static_cast<unsigned long long>(freeBytes), static_cast<unsigned long long>(freeBlocks),
                         static_cast<unsigned long long>(largest));
            if (g_heapTrack)
                dumpHeapConsumers(h);
        }
        if (g_heapTrack)
        {
            const uint32_t levelHeap = rd(0x0079DF1Cu);
            std::fprintf(stderr, "[rdr]   heap da fase 0x%x (topo=%u):\n", levelHeap, rd(levelHeap + 0xCu));
            dumpHeapConsumers(levelHeap);
        }
        std::fflush(stderr);
        g_origFatal(rdram, ctx, runtime);
    }

    // Saida pelo botao de voltar no menu principal (FUNCOES.md #86). Os prompts do rodape vem de FRONT.DAT
    // frontendbuttons.swui (tabela ButtonPromptTable): no escritorio (modos book/poster/journal) so havia
    // "X Enter" e "Options" no meio, com as setas nos cantos. FUN_00105290(tela, nome) le o .swui linha a linha e
    // FUN_001054C8(tela, linha) interpreta as tags <...> da linha (varias por linha). As linhas sao trocadas 1:1:
    // dois comentarios viram os elementos novos (3 botoes centralizados) e os grupos/tabela dos modos do
    // escritorio passam a usa-los, com "voltar Quit" como 3o prompt. FUN_00123AE0(tela, modo) troca o modo dos
    // prompts: nos modos do escritorio o runtime abre "Quit game?" ao apertar voltar.
    // Voltar com circulo, triangulo para Options/Remove (FUNCOES.md #101). No jogo original e o contrario, e no gameplay
    // o triangulo recarrega, entao a troca so vale com menu na tela: cada tela que testa o triangulo marca o quadro em
    // que rodou, e o inicio do quadro liga a troca na leitura do controle. RDR_MENU_BUTTONS=original desliga.
    constexpr uint32_t kMenuScreens[] = {0x00116B98u, 0x0011DBB8u, 0x0011F0D8u, 0x001208B0u, 0x00123FF0u, 0x00127218u,
                                         0x001288A0u, 0x00129E10u, 0x0012AD08u, 0x0012B058u, 0x0012C290u, 0x0012CDB0u,
                                         0x0012F0E8u, 0x00130B00u, 0x00136280u, 0x001365D8u, 0x00137D38u};
    constexpr size_t kMenuScreenCount = std::size(kMenuScreens);
    PS2Runtime::RecompiledFunction g_origMenuScreen[kMenuScreenCount] = {};
    uint64_t g_frameCounter = 0u, g_lastMenuFrame = 0u;
    bool g_officeMenu = false, g_swapMenuButtons = false;

    template <size_t I>
    void menuScreenTick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_lastMenuFrame = g_frameCounter;
        static bool s_logged = false;
        if (!s_logged)
        {
            s_logged = true;
            std::fprintf(stderr, "[rdr] tela de menu 0x%x\n", kMenuScreens[I]);
        }
        g_origMenuScreen[I](rdram, ctx, runtime);
    }

    template <size_t... I>
    void installMenuScreens(PS2Runtime &runtime, std::index_sequence<I...>)
    {
        ((g_origMenuScreen[I] = runtime.lookupFunction(kMenuScreens[I]), runtime.replaceFunction(kMenuScreens[I], menuScreenTick<I>)), ...);
    }

    // Os rodapes dos .swui acompanham a troca: BACK e ALT1 (e ALT2, que tambem usa o circulo) trocam de icone
    void swapPromptTokens(std::string &line)
    {
        static const std::string kBack = "BUTTON_BACK", kAlt1 = "BUTTON_ALT1_OPTIONS", kAlt2 = "BUTTON_ALT2_MISC";
        std::string out;
        for (size_t i = 0u; i < line.size();)
        {
            if (line.compare(i, kBack.size(), kBack) == 0)
                out += kAlt1, i += kBack.size();
            else if (line.compare(i, kAlt1.size(), kAlt1) == 0)
                out += kBack, i += kAlt1.size();
            else if (line.compare(i, kAlt2.size(), kAlt2) == 0)
                out += kBack, i += kAlt2.size();
            else
                out += line[i++];
        }
        line = std::move(out);
    }

    PS2Runtime::RecompiledFunction g_origSwuiLoad = nullptr;
    PS2Runtime::RecompiledFunction g_origSwuiLine = nullptr;
    PS2Runtime::RecompiledFunction g_origPromptMode = nullptr;
    bool g_inFrontendButtons = false;

    void swuiLoadTracked(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const char *name = reinterpret_cast<const char *>(rdram + (GPR_U32(ctx, 5) & 0x1FFFFFFu));
        g_inFrontendButtons = std::strncmp(name, "frontendbuttons", 16) == 0;
        g_origSwuiLoad(rdram, ctx, runtime);
    }

    std::string roomPromptLine(const std::string &line)
    {
        // x dos 3 botoes; o texto fica 30 px a direita, como no original. Quit no lado direito (pedido do usuario),
        // mas antes da seta da direita (502..597); a da esquerda vai ate 130.
        // Options 14 px a esquerda: os espacos Enter-Options e Options-Quit ficam iguais (pedido do usuario).
        static const int kX[3] = {150, 266, 410};
        static const char *const kButton[3] = {"BUTTON_CONFIRM", "BUTTON_ALT1_OPTIONS", "BUTTON_BACK"};
        static const char *const kPrompt[3] = {"Enter", "Options", "Quit"};
        const std::string font = "<FONT:berrylium:SCALE:0.750,0.750>";
        auto texts = [&](const char *prefix, int dx, const char *const *values) {
            std::string s = font;
            for (int i = 0; i < 3; ++i)
                s += "<TEXT:" + std::to_string(kX[i] + dx) + ",388,0:NAME:" + prefix + std::to_string(i) + ":" + values[i] + ">";
            return s;
        };
        const std::string members = "BUTTONR0:PROMPTR0:BUTTONR1:PROMPTR1:BUTTONR2:PROMPTR2";
        const std::string old = "BUTTON0:PROMPT0:BUTTON1:PROMPT1:BUTTON2:PROMPT2:BUTTON3:PROMPT3";
        if (line == ";; storymode / poster layout")
            return texts("BUTTONR", 0, kButton);
        if (line == "; book arrow layout")
            return texts("PROMPTR", 30, kPrompt);
        for (const char *layout : {"layout_book", "layout_poster", "layout_journal"})
        {
            const std::string head = std::string("<GROUP:") + layout + ":nodata:10:";
            if (line.rfind(head, 0) == 0 && line.compare(head.size(), old.size(), old) == 0)
                return std::string("<GROUP:") + layout + ":nodata:8:" + members + line.substr(head.size() + old.size());
        }
        for (const char *mode : {"book", "poster", "journal"})
        {
            const std::string head = std::string("<LOOKUPDATA:") + mode + ",";
            if (line.rfind(head, 0) != 0)
                continue;
            // colunas: modo, layout, 4 pares botao/texto, setas; os pares viram 3 (Enter, Options, Quit)
            std::vector<std::string> cols;
            size_t start = head.size() - 1u;
            for (size_t p = start + 1u; p <= line.size(); ++p)
                if (p == line.size() || line[p] == ',' || line[p] == '>')
                {
                    std::string c = line.substr(start + 1u, p - start - 1u);
                    c.erase(0, c.find_first_not_of(" \t"));
                    cols.push_back(c);
                    start = p;
                    if (p < line.size() && line[p] == '>')
                        break;
                }
            if (cols.size() != 11u)
                return {};
            std::string s = head + "\t" + cols[0];
            for (int i = 0; i < 3; ++i)
                s += std::string(",\t") + kButton[i] + ",\t" + kPrompt[i];
            return s + ",\t" + cols[9] + ",\t" + cols[10] + ">";
        }
        return {};
    }

    void swuiLineRewrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        char *buf = reinterpret_cast<char *>(rdram + (GPR_U32(ctx, 5) & 0x1FFFFFFu));
        std::string line(buf, strnlen(buf, 255));
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::string repl = g_inFrontendButtons ? roomPromptLine(line) : std::string();
        const bool replaced = !repl.empty();
        if (!replaced)
            repl = line;
        if (g_swapMenuButtons)
            swapPromptTokens(repl);
        if ((replaced || repl != line) && repl.size() < 255u) // o buffer da linha (FUN_00105290) tem 256 bytes
            std::memcpy(buf, repl.c_str(), repl.size() + 1u);
        if (replaced)
            std::fprintf(stderr, "[rdr] frontendbuttons: %s\n", repl.c_str());
        g_origSwuiLine(rdram, ctx, runtime);
    }

    void promptModeTracked(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const char *p = reinterpret_cast<const char *>(rdram + (GPR_U32(ctx, 5) & 0x1FFFFFFu));
        const std::string mode(p, strnlen(p, 32));
        static std::string s_last;
        if (mode != s_last)
        {
            s_last = mode;
            std::fprintf(stderr, "[rdr] prompts %s\n", mode.c_str());
        }
        ps2SetBackExitEnabled(mode == "book" || mode == "poster" || mode == "journal");
        rdrSettingsSetMainMenu(mode == "book" || mode == "poster" || mode == "journal");
        g_officeMenu = mode == "book" || mode == "poster" || mode == "journal";
        g_origPromptMode(rdram, ctx, runtime);
    }

    void applyRdrOverrides(PS2Runtime &runtime)
    {
        // Padrao ligado; RDR_CHECKED_FREE=0 desliga (FUNCOES.md #80).
        if (!(std::getenv("RDR_CHECKED_FREE") && std::getenv("RDR_CHECKED_FREE")[0] == '0'))
        {
            g_origHeapFree = runtime.lookupFunction(0x00218DD8u);
            g_subpoolFree = runtime.lookupFunction(0x00219A10u);
            runtime.replaceFunction(0x00218DD8u, checkedHeapFree);
        }
        g_heapTrack = std::getenv("RDR_HEAP_TRACK") && std::getenv("RDR_HEAP_TRACK")[0] == '1';
        if (g_heapTrack)
        {
            g_origHeapAlloc = runtime.lookupFunction(0x00218860u);
            runtime.replaceFunction(0x00218860u, trackedHeapAlloc);
        }
        if (!(std::getenv("RDR_SHOW_MC_POPUPS") && std::getenv("RDR_SHOW_MC_POPUPS")[0] == '1'))
        {
            g_origPopupGetVar = runtime.lookupFunction(0x00103428u);
            runtime.replaceFunction(0x00103428u, popupAutoAccept);
            g_origPopupSetVar = runtime.lookupFunction(0x00103478u);
            runtime.replaceFunction(0x00103478u, popupSetVarNoMcNotice);
        }
        if (!(std::getenv("RDR_SHOW_PAD") && std::getenv("RDR_SHOW_PAD")[0] == '1'))
            runtime.replaceFunction(0x0015B6C0u, skipTutorialPadDraw);
        if (const char *g = std::getenv("RDR_GUARD_236568"); g && g[0] == '1')
        {
            g_origFrameUpdate = runtime.lookupFunction(0x00236568u);
            runtime.replaceFunction(0x00236568u, frameUpdateGuarded);
        }
        g_origFatal = runtime.lookupFunction(0x0019A898u);
        runtime.replaceFunction(0x0019A898u, fatalWithLog);
        g_origLevelLoad = runtime.lookupFunction(0x00228120u);
        runtime.replaceFunction(0x00228120u, levelLoadWithRedirect);
        g_origFmvPlay = runtime.lookupFunction(0x002361B0u);
        runtime.replaceFunction(0x002361B0u, fmvPlayWithRedirect);
        rdrFmvInstall(runtime);
        g_origSwuiLoad = runtime.lookupFunction(0x00105290u);
        runtime.replaceFunction(0x00105290u, swuiLoadTracked);
        g_origSwuiLine = runtime.lookupFunction(0x001054C8u);
        runtime.replaceFunction(0x001054C8u, swuiLineRewrite);
        g_origPromptMode = runtime.lookupFunction(0x00123AE0u);
        runtime.replaceFunction(0x00123AE0u, promptModeTracked);
        ps2SetQuitDialogDrawer(rdrDrawQuitDialog);
        rdrControlsLoad();
        rdrKeyIconsInstall(runtime);
        rdrSettingsInstall(runtime);
        const char *menuButtons = std::getenv("RDR_MENU_BUTTONS");
        g_swapMenuButtons = !(menuButtons && std::strcmp(menuButtons, "original") == 0);
        if (g_swapMenuButtons)
            installMenuScreens(runtime, std::make_index_sequence<kMenuScreenCount>());
        loadExtraArgs();
        if (std::getenv("RDR_TRACE_PARAMS"))
        {
            g_origParamLookup = runtime.lookupFunction(0x0025C0C8u);
            runtime.replaceFunction(0x0025C0C8u, traceParamLookup);
        }
        if (!g_extraArgs.empty())
        {
            g_origParseArgs = runtime.lookupFunction(0x0025A070u);
            g_guestMalloc = runtime.lookupFunction(0x00219E70u);
            runtime.replaceFunction(0x0025A070u, parseArgsWithExtras);
        }
        if (std::find(g_extraArgs.begin(), g_extraArgs.end(), "-unlockall") != g_extraArgs.end() ||
            std::find(g_extraArgs.begin(), g_extraArgs.end(), "-lvlaccess") != g_extraArgs.end())
        {
            g_origBookUpdate = runtime.lookupFunction(0x0011B0F0u);
            runtime.replaceFunction(0x0011B0F0u, bookUpdateAllChapters);
            std::fprintf(stderr, "[rdr] livro de capitulos: todos liberados\n");
        }
        if (const char *dt = std::getenv("RDR_FIXED_DT"))
            g_fixedDt = static_cast<float>(std::atof(dt));
        if (g_fixedDt > 0.0f)
        {
            g_origFrameTime = runtime.lookupFunction(0x0025F960u);
            runtime.replaceFunction(0x0025F960u, fixedFrameTime);
            std::fprintf(stderr, "[rdr] passo de tempo maximo %.4f s (DAT_004F7E00)\n", g_fixedDt);
        }
        if (const char *n = std::getenv("RDR_TRACE_PROBE"))
        {
            g_probeLimit = std::atol(n);
            g_origProbe = runtime.lookupFunction(0x002C7610u);
            runtime.replaceFunction(0x002C7610u, traceProbe);
        }
        if (const char *addr = std::getenv("RDR_WATCH_ADDR"))
            if (const char *list = std::getenv("RDR_WATCH_FUNCS"))
                installWriteWatch(runtime, static_cast<uint32_t>(std::strtoul(addr, nullptr, 16)), list);
        if (const char *list = std::getenv("RDR_TRACE_CALLS"))
            installCallTraces(runtime, list);
        if (const char *skip = std::getenv("RDR_TRACE_FRUSTUM"))
        {
            g_frustumSkip = std::atol(skip);
            g_origFrustum = runtime.lookupFunction(0x00153608u);
            runtime.replaceFunction(0x00153608u, traceFrustum);
        }
        if (std::getenv("RDR_FORCE_VISIBLE"))
        {
            runtime.replaceFunction(0x00153608u, forceMeshVisible);
            std::cerr << "[rdr] RDR_FORCE_VISIBLE: teste de frustum 0x153608 sempre visivel" << std::endl;
        }
        (void)runtime;

        // O laço de quadro do player de FMV (FUN_00236058) chama sceMpegGetPicture antes de
        // alimentar o SPU2 (FUN_00470540); com o GetPicture bloqueante do HLE, a thread leitora
        // ficava presa no callback de áudio esperando espaço no anel (deadlock). FUNCOES.md #11.
        ps2_stubs::setMpegGetPictureNonBlocking(true);
        // A thread leitora (FUN_004713f0) ignora o retorno do sceMpegDemuxPss: com backpressure o HLE
        // devolvia 0 consumido e o resto do video era descartado. FUNCOES.md #12.
        ps2_stubs::setMpegDemuxBlockOnBackpressure(true);
        // O jogo le o .PSS com sceCdRead (sem sceCdSt): o fim do video vem do codigo de fim de programa. FUNCOES.md #14.
        ps2_stubs::setMpegProgramEndIsEof(true);
        // Callbacks de stream na thread leitora (FUNCOES.md #84): na fila iam para a thread principal com a trava
        // do anel de audio do player tomada e as duas threads paravam no mesmo semaforo (ATTRACT.PSS / demo mode).
        ps2_stubs::setMpegStreamCallbacksOnCaller(true);
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        const std::filesystem::path image = findCdImage(paths.elfDirectory);
        if (image.empty())
        {
            std::cerr << "[rdr] imagem do disco nao encontrada (defina RDR_CD_IMAGE ou ponha a ISO em ROM/)" << std::endl;
            return;
        }
        paths.cdImage = image;
        // Saves em Documentos\Red Dead Revolver (como o GTA Vice City); RDR_SAVE_DIR troca (testes automaticos).
        if (const char *saveEnv = std::getenv("RDR_SAVE_DIR"); saveEnv && saveEnv[0] != '\0')
            paths.mcRoot = std::filesystem::path(saveEnv);
        else if (const std::filesystem::path docs = rdrDocumentsDirectory(); !docs.empty())
            paths.mcRoot = docs / "Red Dead Revolver";
        std::error_code mcEc;
        std::filesystem::create_directories(paths.mcRoot, mcEc);
        PS2Runtime::setIoPaths(paths);
        std::cerr << "[rdr] saves: " << paths.mcRoot.string() << std::endl;
        std::cerr << "[rdr] CD image: " << PS2Runtime::getIoPaths().cdImage.string() << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Red Dead Revolver (SLUS-20500)", "SLUS_205.00", 0u, kRdrUnpackedElfCrc32, applyRdrOverrides)

void ps2PadSetSwapTriangleCircle(bool swap); // ps2_pad.cpp

// Inicio de quadro (rdr_settings.cpp): menu ativo = alguma tela rodou nos ultimos quadros, ou o escritorio
void rdrMenuButtonsPump()
{
    ++g_frameCounter;
    if (!g_swapMenuButtons)
        return;
    const bool active = g_officeMenu || g_frameCounter - g_lastMenuFrame <= 3u;
    static bool s_active = false;
    if (active != s_active)
    {
        s_active = active;
        static int s_logs = 0;
        if (s_logs++ < 40)
            std::fprintf(stderr, "[rdr] botoes de menu: %d\n", active ? 1 : 0);
    }
    ps2PadSetSwapTriangleCircle(active);
}

// Inicio de quadro (rdr_settings.cpp)
void rdrTitleLogoPump(uint8_t *rdram)
{
    titleLogoPump(rdram);
}
