// Icones de tecla no lugar dos botoes do controle (docs/CONTROLES.md, FUNCOES.md #94). Os icones sao glifos das
// fontes do jogo (0xBA X, 0xBC triangulo, 0xBD quadrado, 0xBE circulo, 0x2020 L1, 0x2021 R1, 0x2022 L2,
// 0x2023 R2), e as fontes vem dos pacotes "Dave" dos .DAT (FRONT, SWAT e um por fase). Todo arquivo de um .DAT e
// lido por FUN_002C9F40(handle, destino, bytes) (entrada do diretorio em handle+4: +4 offset, +8 tamanho,
// +0xC tamanho comprimido; posicao no arquivo em handle+8). O override chama o original e, se a entrada for uma
// pagina de fonte com icones, troca os bytes lidos pelos da pagina com as teclas desenhadas (feita aqui, a
// partir do mesmo .DAT do disco do usuario). Desenho sem GPU: roda na thread do jogo.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

std::string rdrKeyName(int code);       // rdr_controls.cpp
int rdrControlsPrimaryForPadBit(int bit); // rdr_controls.cpp (0 = sem tecla)

namespace
{
    struct IconGlyph
    {
        uint16_t ch;
        int padBit; // bit da mascara do PS2 (Ps2PadKeyMap::buttons)
    };
    const IconGlyph kIconGlyphs[] = {{0xBA, 14}, {0xBC, 12}, {0xBD, 15}, {0xBE, 13},
                                     {0x2020, 10}, {0x2021, 11}, {0x2022, 8}, {0x2023, 9}};

    struct GlyphRect
    {
        int padBit, x, y, w, h;
    };

    // Uma pagina .tex de fonte que tem icones, num .DAT
    struct IconPage
    {
        fs::path dat;
        uint32_t offset = 0, size = 0, csize = 0;
        std::vector<GlyphRect> glyphs;
        std::vector<uint8_t> patched; // gerada no primeiro uso
        bool built = false;
    };

    std::mutex g_mutex;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, IconPage> g_pages; // (offset, tamanho, comprimido)
    PS2Runtime::RecompiledFunction g_origRead = nullptr;

    uint32_t rd32(const uint8_t *p)
    {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }

    struct DaveEntry
    {
        std::string name;
        uint32_t offset, size, csize;
    };

    // Diretorio de um pacote Dave (tools/dave_list.py): entradas de 16 bytes em 0x800 (nome, offset, tamanho,
    // comprimido) e nomes em 6 bits, com prefixo da entrada anterior, depois do diretorio.
    bool readDaveDirectory(const fs::path &dat, std::vector<DaveEntry> &out)
    {
        std::ifstream f(dat, std::ios::binary);
        if (!f)
            return false;
        std::vector<uint8_t> head(0x800);
        if (!f.read(reinterpret_cast<char *>(head.data()), head.size()) ||
            (std::memcmp(head.data(), "Dave", 4) != 0 && std::memcmp(head.data(), "DAVE", 4) != 0))
            return false;
        const uint32_t n = rd32(&head[4]), dirSize = rd32(&head[8]);
        if (n == 0 || n > 200000 || dirSize > 64u * 1024u * 1024u)
            return false;
        std::vector<uint8_t> d(0x800 + dirSize + n * 200u + 64u);
        f.seekg(0);
        f.read(reinterpret_cast<char *>(d.data()), d.size());
        d.resize(static_cast<size_t>(f.gcount()));
        if (d.size() < 0x800u + 16u * n)
            return false;
        static const char kChars[] = "\0 #$()-./?0123456789_abcdefghijklmnopqrstuvwxyz~";
        const size_t base = 0x800 + dirSize;
        std::string prev;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint8_t *e = &d[0x800 + 16 * i];
            size_t bit = (base + rd32(e)) * 8;
            auto next = [&]() {
                uint32_t v = 0;
                for (int b = 0; b < 6; ++b, ++bit)
                    if (bit / 8 < d.size() && (d[bit / 8] >> (bit % 8)) & 1)
                        v |= 1u << b;
                return v;
            };
            std::string name;
            uint32_t v0 = next();
            if (v0 >= 0x38 && !prev.empty())
            {
                const uint32_t keep = (v0 - 0x38) + (next() & 31) * 8;
                name = prev.substr(0, keep);
                v0 = next();
            }
            for (int k = 0; k < 200 && v0 != 0; ++k, v0 = next())
                name += v0 < sizeof(kChars) - 1 ? kChars[v0] : '?';
            out.push_back({name, rd32(e + 4), rd32(e + 8), rd32(e + 12)});
            prev = name;
        }
        return true;
    }

    std::vector<uint8_t> readDaveFile(const fs::path &dat, const DaveEntry &e)
    {
        std::ifstream f(dat, std::ios::binary);
        std::vector<uint8_t> raw(e.csize);
        f.seekg(e.offset);
        if (!f.read(reinterpret_cast<char *>(raw.data()), raw.size()))
            return {};
        if (e.csize == e.size)
            return raw;
        int outSize = 0;
        unsigned char *out = DecompressData(raw.data(), static_cast<int>(raw.size()), &outSize);
        std::vector<uint8_t> result;
        if (out)
        {
            result.assign(out, out + outSize);
            MemFree(out);
        }
        return result;
    }

    // Procura em cada .fonttex os glifos de icone e registra as paginas onde eles estao
    void indexDat(const fs::path &dat)
    {
        std::vector<DaveEntry> entries;
        if (!readDaveDirectory(dat, entries))
            return;
        int pages = 0;
        for (const DaveEntry &font : entries)
        {
            if (font.name.size() < 9 || font.name.compare(font.name.size() - 8, 8, ".fonttex") != 0)
                continue;
            const std::vector<uint8_t> d = readDaveFile(dat, font);
            if (d.size() < 0x30)
                continue;
            const uint32_t n = rd32(&d[0x2C]);
            const size_t q = 0x30 + 10u * n;
            if (d.size() < q + 4)
                continue;
            const uint32_t npages = rd32(&d[q]);
            if (d.size() < q + 4 + 4u * npages)
                continue;
            std::map<int, std::vector<GlyphRect>> byPage;
            int page = 0;
            for (uint32_t i = 0; i < n; ++i)
            {
                while (page < static_cast<int>(npages) && i >= rd32(&d[q + 4 + 4u * page]))
                    ++page;
                const uint8_t *g = &d[0x30 + 10u * i];
                const uint16_t ch = static_cast<uint16_t>(g[0] | (g[1] << 8));
                for (const IconGlyph &ig : kIconGlyphs)
                    if (ig.ch == ch && g[6] > 0 && g[7] > 0)
                        byPage[page].push_back({ig.padBit, g[4], g[5], g[6], g[7]});
            }
            const std::string stem = font.name.substr(0, font.name.size() - 8);
            for (auto &[pg, rects] : byPage)
            {
                char texName[160];
                std::snprintf(texName, sizeof(texName), "%s_%02d.tex", stem.c_str(), pg);
                for (const DaveEntry &t : entries)
                    if (t.name == texName)
                    {
                        IconPage &p = g_pages[{t.offset, t.size, t.csize}];
                        p.dat = dat;
                        p.offset = t.offset, p.size = t.size, p.csize = t.csize;
                        p.glyphs = rects;
                        ++pages;
                    }
            }
        }
        if (pages)
            std::fprintf(stderr, "[rdr] icones de tecla: %d pagina(s) de fonte em %s\n", pages, dat.filename().string().c_str());
    }

    // Fonte bitmap 5x7 (linhas, bit 4 = coluna da esquerda)
    const uint8_t *glyph5x7(char c)
    {
        static const uint8_t kLetters[26][7] = {
            {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
            {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
            {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
            {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
            {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
            {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
            {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
            {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
            {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
            {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
            {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
            {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
            {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
        static const uint8_t kDigits[10][7] = {
            {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
            {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
            {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
            {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
            {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
        static const uint8_t kPlus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
        static const uint8_t kMinus[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        static const uint8_t kUp[7] = {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04};
        static const uint8_t kDown[7] = {0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04};
        static const uint8_t kLeftA[7] = {0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00};
        static const uint8_t kRightA[7] = {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00};
        if (c >= 'A' && c <= 'Z')
            return kLetters[c - 'A'];
        if (c >= '0' && c <= '9')
            return kDigits[c - '0'];
        switch (c)
        {
        case '+': return kPlus;
        case '-': return kMinus;
        case '^': return kUp;
        case 'v': return kDown;
        case '<': return kLeftA;
        case '>': return kRightA;
        default: return nullptr;
        }
    }

    // Rotulo curto da tecla (cabe em 3 letras no icone de 25 px)
    std::string keyLabel(int code)
    {
        const std::string n = rdrKeyName(code);
        static const std::map<std::string, std::string> kShort = {
            {"SPACE", "SPC"}, {"ENTER", "ENT"}, {"ESCAPE", "ESC"}, {"BACKSPACE", "BKS"}, {"TAB", "TAB"},
            {"CAPSLOCK", "CAP"}, {"LSHIFT", "SHF"}, {"RSHIFT", "RSH"}, {"LCTRL", "CTL"}, {"RCTRL", "RCT"},
            {"LALT", "ALT"}, {"RALT", "RAL"}, {"UP", "^"}, {"DOWN", "v"}, {"LEFT", "<"}, {"RIGHT", ">"},
            {"MOUSE1", "LMB"}, {"MOUSE2", "RMB"}, {"MOUSE3", "MMB"}, {"MOUSE4", "M4"}, {"MOUSE5", "M5"},
            {"WHEELUP", "WH+"}, {"WHEELDOWN", "WH-"}, {"INSERT", "INS"}, {"DELETE", "DEL"}, {"HOME", "HOM"},
            {"END", "END"}, {"PAGEUP", "PGU"}, {"PAGEDOWN", "PGD"}};
        if (auto it = kShort.find(n); it != kShort.end())
            return it->second;
        if (n.rfind("NUMPAD", 0) == 0)
            return "N" + n.substr(6);
        return n.substr(0, 3);
    }

    // Indice da paleta mais proximo da cor (a: alfa do PS2, 0..0x80)
    int nearest(const uint8_t *pal, int count, int r, int g, int b, int a)
    {
        int best = 0;
        long bestD = -1;
        for (int i = 0; i < count; ++i)
        {
            const uint8_t *p = pal + 4 * i; // B, G, R, A
            const long da = (static_cast<long>(p[3]) - a) * 4;
            const long d = (p[2] - r) * (p[2] - r) + (p[1] - g) * (p[1] - g) + (p[0] - b) * (p[0] - b) + da * da;
            if (bestD < 0 || d < bestD)
                best = i, bestD = d;
        }
        return best;
    }

    // Desenha uma tecla (fundo escuro, borda clara, rotulo branco) no retangulo do glifo
    void drawKeyCap(std::vector<uint8_t> &tex, const GlyphRect &r, const std::string &label)
    {
        const int w = tex[0] | (tex[1] << 8), h = tex[2] | (tex[3] << 8);
        const bool eight = tex.size() >= 14u + 1024u + static_cast<size_t>(w * h);
        const int palCount = eight ? 256 : 16;
        const size_t pixels = 14u + (eight ? 1024u : 64u);
        if (!eight && tex.size() < pixels + static_cast<size_t>(w * h / 2))
            return;
        const uint8_t *pal = &tex[14];
        const int clear = nearest(pal, palCount, 0, 0, 0, 0);
        const int fill = nearest(pal, palCount, 38, 34, 30, 0x80);
        const int border = nearest(pal, palCount, 205, 200, 190, 0x80);
        const int ink = nearest(pal, palCount, 255, 255, 255, 0x80);
        auto put = [&](int x, int y, int idx) {
            if (x < 0 || y < 0 || x >= w || y >= h)
                return;
            const int i = y * w + x;
            if (eight)
                tex[pixels + i] = static_cast<uint8_t>(idx);
            else
            {
                uint8_t &b = tex[pixels + i / 2];
                b = (i & 1) ? static_cast<uint8_t>((b & 0x0F) | (idx << 4)) : static_cast<uint8_t>((b & 0xF0) | (idx & 15));
            }
        };
        // tecla com cantos arredondados (1 px)
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x)
            {
                const bool corner = (x == 0 || x == r.w - 1) && (y == 0 || y == r.h - 1);
                const bool edge = x == 0 || y == 0 || x == r.w - 1 || y == r.h - 1;
                put(r.x + x, r.y + y, corner ? clear : edge ? border : fill);
            }
        // rotulo: escala 2 se couber, senao 1; centralizado
        int scale = 2;
        auto textW = [&](int s) { return static_cast<int>(label.size()) * 6 * s - s; };
        while (scale > 1 && (textW(scale) > r.w - 4 || 7 * scale > r.h - 4))
            --scale;
        const int tx = r.x + (r.w - textW(scale)) / 2, ty = r.y + (r.h - 7 * scale) / 2;
        for (size_t c = 0; c < label.size(); ++c)
            if (const uint8_t *g = glyph5x7(label[c]))
                for (int y = 0; y < 7; ++y)
                    for (int x = 0; x < 5; ++x)
                        if (g[y] & (0x10 >> x))
                            for (int sy = 0; sy < scale; ++sy)
                                for (int sx = 0; sx < scale; ++sx)
                                    put(tx + (static_cast<int>(c) * 6 + x) * scale + sx, ty + y * scale + sy, ink);
    }

    void buildPage(IconPage &p)
    {
        p.built = true;
        DaveEntry e{"", p.offset, p.size, p.csize};
        p.patched = readDaveFile(p.dat, e);
        if (p.patched.size() != p.size || p.size < 14)
        {
            p.patched.clear();
            return;
        }
        for (const GlyphRect &r : p.glyphs)
            if (const int code = rdrControlsPrimaryForPadBit(r.padBit); code != 0)
                drawKeyCap(p.patched, r, keyLabel(code));
        // RDR_DUMP_ICONS=<pasta>: grava a pagina corrigida em PNG (conferencia sem rodar ate a tela)
        if (const char *dir = std::getenv("RDR_DUMP_ICONS"); dir && dir[0])
        {
            const std::vector<uint8_t> &t = p.patched;
            const int w = t[0] | (t[1] << 8), h = t[2] | (t[3] << 8);
            const bool eight = t.size() >= 14u + 1024u + static_cast<size_t>(w * h);
            const size_t px = 14u + (eight ? 1024u : 64u);
            std::vector<uint8_t> rgba(static_cast<size_t>(w * h) * 4u);
            for (int i = 0; i < w * h; ++i)
            {
                const int idx = eight ? t[px + i] : (t[px + i / 2] >> ((i & 1) * 4)) & 15;
                const uint8_t *c = &t[14 + idx * 4];
                rgba[i * 4] = c[2], rgba[i * 4 + 1] = c[1], rgba[i * 4 + 2] = c[0];
                rgba[i * 4 + 3] = static_cast<uint8_t>(std::min(255, c[3] * 2));
            }
            Image img{rgba.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            char name[64];
            std::snprintf(name, sizeof(name), "/icons_%08x.png", p.offset);
            ExportImage(img, (std::string(dir) + name).c_str());
        }
    }

    // A leitura de uma pagina pode ser interrompida por checkpoints e bloqueios, e a funcao original entao termina pelo
    // escalonador, depois que o override ja voltou. Antes o override desistia nesse caso e a pagina ficava com os botoes
    // do controle (R1 nas dicas de fase). Agora o retorno da leitura passa por daveReadDone, que sempre roda no fim.
    uint32_t g_readDonePc = 0u; // PC livre na tabela de funcoes (a tabela so cobre a faixa do codigo do jogo)
    struct PendingRead
    {
        uint32_t retPc, dst, requested, pos;
        IconPage *page;
    };
    std::vector<PendingRead> g_pendingReads;

    void daveReadDone(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        std::unique_lock<std::mutex> lock(g_mutex);
        if (g_pendingReads.empty())
            return;
        const PendingRead r = g_pendingReads.back();
        g_pendingReads.pop_back();
        ctx->pc = r.retPc;
        SET_GPR_U32(ctx, 31, r.retPc);
        const uint32_t got = std::min(GPR_U32(ctx, 2), r.requested);
        if (got == 0 || got > 0x01000000u)
            return;
        IconPage &p = *r.page;
        if (!p.built)
        {
            buildPage(p);
            std::fprintf(stderr, "[rdr] icones de tecla aplicados (%s, %zu glifo(s))\n", p.dat.filename().string().c_str(), p.glyphs.size());
        }
        if (p.patched.empty() || r.pos >= p.patched.size())
            return;
        const uint32_t n = std::min<uint32_t>(got, static_cast<uint32_t>(p.patched.size()) - r.pos);
        std::memcpy(rdram + (r.dst & 0x1FFFFFFu), p.patched.data() + r.pos, n);
    }

    void daveReadWithIcons(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t handle = GPR_U32(ctx, 4);
        const uint32_t entry = rd32(rdram + ((handle + 4) & 0x1FFFFFFu));
        const uint8_t *e = rdram + (entry & 0x1FFFFFFu);
        const auto key = std::make_tuple(rd32(e + 4), rd32(e + 8), rd32(e + 12));
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_pages.find(key);
            if (it != g_pages.end() && g_readDonePc != 0u)
            {
                g_pendingReads.push_back({GPR_U32(ctx, 31), GPR_U32(ctx, 5), GPR_U32(ctx, 6),
                                          rd32(rdram + ((handle + 8) & 0x1FFFFFFu)), &it->second});
                SET_GPR_U32(ctx, 31, g_readDonePc);
            }
        }
        g_origRead(rdram, ctx, runtime);
    }

    // .DAT do disco: no instalado, os listados no disc.vdisc; no desenvolvimento, os da pasta do ELF
    std::vector<fs::path> findDats(const fs::path &elfDir)
    {
        std::vector<fs::path> dats;
        std::error_code ec;
        auto isDat = [](const fs::path &p) {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return ext == ".DAT";
        };
        const fs::path vdisc = elfDir / "disc.vdisc";
        if (std::ifstream in(vdisc); in)
        {
            std::string line;
            std::getline(in, line);
            while (std::getline(in, line))
            {
                const size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
                if (b == std::string::npos)
                    continue;
                std::string rel = line.substr(b + 1);
                if (!rel.empty() && rel.back() == '\r')
                    rel.pop_back();
                const fs::path p = elfDir / fs::u8path(rel);
                if (isDat(p))
                    dats.push_back(p);
            }
            return dats;
        }
        for (auto it = fs::recursive_directory_iterator(elfDir, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec) && isDat(it->path()))
                dats.push_back(it->path());
        return dats;
    }
}

// Chamado depois de rdrControlsLoad (precisa das teclas). Sem teclas do RDR (RDR_CONTROLS=legacy) nao faz nada.
void rdrKeyIconsInstall(PS2Runtime &runtime)
{
    if (const char *mode = std::getenv("RDR_CONTROLS"); mode && (std::string(mode) == "legacy" || std::string(mode) == "LEGACY"))
        return;
    const fs::path elfDir = PS2Runtime::getIoPaths().elfDirectory;
    for (const fs::path &dat : findDats(elfDir))
        indexDat(dat);
    if (g_pages.empty())
    {
        std::fprintf(stderr, "[rdr] icones de tecla: nenhuma fonte com icones encontrada em %s\n", elfDir.string().c_str());
        return;
    }
    g_origRead = runtime.lookupFunction(0x002C9F40u);
    if (g_origRead)
    {
        runtime.replaceFunction(0x002C9F40u, daveReadWithIcons);
        for (uint32_t pc = 0x00100000u; pc < 0x00600000u && g_readDonePc == 0u; pc += 4u)
            if (!runtime.hasFunction(pc) && runtime.replaceFunction(pc, daveReadDone))
                g_readDonePc = pc;
        std::fprintf(stderr, "[rdr] fim de leitura das fontes em 0x%x\n", g_readDonePc);
    }
}

// Teclas trocadas (tela de ajuste): as paginas sao refeitas na proxima leitura (fontes ja carregadas so mudam
// quando o jogo as carregar de novo, por exemplo na proxima fase)
void rdrKeyIconsInvalidate()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &[k, p] : g_pages)
        p.built = false, p.patched.clear();
}

// Rotulo curto de uma tecla (como nos icones): SPC, BKS, LMB...
std::string rdrKeyShortLabel(int code)
{
    return keyLabel(code);
}
