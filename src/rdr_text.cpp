// Traducao (docs/TRADUCAO.md, FUNCOES.md #108): textos por chave em text/<idioma>.txt, sem alterar os .strtbl do jogo.
// Formato do arquivo: UTF-8, uma linha "chave<TAB>texto", # comenta, escapes \n, \t, \\ e \uXXXX; texto vazio mantem o
// original. tools/text_extract.py gera a base em ingles. RDR_LANG escolhe o idioma (padrao pt-br); sem arquivo, nada muda.
//
// FUN_0017F398(chave) e a unica entrada para FUN_0017BF38 e devolve o objeto de texto, com a cadeia UTF-16 em +8, alocada
// por FUN_00219E70 e liberada por FUN_00219F70 no destrutor FUN_0017B980. A troca e feita no proprio objeto.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;

namespace
{
    constexpr uint32_t kRamMask = 0x1FFFFFFu;

    std::unordered_map<std::string, std::u16string> g_text;
    PS2Runtime::RecompiledFunction g_origTextLookup = nullptr;
    PS2Runtime::RecompiledFunction g_alloc = nullptr;
    PS2Runtime::RecompiledFunction g_free = nullptr;

    void appendUtf16(std::u16string &out, uint32_t cp)
    {
        if (cp >= 0x10000u)
        {
            cp -= 0x10000u;
            out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        }
        else
            out.push_back(static_cast<char16_t>(cp));
    }

    std::u16string decodeLine(const std::string &s)
    {
        std::u16string out;
        for (size_t i = 0; i < s.size();)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '\\' && i + 1 < s.size())
            {
                const char e = s[i + 1];
                if (e == 'u' && i + 5 < s.size())
                {
                    out.push_back(static_cast<char16_t>(std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16)));
                    i += 6;
                    continue;
                }
                out.push_back(e == 'n' ? u'\n' : e == 't' ? u'\t' : static_cast<char16_t>(static_cast<unsigned char>(e)));
                i += 2;
                continue;
            }
            const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            uint32_t cp = extra == 3 ? c & 7u : extra == 2 ? c & 15u : extra == 1 ? c & 31u : c;
            for (int k = 1; k <= extra && i + k < s.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 63u);
            appendUtf16(out, cp);
            i += 1u + extra;
        }
        return out;
    }

    fs::path findLanguageFile(const std::string &lang)
    {
        const fs::path elfDir = PS2Runtime::getIoPaths().elfDirectory;
        std::error_code ec;
        for (const fs::path &dir : {elfDir.parent_path() / "text", elfDir / "text"})
            if (fs::exists(dir / (lang + ".txt"), ec))
                return dir / (lang + ".txt");
        return {};
    }

    void loadLanguage(const fs::path &file)
    {
        std::ifstream in(file, std::ios::binary);
        std::string line;
        bool first = true;
        while (std::getline(in, line))
        {
            if (first && line.rfind("\xEF\xBB\xBF", 0) == 0)
                line.erase(0, 3);
            first = false;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const size_t tab = line.find('\t');
            if (line.empty() || line[0] == '#' || tab == std::string::npos || tab + 1 == line.size())
                continue;
            g_text[line.substr(0, tab)] = decodeLine(line.substr(tab + 1));
        }
    }

    bool sameText(const uint8_t *rdram, uint32_t guest, const std::u16string &text)
    {
        if (guest == 0u)
            return false;
        const size_t bytes = (text.size() + 1u) * 2u;
        if ((guest & kRamMask) + bytes > (kRamMask + 1u))
            return false;
        return std::memcmp(rdram + (guest & kRamMask), text.c_str(), bytes) == 0;
    }

    void textLookup(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t keyAddr = GPR_U32(ctx, 4);
        std::string key;
        if (keyAddr != 0u)
            key = reinterpret_cast<const char *>(rdram + (keyAddr & kRamMask));

        g_origTextLookup(rdram, ctx, runtime);

        const uint32_t object = GPR_U32(ctx, 2);
        const auto it = g_text.find(key);
        if (object == 0u || it == g_text.end())
            return;
        uint32_t current = 0;
        std::memcpy(&current, rdram + ((object + 8u) & kRamMask), 4);
        const std::u16string &text = it->second;
        // A comparacao, e nao um registro do que ja foi trocado, cobre objetos recriados ao trocar de fase
        if (sameText(rdram, current, text))
            return;

        const uint32_t ra = GPR_U32(ctx, 31);
        SET_GPR_U32(ctx, 4, static_cast<uint32_t>((text.size() + 1u) * 2u));
        g_alloc(rdram, ctx, runtime);
        const uint32_t block = GPR_U32(ctx, 2);
        if (block != 0u)
        {
            std::memcpy(rdram + (block & kRamMask), text.c_str(), (text.size() + 1u) * 2u);
            std::memcpy(rdram + ((object + 8u) & kRamMask), &block, 4);
            if (current != 0u)
            {
                SET_GPR_U32(ctx, 4, current);
                SET_GPR_U32(ctx, 31, ra);
                g_free(rdram, ctx, runtime);
            }
        }
        SET_GPR_U32(ctx, 31, ra);
        SET_GPR_U32(ctx, 2, object);
        ctx->pc = ra;
    }
}

void rdrTextInstall(PS2Runtime &runtime)
{
    const char *env = std::getenv("RDR_LANG");
    const std::string lang = env && env[0] ? env : "pt-br";
    const fs::path file = findLanguageFile(lang);
    if (file.empty())
        return;
    loadLanguage(file);
    std::fprintf(stderr, "[rdr] texto: %zu traducoes em %s\n", g_text.size(), file.string().c_str());
    if (g_text.empty())
        return;
    g_alloc = runtime.lookupFunction(0x00219E70u);
    g_free = runtime.lookupFunction(0x00219F70u);
    g_origTextLookup = runtime.lookupFunction(0x0017F398u);
    runtime.replaceFunction(0x0017F398u, textLookup);
}
