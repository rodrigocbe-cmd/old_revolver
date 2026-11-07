// Traducao (docs/TRADUCAO.md, FUNCOES.md #108): textos por chave em text/<idioma>.txt, sem alterar os .strtbl do jogo.
// Formato do arquivo: UTF-8, uma linha "chave<TAB>texto", # comenta, escapes \n, \t, \\ e \uXXXX; texto vazio mantem o
// original. tools/text_extract.py gera a base em ingles. Por enquanto ha ingles (o texto do jogo) e portugues.
// Idioma: opcao Language do PC Settings; em Auto, o do sistema, com ingles se nao houver arquivo. RDR_LANG=en|pt-br fixa.
//
// FUN_0017F398(chave) e a unica entrada para FUN_0017BF38 e devolve o objeto de texto, com a cadeia UTF-16 em +8, alocada
// por FUN_00219E70 e liberada no destrutor FUN_0017B980. A troca aponta +8 para um bloco nosso, e o destrutor recebe o
// ponteiro original de volta.
//
// Estes ganchos nao chamam funcoes do jogo depois de comecar, nem fazem nada depois da funcao original: o escalonador pode
// interromper o codigo gerado, e a funcao "volta" antes de terminar (FUNCOES.md #96). Por isso a busca e refeita aqui, so
// lendo a memoria, antes de seguir para o original. A primeira versao chamava malloc e free e corrompia a thread.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
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
    std::unordered_map<std::string, uint32_t> g_block;      // chave -> traducao na memoria do jogo
    std::unordered_map<uint32_t, uint32_t> g_swapped;       // objeto -> ponteiro original
    bool g_blocksReady = false;
    std::atomic<bool> g_translate{false};
    bool g_hasFixedLanguage = false; // RDR_LANG
    PS2Runtime::RecompiledFunction g_origTextLookup = nullptr;
    PS2Runtime::RecompiledFunction g_origTextDestroy = nullptr;

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

    uint32_t read32(const uint8_t *rdram, uint32_t address)
    {
        uint32_t value = 0u;
        std::memcpy(&value, rdram + (address & kRamMask), 4);
        return value;
    }

    void write32(uint8_t *rdram, uint32_t address, uint32_t value)
    {
        std::memcpy(rdram + (address & kRamMask), &value, 4);
    }

    // FUN_0017C1C8
    uint32_t keyHash(const char *key)
    {
        uint32_t h = 0u;
        for (; *key; ++key)
        {
            h = (h << 4) + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(*key)));
            const uint32_t top = h & 0xF0000000u;
            if (top != 0u)
                h ^= (top >> 24) ^ top;
        }
        return h;
    }

    // FUN_0017F398 + FUN_0017C690, so lendo: tabelas na mesma ordem, primeira que tiver a chave
    uint32_t findTextObject(const uint8_t *rdram, uint32_t hash)
    {
        for (const uint32_t slot : {0x004F786Cu, 0x004F7870u, 0x004F7868u, 0x004F7864u})
        {
            const uint32_t table = read32(rdram, slot);
            const uint32_t map = table != 0u ? read32(rdram, table) : 0u;
            const uint32_t size = map != 0u ? read32(rdram, map) : 0u;
            if (size == 0u)
                continue;
            for (uint32_t node = read32(rdram, read32(rdram, map + 4u) + (hash % size) * 4u); node != 0u; node = read32(rdram, node + 8u))
                if (read32(rdram, node) == hash)
                    return read32(rdram, node + 4u);
        }
        return 0u;
    }

    // Uma vez: copia as traducoes para o heap do runtime, que o jogo nao libera
    void buildBlocks(uint8_t *rdram, PS2Runtime *runtime)
    {
        g_blocksReady = true;
        size_t used = 0u, skipped = 0u;
        for (const auto &[key, text] : g_text)
        {
            const uint32_t bytes = static_cast<uint32_t>((text.size() + 1u) * 2u);
            const uint32_t block = runtime->guestMalloc(bytes, 4u);
            if (block == 0u)
            {
                ++skipped;
                continue;
            }
            std::memcpy(rdram + (block & kRamMask), text.c_str(), bytes);
            g_block[key] = block;
            used += bytes;
        }
        std::fprintf(stderr, "[rdr] texto: %zu bytes na memoria do jogo, %zu sem espaco\n", used, skipped);
    }

    void textLookup(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_blocksReady)
            buildBlocks(rdram, runtime);
        const uint32_t keyAddress = GPR_U32(ctx, 4);
        if (keyAddress != 0u)
        {
            const char *key = reinterpret_cast<const char *>(rdram + (keyAddress & kRamMask));
            const auto block = g_block.find(key);
            const uint32_t object = block != g_block.end() ? findTextObject(rdram, keyHash(key)) : 0u;
            if (object != 0u)
            {
                const auto swapped = g_swapped.find(object);
                if (g_translate.load(std::memory_order_relaxed))
                {
                    if (swapped == g_swapped.end())
                    {
                        g_swapped[object] = read32(rdram, object + 8u);
                        write32(rdram, object + 8u, block->second);
                    }
                }
                else if (swapped != g_swapped.end())
                {
                    write32(rdram, object + 8u, swapped->second);
                    g_swapped.erase(swapped);
                }
            }
        }
        g_origTextLookup(rdram, ctx, runtime);
    }

    // FUN_0017B980: o jogo libera o que alocou, nunca o nosso bloco
    void textDestroy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t object = GPR_U32(ctx, 4);
        if (const auto swapped = g_swapped.find(object); swapped != g_swapped.end())
        {
            write32(rdram, object + 8u, swapped->second);
            g_swapped.erase(swapped);
        }
        g_origTextDestroy(rdram, ctx, runtime);
    }
}

bool rdrSystemLanguageIsPortuguese(); // rdr_win32.cpp

// Ha traducao instalada? Sem ela o menu so oferece ingles.
bool rdrTextHasPortuguese()
{
    return !findLanguageFile("pt-br").empty();
}

// 0 automatico, 1 ingles, 2 portugues. Vale nas proximas buscas: telas ja abertas mudam ao serem reabertas.
void rdrTextSetLanguage(int choice)
{
    if (!g_hasFixedLanguage)
        g_translate.store(choice == 2 || (choice == 0 && rdrSystemLanguageIsPortuguese()), std::memory_order_relaxed);
}

void rdrTextInstall(PS2Runtime &runtime)
{
    const fs::path file = findLanguageFile("pt-br");
    if (file.empty())
        return;
    if (const char *env = std::getenv("RDR_LANG"); env && env[0])
    {
        g_hasFixedLanguage = true;
        g_translate.store(std::strcmp(env, "pt-br") == 0, std::memory_order_relaxed);
    }
    loadLanguage(file);
    std::fprintf(stderr, "[rdr] texto: %zu traducoes em %s\n", g_text.size(), file.string().c_str());
    if (g_text.empty())
        return;
    g_origTextLookup = runtime.lookupFunction(0x0017F398u);
    runtime.replaceFunction(0x0017F398u, textLookup);
    g_origTextDestroy = runtime.lookupFunction(0x0017B980u);
    runtime.replaceFunction(0x0017B980u, textDestroy);
}
