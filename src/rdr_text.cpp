// Traducao (docs/TRADUCAO.md, FUNCOES.md #108, #111): um arquivo por idioma em text/ (PT-BR.txt...), lidos ao iniciar.
// Formato: UTF-8; a primeira linha "@language Nome" da o nome mostrado no menu (o id e o nome do arquivo); depois uma
// linha "chave<TAB>texto" por texto; # comenta; escapes \n, \t, \\ e \uXXXX; texto vazio mantem o original.
// As chaves sao as dos .strtbl do jogo (tools/text_extract.py gera a base) e, para os menus do port (PC Settings, caixa de
// saida), o proprio texto em ingles. Ingles e o texto original e nao precisa de arquivo.
// Idioma: opcao Language do PC Settings, guardada no settings.ini; sem escolha, o do Windows, ou ingles. RDR_LANG=<id> fixa.
// RDR_TEXT_LOG=1 registra cada chave sem traducao uma vez (para montar arquivos novos).
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

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

std::string rdrSystemLocaleName(); // rdr_win32.cpp ("pt-BR")

namespace
{
    constexpr uint32_t kRamMask = 0x1FFFFFFu;

    struct Language
    {
        std::string id, name; // id = nome do arquivo sem .txt; ingles = "EN" sem arquivo
        fs::path file;
    };

    std::vector<Language> g_languages;
    std::once_flag g_scanOnce;
    std::atomic<int> g_wanted{0};
    bool g_fixed = false; // RDR_LANG
    bool g_log = false;   // RDR_TEXT_LOG

    // Menus do port (thread da janela)
    std::mutex g_uiMutex;
    std::unordered_map<std::string, std::string> g_ui;
    std::set<std::string> g_uiMissing;

    // Textos do jogo (thread do jogo)
    int g_loaded = 0;
    std::unordered_map<std::string, std::u16string> g_text;
    std::unordered_map<std::string, uint32_t> g_block;      // chave -> traducao na memoria do jogo
    std::unordered_map<uint32_t, uint32_t> g_swapped;       // objeto -> ponteiro original
    std::set<std::string> g_missing;
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

    // Texto dos menus do port: UTF-8, so os escapes simples
    std::string unescapeUtf8(const std::string &s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == 't' || s[i + 1] == '\\'))
            {
                out.push_back(s[i + 1] == 'n' ? '\n' : s[i + 1] == 't' ? '\t' : '\\');
                ++i;
            }
            else
                out.push_back(s[i]);
        }
        return out;
    }

    // Le o arquivo; text e ui podem ser nulos (so o nome)
    void parseFile(const fs::path &file, std::string *name, std::unordered_map<std::string, std::u16string> *text,
                   std::unordered_map<std::string, std::string> *ui)
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
            if (line.rfind("@language ", 0) == 0)
            {
                if (name)
                    *name = line.substr(10);
                if (!text && !ui)
                    return;
                continue;
            }
            const size_t tab = line.find('\t');
            if (line.empty() || line[0] == '#' || tab == std::string::npos || tab + 1 == line.size())
                continue;
            const std::string key = line.substr(0, tab), value = line.substr(tab + 1);
            if (text)
                (*text)[key] = decodeLine(value);
            if (ui)
                (*ui)[key] = unescapeUtf8(value);
        }
    }

    std::string textUpper(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    }

    void scanLanguages()
    {
        g_languages.push_back({"EN", "English", {}});
        const fs::path elfDir = PS2Runtime::getIoPaths().elfDirectory;
        std::error_code ec;
        for (const fs::path &dir : {elfDir.parent_path() / "text", elfDir / "text"})
            for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            {
                if (textUpper(it->path().extension().string()) != ".TXT")
                    continue;
                Language lang{textUpper(it->path().stem().string()), {}, it->path()};
                if (std::any_of(g_languages.begin(), g_languages.end(), [&](const Language &l) { return l.id == lang.id; }))
                    continue;
                parseFile(lang.file, &lang.name, nullptr, nullptr);
                if (lang.name.empty())
                    lang.name = lang.id;
                if (lang.id == "EN")
                    g_languages[0] = lang; // um EN.txt pode trocar textos em ingles
                else
                    g_languages.push_back(lang);
            }
        const char *log = std::getenv("RDR_TEXT_LOG");
        g_log = log && log[0] == '1';
    }

    void ensureScanned() { std::call_once(g_scanOnce, scanLanguages); }

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

    // Troca de idioma na thread do jogo: devolve os textos originais, libera os blocos e copia os do idioma novo para o
    // heap do runtime (o jogo nao o usa nem libera)
    void switchLanguage(uint8_t *rdram, PS2Runtime *runtime, int index)
    {
        for (const auto &[object, original] : g_swapped)
            write32(rdram, object + 8u, original);
        g_swapped.clear();
        for (const auto &[key, block] : g_block)
            runtime->guestFree(block);
        g_block.clear();
        g_text.clear();
        g_loaded = index;
        if (index <= 0 || index >= static_cast<int>(g_languages.size()) || g_languages[index].file.empty())
            return;
        parseFile(g_languages[index].file, nullptr, &g_text, nullptr);
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
        std::fprintf(stderr, "[rdr] texto: %s, %zu textos, %zu bytes na memoria do jogo, %zu sem espaco\n",
                     g_languages[index].id.c_str(), g_text.size(), used, skipped);
    }

    void textLookup(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int wanted = g_wanted.load(std::memory_order_relaxed);
        if (wanted != g_loaded)
            switchLanguage(rdram, runtime, wanted);
        const uint32_t keyAddress = GPR_U32(ctx, 4);
        if (keyAddress != 0u && (!g_block.empty() || g_log))
        {
            const char *key = reinterpret_cast<const char *>(rdram + (keyAddress & kRamMask));
            const auto block = g_block.find(key);
            if (block == g_block.end() && g_log && g_missing.insert(key).second)
                std::fprintf(stderr, "[rdr] texto sem traducao: %s\n", key);
            const uint32_t object = block != g_block.end() ? findTextObject(rdram, keyHash(key)) : 0u;
            if (object != 0u && g_swapped.find(object) == g_swapped.end())
            {
                g_swapped[object] = read32(rdram, object + 8u);
                write32(rdram, object + 8u, block->second);
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

int rdrTextLanguageCount()
{
    ensureScanned();
    return static_cast<int>(g_languages.size());
}

std::string rdrTextLanguageName(int index)
{
    ensureScanned();
    return index >= 0 && index < static_cast<int>(g_languages.size()) ? g_languages[index].name : std::string();
}

std::string rdrTextLanguageId(int index)
{
    ensureScanned();
    return index >= 0 && index < static_cast<int>(g_languages.size()) ? g_languages[index].id : std::string("EN");
}

// Indice do id (sem diferenciar maiusculas); -1 se nao houver
int rdrTextLanguageIndex(const std::string &id)
{
    ensureScanned();
    for (size_t i = 0; i < g_languages.size(); ++i)
        if (g_languages[i].id == textUpper(id))
            return static_cast<int>(i);
    return -1;
}

// Sem escolha salva: o idioma do Windows ("pt-BR" acha PT-BR.txt; "pt-PT" tambem, pelo "PT"), senao ingles
int rdrTextDefaultLanguage()
{
    ensureScanned();
    const std::string locale = textUpper(rdrSystemLocaleName());
    if (const int exact = rdrTextLanguageIndex(locale); exact >= 0)
        return exact;
    const std::string base = locale.substr(0, locale.find('-'));
    for (size_t i = 1; i < g_languages.size(); ++i)
        if (g_languages[i].id.substr(0, g_languages[i].id.find('-')) == base)
            return static_cast<int>(i);
    return 0;
}

// Vale nas proximas buscas: telas ja abertas mudam ao serem reabertas
void rdrTextSetLanguage(int index)
{
    ensureScanned();
    if (g_fixed || index < 0 || index >= static_cast<int>(g_languages.size()))
        return;
    std::unordered_map<std::string, std::string> ui;
    if (!g_languages[index].file.empty())
        parseFile(g_languages[index].file, nullptr, nullptr, &ui);
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        g_ui.swap(ui);
    }
    g_wanted.store(index, std::memory_order_relaxed);
}

// Menus do port: o texto em ingles e a chave
std::string rdrTextTr(const std::string &english)
{
    std::lock_guard<std::mutex> lock(g_uiMutex);
    const auto it = g_ui.find(english);
    if (it != g_ui.end())
        return it->second;
    if (g_log && g_wanted.load(std::memory_order_relaxed) != 0 && g_uiMissing.insert(english).second)
        std::fprintf(stderr, "[rdr] menu sem traducao: %s\n", english.c_str());
    return english;
}

void rdrTextInstall(PS2Runtime &runtime)
{
    ensureScanned();
    std::fprintf(stderr, "[rdr] texto: %zu idioma(s)\n", g_languages.size());
    if (const char *env = std::getenv("RDR_LANG"); env && env[0])
    {
        rdrTextSetLanguage(std::max(0, rdrTextLanguageIndex(env)));
        g_fixed = true;
    }
    if (g_languages.size() < 2u && !g_log)
        return;
    g_origTextLookup = runtime.lookupFunction(0x0017F398u);
    runtime.replaceFunction(0x0017F398u, textLookup);
    g_origTextDestroy = runtime.lookupFunction(0x0017B980u);
    runtime.replaceFunction(0x0017B980u, textDestroy);
}
