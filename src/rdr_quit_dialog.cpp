// Caixa "Quit game?" no estilo do Red Dead Revolver (FUNCOES.md #86): papel dos popups do jogo (popup_paper.tex),
// titulo e opcoes na fonte regulatorcondensed (a dos titulos/opcoes das telas, ex.: mainoptions.swui) e prompts na
// berrylium (com os icones dos botoes). Tudo lido do FRONT.DAT do disco do usuario em tempo de execucao
// (na versao instalada, frontend/FRONT.DAT, achado pelo disc.vdisc).
// Registrado no runtime por applyRdrOverrides (ps2SetQuitDialogDrawer); a entrada continua no runtime.

#include "ps2_runtime.h"
#include "ps2_virtual_disc.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // Pacote "Dave" (tools/dave_list.py, FUN_002CAB88): diretorio em 0x800 (nome, dado, tamanho, comprimido),
    // nomes em 6 bits com prefixo da entrada anterior, dado comprimido = deflate cru.
    struct Dave
    {
        std::vector<uint8_t> data;
        std::vector<std::string> names;

        static uint32_t u32(const std::vector<uint8_t> &d, size_t off)
        {
            uint32_t v = 0;
            std::memcpy(&v, d.data() + off, 4);
            return v;
        }

        bool open(const std::string &path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;
            data.assign(std::istreambuf_iterator<char>(f), {});
            if (data.size() < 0x800 || (std::memcmp(data.data(), "Dave", 4) != 0 && std::memcmp(data.data(), "DAVE", 4) != 0))
                return false;
            static const char kChars[] = "\0 #$()-./?0123456789_abcdefghijklmnopqrstuvwxyz~";
            const uint32_t n = u32(data, 4), dirSize = u32(data, 8);
            const size_t base = 0x800 + dirSize;
            for (uint32_t i = 0; i < n; ++i)
            {
                size_t bit = (base + u32(data, 0x800 + 16 * i)) * 8;
                auto next = [&]() {
                    uint32_t v = 0;
                    for (int b = 0; b < 6; ++b, ++bit)
                        if (bit / 8 < data.size() && (data[bit / 8] >> (bit % 8)) & 1)
                            v |= 1u << b;
                    return v;
                };
                std::string name;
                uint32_t v0 = next();
                if (v0 >= 0x38 && !names.empty())
                {
                    const uint32_t keep = (v0 - 0x38) + (next() & 31) * 8;
                    name = names.back().substr(0, keep);
                    v0 = next();
                }
                for (int k = 0; k < 200 && v0 != 0; ++k, v0 = next())
                    name += v0 < sizeof(kChars) - 1 ? kChars[v0] : '?';
                names.push_back(name);
            }
            return true;
        }

        std::vector<uint8_t> file(const std::string &name) const
        {
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] != name)
                    continue;
                const uint32_t off = u32(data, 0x808 + 16 * i - 4), size = u32(data, 0x808 + 16 * i), csize = u32(data, 0x80C + 16 * i);
                if (static_cast<size_t>(off) + csize > data.size())
                    return {};
                if (csize == size)
                    return {data.begin() + off, data.begin() + off + size};
                int outSize = 0;
                unsigned char *out = DecompressData(data.data() + off, static_cast<int>(csize), &outSize);
                std::vector<uint8_t> result;
                if (out)
                {
                    result.assign(out, out + outSize);
                    MemFree(out);
                }
                return result;
            }
            return {};
        }
    };

    // .tex: u16 largura, u16 altura, 14 bytes de cabecalho, paleta (256 ou 16 cores, ordem B,G,R,A; alfa ate 0x80)
    // e indices de 8 ou 4 bits (nibble baixo primeiro).
    Texture2D loadTex(const std::vector<uint8_t> &t)
    {
        if (t.size() < 14)
            return {};
        const int w = t[0] | (t[1] << 8), h = t[2] | (t[3] << 8);
        const bool eight = t.size() >= 14u + 1024u + static_cast<size_t>(w * h);
        const size_t palSize = eight ? 1024u : 64u;
        if (!eight && t.size() < 14u + 64u + static_cast<size_t>(w * h / 2))
            return {};
        std::vector<uint8_t> rgba(static_cast<size_t>(w * h) * 4u);
        for (int i = 0; i < w * h; ++i)
        {
            const int idx = eight ? t[14 + palSize + i] : (t[14 + palSize + i / 2] >> ((i & 1) * 4)) & 15;
            const uint8_t *p = &t[14 + idx * 4];
            rgba[i * 4 + 0] = p[2];
            rgba[i * 4 + 1] = p[1];
            rgba[i * 4 + 2] = p[0];
            rgba[i * 4 + 3] = static_cast<uint8_t>(std::min(255, p[3] * 2));
        }
        Image img{rgba.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D tex = LoadTextureFromImage(img);
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        return tex;
    }

    // .fonttex: em 0x04 altura da linha, 0x08 espaco entre letras, 0x0C largura do espaco; em 0x2C n glifos de
    // 10 bytes (u16 caractere, u16 ?, u16 posicao y*256+x, u8 largura, u8 altura, i8 deslocamento y da base,
    // u8 ? (nao faz parte do deslocamento: no "a" da berrylium vale 0xFE e o glifo ia para fora da caixa));
    // depois u32 n paginas + o indice final de glifo de cada pagina (<nome>_NN.tex).
    struct Font
    {
        struct Glyph
        {
            uint16_t ch;
            int page, x, y, w, h, yoff;
        };
        std::vector<Glyph> glyphs;
        std::vector<Texture2D> pages;
        int spacing = 1, spaceWidth = 14;

        bool load(const Dave &dave, const std::string &name)
        {
            const std::vector<uint8_t> d = dave.file(name + ".fonttex");
            if (d.size() < 0x30)
                return false;
            spacing = static_cast<int>(Dave::u32(d, 0x08));
            spaceWidth = static_cast<int>(Dave::u32(d, 0x0C));
            const uint32_t n = Dave::u32(d, 0x2C);
            const size_t q = 0x30 + 10u * n;
            if (d.size() < q + 4)
                return false;
            const uint32_t npages = Dave::u32(d, q);
            if (d.size() < q + 4 + 4u * npages)
                return false;
            int page = 0;
            for (uint32_t i = 0; i < n; ++i)
            {
                while (page < static_cast<int>(npages) && i >= Dave::u32(d, q + 4 + 4u * page))
                    ++page;
                const uint8_t *g = &d[0x30 + 10u * i];
                const int pos = g[4] | (g[5] << 8);
                glyphs.push_back({static_cast<uint16_t>(g[0] | (g[1] << 8)), page, pos & 255, pos >> 8, g[6], g[7],
                                  static_cast<int8_t>(g[8])});
            }
            for (uint32_t k = 0; k < npages; ++k)
            {
                char tex[96];
                std::snprintf(tex, sizeof(tex), "%s_%02u.tex", name.c_str(), k);
                pages.push_back(loadTex(dave.file(tex)));
                if (pages.back().id == 0)
                    return false;
            }
            return !glyphs.empty();
        }

        const Glyph *find(uint16_t ch) const
        {
            for (const Glyph &g : glyphs)
                if (g.ch == ch)
                    return &g;
            return nullptr;
        }

        float width(const std::vector<uint16_t> &text, float scale) const
        {
            float w = 0.0f;
            for (uint16_t c : text)
                if (const Glyph *g = find(c))
                    w += (g->w + spacing) * scale;
                else
                    w += spaceWidth * scale;
            return w;
        }

        // Escreve a partir de x na linha de base y; icones (caracteres >= 0x80) sem a cor do texto.
        void draw(const std::vector<uint16_t> &text, float x, float y, float scale, Color color) const
        {
            for (uint16_t c : text)
            {
                const Glyph *g = find(c);
                if (!g)
                {
                    x += spaceWidth * scale;
                    continue;
                }
                const Rectangle src{static_cast<float>(g->x), static_cast<float>(g->y), static_cast<float>(g->w), static_cast<float>(g->h)};
                const Rectangle dst{x, y + g->yoff * scale, g->w * scale, g->h * scale};
                const Color tint = c >= 0x80 ? Color{255, 255, 255, color.a} : color;
                DrawTexturePro(pages[g->page], src, dst, Vector2{0, 0}, 0.0f, tint);
                x += (g->w + spacing) * scale;
            }
        }
    };

    std::vector<uint16_t> chars(const char *s)
    {
        std::vector<uint16_t> v;
        for (; *s; ++s)
            v.push_back(static_cast<uint8_t>(*s));
        return v;
    }

    struct Assets
    {
        bool tried = false, ok = false;
        Texture2D paper{};
        Font title, prompt;
    } g_assets;

    bool loadAssets()
    {
        if (g_assets.tried)
            return g_assets.ok;
        g_assets.tried = true;
        Dave dave;
        // Versao instalada: FRONT.DAT fica em frontend/; o disc.vdisc diz onde
        const std::filesystem::path elfDir = PS2Runtime::getIoPaths().elfDirectory;
        std::filesystem::path front = elfDir / "FRONT.DAT", moved;
        std::error_code ec;
        if (!std::filesystem::exists(front, ec) && ps2_vdisc::findFile(elfDir / "disc.vdisc", "FRONT.DAT", moved))
            front = moved;
        const std::string path = front.string();
        if (!dave.open(path))
        {
            std::fprintf(stderr, "[rdr] caixa de saida: %s nao abriu, usando a caixa simples\n", path.c_str());
            return false;
        }
        g_assets.paper = loadTex(dave.file("popup_paper.tex"));
        g_assets.ok = g_assets.paper.id != 0 && g_assets.title.load(dave, "regulatorcondensed") &&
                      g_assets.prompt.load(dave, "berrylium");
        if (!g_assets.ok)
            std::fprintf(stderr, "[rdr] caixa de saida: recursos do FRONT.DAT incompletos, usando a caixa simples\n");
        return g_assets.ok;
    }
}

// Coordenadas da tela do jogo (640x448) convertidas para a area da imagem na janela.
bool rdrDrawQuitDialog(const float game[4], int selected, float items[2][4])
{
    if (!loadAssets())
        return false;
    const float sx = game[2] / 640.0f, sy = game[3] / 448.0f, s = std::min(sx, sy);
    auto X = [&](float x) { return game[0] + x * sx; };
    auto Y = [&](float y) { return game[1] + y * sy; };

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.5f));
    const Rectangle paper{X(150), Y(134), 340 * sx, 180 * sy};
    DrawTexturePro(g_assets.paper, Rectangle{0, 0, static_cast<float>(g_assets.paper.width), static_cast<float>(g_assets.paper.height)},
                   paper, Vector2{0, 0}, 0.0f, WHITE);

    // Marrom do titulo das telas do jogo (mainoptions.swui: BASECOLOR 0.289,0.041,0)
    const Color ink{74, 10, 0, 255};
    const Color faded{74, 10, 0, 120};
    const std::vector<uint16_t> title = chars("Quit the game?");
    const float ts = 1.35f * s;
    g_assets.title.draw(title, X(320) - g_assets.title.width(title, ts) / 2.0f, Y(192), ts, ink);

    static const char *const kItems[2] = {"Yes", "No"};
    const float is = 1.2f * s;
    const double pulse = 0.75 + 0.25 * std::sin(GetTime() * 5.0);
    for (int i = 0; i < 2; ++i)
    {
        const std::vector<uint16_t> t = chars(kItems[i]);
        const float w = g_assets.title.width(t, is);
        const float cx = X(i == 0 ? 260.0f : 380.0f);
        const bool sel = i == selected;
        Color c = sel ? Color{140, 20, 0, static_cast<uint8_t>(255 * pulse)} : faded;
        g_assets.title.draw(t, cx - w / 2.0f, Y(242), is, c);
        if (sel)
            DrawRectangleRec(Rectangle{cx - w / 2.0f, Y(246), w, std::max(1.0f, 2.0f * sy)}, Color{140, 20, 0, 200});
        items[i][0] = cx - 50 * sx;
        items[i][1] = Y(214);
        items[i][2] = 100 * sx;
        items[i][3] = 40 * sy;
    }

    // Prompts como nas telas do jogo: [X] Select   [triangulo] Back (glifos 0xBA e 0xBC da berrylium)
    const float ps = 0.75f * s;
    std::vector<uint16_t> prompts = {0xBA, ' '};
    for (uint16_t c : chars("Select    "))
        prompts.push_back(c);
    prompts.push_back(0xBC);
    prompts.push_back(' ');
    for (uint16_t c : chars("Back"))
        prompts.push_back(c);
    g_assets.prompt.draw(prompts, X(320) - g_assets.prompt.width(prompts, ps) / 2.0f, Y(286), ps, ink);
    return true;
}
