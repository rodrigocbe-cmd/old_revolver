// Game overrides do Red Dead Revolver (SLUS-20500 v1.03).
// Aplicados pelo ps2xRuntime logo apos o loadELF, so para o ELF descomprimido
// (game/SLUS_205.00.unpacked.elf copiado como game/disc/SLUS_205.00, CRC32 EF570C27).
// Todo item aqui deve estar registrado em docs/FUNCOES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_mpeg_options.h"
#include "runtime/gs/gs_draw_stats.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

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

    void applyRdrOverrides(PS2Runtime &runtime)
    {
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
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        const std::filesystem::path image = findCdImage(paths.elfDirectory);
        if (image.empty())
        {
            std::cerr << "[rdr] imagem do disco nao encontrada (defina RDR_CD_IMAGE ou ponha a ISO em ROM/)" << std::endl;
            return;
        }
        paths.cdImage = image;
        PS2Runtime::setIoPaths(paths);
        std::cerr << "[rdr] CD image: " << PS2Runtime::getIoPaths().cdImage.string() << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Red Dead Revolver (SLUS-20500)", "SLUS_205.00", 0u, kRdrUnpackedElfCrc32, applyRdrOverrides)
