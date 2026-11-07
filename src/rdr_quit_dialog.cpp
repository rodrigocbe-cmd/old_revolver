// Caixa "Quit game?" no estilo do Red Dead Revolver (FUNCOES.md #86): igual ao popup do jogo ao sair de uma fase
// (cartao preto do silentfilmpopup.tex, texto na berrylium, YES/NO na regulatorcondensed). Tudo lido do FRONT.DAT do disco do usuario em tempo de execucao
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
                if (const Glyph *g = findOrPlain(c))
                    w += (g->w + spacing) * scale;
                else
                    w += spaceWidth * scale;
            return w;
        }

        // Fonte sem acentos (regulatorcondensed): a letra sem o acento
        const Glyph *findOrPlain(uint16_t c) const
        {
            if (const Glyph *g = find(c))
                return g;
            static const char kPlain[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
            return c >= 0xC0u && c <= 0xFFu ? find(static_cast<uint8_t>(kPlain[c - 0xC0u])) : nullptr;
        }

        // Escreve a partir de x na linha de base y; icones (caracteres >= 0x80) sem a cor do texto.
        void draw(const std::vector<uint16_t> &text, float x, float y, float scale, Color color) const
        {
            for (uint16_t c : text)
            {
                const Glyph *g = findOrPlain(c);
                if (!g)
                {
                    x += spaceWidth * scale;
                    continue;
                }
                const Rectangle src{static_cast<float>(g->x), static_cast<float>(g->y), static_cast<float>(g->w), static_cast<float>(g->h)};
                const Rectangle dst{x, y + g->yoff * scale, g->w * scale, g->h * scale};
                const Color tint = c >= 0x80 && c < 0xC0 ? Color{255, 255, 255, color.a} : color; // icones; acentos seguem a cor
                DrawTexturePro(pages[g->page], src, dst, Vector2{0, 0}, 0.0f, tint);
                x += (g->w + spacing) * scale;
            }
        }
    };

    // UTF-8 vira os codigos das fontes do jogo (Latin-1); byte solto (icones 0x80..0xBF) passa como esta
    std::vector<uint16_t> chars(const std::string &s)
    {
        std::vector<uint16_t> v;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if ((c & 0xE0u) == 0xC0u && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0u) == 0x80u)
            {
                v.push_back(static_cast<uint16_t>(((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu)));
                ++i;
            }
            else
                v.push_back(c);
        }
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
        g_assets.paper = loadTex(dave.file("silentfilmpopup.tex"));
        g_assets.ok = g_assets.paper.id != 0 && g_assets.title.load(dave, "regulatorcondensed") &&
                      g_assets.prompt.load(dave, "berrylium");
        if (!g_assets.ok)
            std::fprintf(stderr, "[rdr] caixa de saida: recursos do FRONT.DAT incompletos, usando a caixa simples\n");
        return g_assets.ok;
    }
}

std::string rdrTextTr(const std::string &english); // rdr_text.cpp

// Cartao dos popups do jogo: preto translucido (como o aviso de jogo novo do livro) com o ornamento de cima do
// silentfilmpopup.tex. Pixels da janela.
void rdrUiDrawCard(float x, float y, float w, float h)
{
    if (!loadAssets())
        return;
    DrawRectangleRec(Rectangle{x, y, w, h}, Color{16, 13, 11, 205});
    const float k = w / 512.0f;
    DrawTexturePro(g_assets.paper, Rectangle{0, 0, 512, 48}, Rectangle{x, y + 14 * k, w, 48 * k}, Vector2{0, 0}, 0.0f, WHITE);
}

// Coordenadas da tela do jogo (640x448) convertidas para a area da imagem na janela. Layout do popup do proprio
// jogo ao sair de uma fase, com o texto "Quit the game?": cartao preto com o ornamento do
// silentfilmpopup.tex, texto branco na berrylium e YES/NO na regulatorcondensed (selecionado branco, outro cinza).
bool rdrDrawQuitDialog(const float game[4], int selected, float items[2][4])
{
    if (!loadAssets())
        return false;
    const float sx = game[2] / 640.0f, sy = game[3] / 448.0f, s = std::min(sx, sy);
    auto X = [&](float x) { return game[0] + x * sx; };
    auto Y = [&](float y) { return game[1] + y * sy; };
    // escala para a letra maiuscula ter a altura pedida (em pixels da tela do jogo)
    auto fit = [&](const auto &f, uint16_t ref, float height) {
        const auto *g = f.find(ref);
        return g && g->h > 0 ? height * s / g->h : s;
    };

    rdrUiDrawCard(X(161), Y(113), 320 * sx, 224 * sy);

    const Color white{235, 235, 230, 255}, gray{128, 128, 128, 255};
    const std::vector<uint16_t> title = chars(rdrTextTr("Quit the game?"));
    const float bs = fit(g_assets.prompt, 'Q', 20.0f);
    g_assets.prompt.draw(title, X(320) - g_assets.prompt.width(title, bs) / 2.0f, Y(222), bs, white);

    static const char *const kItems[2] = {"YES", "NO"};
    const float is = fit(g_assets.title, 'E', 38.0f);
    for (int i = 0; i < 2; ++i)
    {
        const std::vector<uint16_t> t = chars(rdrTextTr(kItems[i]));
        const float w = g_assets.title.width(t, is);
        const float cx = X(i == 0 ? 268.0f : 384.0f);
        g_assets.title.draw(t, cx - w / 2.0f, Y(302), is, i == selected ? white : gray);
        items[i][0] = cx - 50 * sx;
        items[i][1] = Y(258);
        items[i][2] = 100 * sx;
        items[i][3] = 50 * sy;
    }
    return true;
}

// Texto com as fontes do jogo para outros menus do port (rdr_settings.cpp). font 0 = regulatorcondensed (titulos e
// opcoes grandes), 1 = berrylium (texto). y = linha de base.
bool rdrUiReady()
{
    return loadAssets();
}

float rdrUiScaleFor(int font, char ref, float pixelHeight)
{
    const auto &f = font == 0 ? g_assets.title : g_assets.prompt;
    const auto *g = f.find(static_cast<uint8_t>(ref));
    return g && g->h > 0 ? pixelHeight / g->h : 1.0f;
}

float rdrUiTextWidth(int font, const std::string &text, float scale)
{
    const auto &f = font == 0 ? g_assets.title : g_assets.prompt;
    return f.width(chars(rdrTextTr(text)), scale);
}

void rdrUiDrawText(int font, const std::string &text, float x, float baseline, float scale, Color color)
{
    const auto &f = font == 0 ? g_assets.title : g_assets.prompt;
    f.draw(chars(rdrTextTr(text)), x, baseline, scale, color);
}
