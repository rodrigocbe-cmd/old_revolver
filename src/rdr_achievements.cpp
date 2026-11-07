// Conquistas (FUNCOES.md #105): as 20 do RetroAchievements (achievements/achievements.json) sao de capitulo concluido.
// O perfil guarda em +0x29C o nome da ultima fase concluida (save e RAM em *0x742120 usam o mesmo layout). A coluna +0x34
// da tabela de fases (0x7436B8) marca 20 fases, na mesma ordem das conquistas. O registro fica em
// Documentos\Red Dead Revolver\achievements.ini (id = data) e so acumula: repetir um capitulo nao apaga nada.
//
// Trapacas (FUNCOES.md #110): o save tem 6 perfis de 0xB18 bytes; nome em +0x81C. A palavra em +0x860 guarda as trapacas
// ligadas (FUN_00180C88, bits 6 a 15) e so os bits abaixo de 16 sao usados pelo jogo; o bit 31 e a marca do port, "este perfil
// ja usou trapaca". Ela vai junto no save, entao vale por perfil, sai quando o perfil e apagado e nunca e desligada.
#include "ps2_runtime.h"
#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

fs::path rdrDocumentsDirectory(); // rdr_win32.cpp

namespace
{
    // Ordem da historia (tabela 0x7436B8), sem as fases "tomb", que se repetem entre capitulos
    const char *const kStory[] = {"perfnew", "hng",      "sher70", "traincar", "traingood", "ghost",   "shernite", "bomb",     "gunsmoke",
                                  "cemetery", "ranch",   "barfight", "bridge", "coach",     "woods",   "core",     "corenew",  "msn",
                                  "msncyn",  "fort",     "train",  "trainend", "roydusk",   "royfinal", "mans",    "mansintb", "mansroof"};
    // Fase que conclui cada conquista (id 1..20)
    const char *const kAchievementLevel[20] = {"perfnew", "hng",  "sher70", "traingood", "ghost",   "shernite", "bomb",     "cemetery", "ranch", "barfight",
                                               "bridge",  "coach", "woods",  "corenew",   "msncyn", "fort",     "trainend", "royfinal", "mans",  "mansroof"};
    constexpr uint32_t kProfilePtr = 0x00742120u, kLastLevelOffset = 0x29Cu;
    constexpr uint32_t kProfileSize = 0xB18u, kProfileCount = 6u, kNameOffset = 0x81Cu, kCheatsOffset = 0x860u;
    // Invincibility 6, Golden Gun 7, Show Player Position 8, Show AI Info 9, Infinite Deadeye 10, Bite the Bullet 12,
    // Sad Stars 13, No HUD 14, No Distortion 15
    constexpr uint32_t kCheatMask = 0xF7C0u, kCheatUsed = 0x80000000u;

    std::atomic<bool> g_bigMoneyUsed{false}, g_recheckProfile{false};
    std::atomic<int64_t> g_toastUntilMs{0};

    int64_t nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    int storyIndex(const std::string &level)
    {
        for (size_t i = 0; i < std::size(kStory); ++i)
            if (level == kStory[i])
                return static_cast<int>(i);
        return -1;
    }

    fs::path dataDir()
    {
        if (const char *env = std::getenv("RDR_SAVE_DIR"); env && env[0])
            return fs::u8path(env);
        const fs::path docs = rdrDocumentsDirectory();
        return docs.empty() ? fs::path(".") : docs / "Red Dead Revolver";
    }

    fs::path recordFile() { return dataDir() / "achievements.ini"; }

    std::map<int, std::string> loadRecord()
    {
        std::map<int, std::string> record;
        std::ifstream in(recordFile());
        std::string line;
        while (std::getline(in, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == ';')
                continue;
            const int id = std::atoi(line.c_str());
            std::string date = line.substr(eq + 1u);
            date.erase(0, date.find_first_not_of(" \t"));
            while (!date.empty() && (date.back() == '\r' || date.back() == ' '))
                date.pop_back();
            if (id >= 1 && id <= 20)
                record[id] = date;
        }
        return record;
    }

    // Ultima fase concluida ("lvl_hng") -> registra o que falta. Devolve as conquistas novas.
    std::vector<int> mergeLevel(const char *name)
    {
        std::vector<int> added;
        if (std::strncmp(name, "lvl_", 4) != 0)
            return added;
        const int reached = storyIndex(name + 4);
        if (reached < 0)
            return added;
        std::map<int, std::string> record = loadRecord();
        char today[16];
        const std::time_t now = std::time(nullptr);
        std::strftime(today, sizeof(today), "%Y-%m-%d", std::localtime(&now));
        for (int id = 1; id <= 20; ++id)
        {
            const int level = storyIndex(kAchievementLevel[id - 1]);
            if (level <= reached && record.find(id) == record.end())
            {
                record[id] = today;
                added.push_back(id);
            }
        }
        if (added.empty())
            return added;
        std::error_code ec;
        fs::create_directories(dataDir(), ec);
        std::ofstream out(recordFile(), std::ios::binary | std::ios::trunc);
        out << "; Red Dead Revolver - achievements (id = date). Kept by the game and the launcher.\r\n";
        for (const auto &[id, date] : record)
            out << id << " = " << date << "\r\n";
        return added;
    }
}

// Lancador: le o save no disco
void rdrAchievementsSyncFromSave()
{
    std::ifstream in(dataDir() / "BASLUS-20500RDR" / "BASLUS-20500RDR", std::ios::binary);
    for (uint32_t slot = 0u; slot < kProfileCount; ++slot)
    {
        const std::streamoff start = static_cast<std::streamoff>(slot) * kProfileSize;
        uint32_t cheats = 0u;
        char name[24] = {};
        in.clear();
        in.seekg(start + kCheatsOffset);
        in.read(reinterpret_cast<char *>(&cheats), sizeof(cheats));
        in.seekg(start + kLastLevelOffset);
        in.read(name, sizeof(name) - 1);
        if (in && (cheats & (kCheatMask | kCheatUsed)) == 0u)
            mergeLevel(name);
    }
}

// Botao Big Money da tela de trapacas (rdr_overrides.cpp): nao passa pelos bits
void rdrAchievementsBigMoneyUsed()
{
    g_bigMoneyUsed.store(true, std::memory_order_relaxed);
}

// Volta do livro (onde se troca de perfil) ao escritorio: o aviso vale de novo
void rdrAchievementsRecheckProfile()
{
    g_recheckProfile.store(true, std::memory_order_relaxed);
}

// Thread da janela: aviso de 6 s no canto superior, por cima do jogo
void rdrAchievementsOverlay()
{
    if (nowMs() >= g_toastUntilMs.load(std::memory_order_relaxed))
        return;
    const float s = GetScreenHeight() / 1080.0f;
    const char *title = "Achievements disabled";
    const char *line = "This profile has used cheats.";
    const int titleSize = static_cast<int>(34 * s), lineSize = static_cast<int>(24 * s);
    const int width = std::max(MeasureText(title, titleSize), MeasureText(line, lineSize)) + static_cast<int>(48 * s);
    const int height = static_cast<int>(92 * s);
    const int x = (GetScreenWidth() - width) / 2, y = static_cast<int>(36 * s);
    DrawRectangle(x, y, width, height, {12, 8, 6, 225});
    DrawRectangleLines(x, y, width, height, {214, 170, 96, 255});
    DrawText(title, x + static_cast<int>(24 * s), y + static_cast<int>(14 * s), titleSize, {214, 170, 96, 255});
    DrawText(line, x + static_cast<int>(24 * s), y + static_cast<int>(54 * s), lineSize, {232, 232, 228, 255});
}

// Jogo, a cada quadro: confere o perfil na RAM a cada ~10 s
void rdrAchievementsPump(uint8_t *rdram)
{
    uint32_t base = 0u;
    std::memcpy(&base, rdram + kProfilePtr, 4);
    if (base == 0u)
        return;
    uint8_t *cheatWord = rdram + ((base + kCheatsOffset) & 0x1FFFFFFu);
    uint32_t cheats = 0u;
    std::memcpy(&cheats, cheatWord, 4);
    if ((cheats & kCheatMask) != 0u || g_bigMoneyUsed.exchange(false, std::memory_order_relaxed))
    {
        cheats |= kCheatUsed;
        std::memcpy(cheatWord, &cheats, 4);
    }
    char profile[16] = {};
    std::memcpy(profile, rdram + ((base + kNameOffset) & 0x1FFFFFFu), sizeof(profile) - 1u);
    static std::string s_warned;
    static uint32_t s_warnedBase = 0u;
    if (g_recheckProfile.exchange(false, std::memory_order_relaxed))
        s_warnedBase = 0u;
    if ((cheats & kCheatUsed) != 0u && (s_warnedBase != base || s_warned != profile))
    {
        s_warned = profile;
        s_warnedBase = base;
        g_toastUntilMs.store(nowMs() + 6000, std::memory_order_relaxed);
        std::fprintf(stderr, "[rdr] conquistas desativadas no perfil %s\n", profile);
    }
    static int s_tick = 0;
    if (++s_tick < 600)
        return;
    s_tick = 0;
    if ((cheats & kCheatUsed) != 0u)
        return;
    char name[24] = {};
    std::memcpy(name, rdram + ((base + kLastLevelOffset) & 0x1FFFFFFu), sizeof(name) - 1u);
    static std::string s_last;
    if (s_last == name)
        return;
    s_last = name;
    for (const int id : mergeLevel(name))
        std::fprintf(stderr, "[rdr] conquista %d registrada (%s)\n", id, name);
}

// ---------------------------------------------------------------- tela do lancador
namespace
{
    struct AchievementView
    {
        std::string title, description;
        Texture2D unlocked{}, locked{};
    };

    // Le "chave": "texto" do objeto a partir de pos (o JSON foi feito por nos: texto simples, UTF-8)
    std::string jsonValue(const std::string &text, size_t pos, const char *key)
    {
        const std::string marker = std::string("\"") + key + "\"";
        const size_t at = text.find(marker, pos);
        if (at == std::string::npos)
            return {};
        size_t i = text.find('"', text.find(':', at) + 1u);
        std::string out;
        for (++i; i < text.size() && text[i] != '"'; ++i)
        {
            if (text[i] == '\\' && i + 1u < text.size())
                ++i;
            out += text[i];
        }
        return out;
    }

    std::vector<AchievementView> loadViews(const fs::path &root)
    {
        std::vector<AchievementView> views;
        std::ifstream in(root / "achievements" / "achievements.json", std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        auto image = [&root](const std::string &url) {
            const fs::path file = root / "achievements" / "imagens" / url.substr(url.find_last_of('/') + 1u);
            return LoadTexture(file.string().c_str());
        };
        for (size_t pos = text.find("\"titulo_pt\""); pos != std::string::npos; pos = text.find("\"titulo_pt\"", pos + 1u))
        {
            AchievementView v;
            v.title = jsonValue(text, pos, "titulo_pt");
            v.description = jsonValue(text, pos, "descricao_pt");
            v.unlocked = image(jsonValue(text, pos, "imagem_desbloqueada"));
            v.locked = image(jsonValue(text, pos, "imagem_bloqueada"));
            SetTextureFilter(v.unlocked, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(v.locked, TEXTURE_FILTER_BILINEAR);
            views.push_back(std::move(v));
        }
        return views;
    }

    std::string fitText(const Font &font, std::string text, float size, float maxWidth)
    {
        if (MeasureTextEx(font, text.c_str(), size, 1.0f).x <= maxWidth)
            return text;
        while (!text.empty() && MeasureTextEx(font, (text + "...").c_str(), size, 1.0f).x > maxWidth)
        {
            text.pop_back();
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u)
                text.pop_back(); // nao corta um caractere UTF-8 no meio
        }
        return text + "...";
    }
}

void rdrAchievementsScreen(const fs::path &root)
{
    rdrAchievementsSyncFromSave();
    const std::map<int, std::string> record = loadRecord();
    std::vector<AchievementView> views = loadViews(root);
    // Fonte com acentos: a do lancador so carrega ASCII
    std::vector<int> codepoints;
    for (int c = 32; c < 256; ++c)
        codepoints.push_back(c);
    const fs::path fontPath = root / "launcher" / "Rye-Regular.ttf";
    Font font = LoadFontEx(fontPath.string().c_str(), 48, codepoints.data(), static_cast<int>(codepoints.size()));
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    // Um quadro antes de ler a entrada: o Enter que abriu esta tela ainda conta como apertado
    BeginDrawing();
    EndDrawing();
    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_BACKSPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            break;
        const float s = GetScreenHeight() / 576.0f;
        const float colWidth = GetScreenWidth() / 2.0f - 30.0f * s, rowHeight = 50.0f * s;
        BeginDrawing();
        ClearBackground({12, 8, 6, 255});
        const std::string header = "Achievements  " + std::to_string(record.size()) + " / " + std::to_string(views.size());
        DrawTextEx(font, header.c_str(), {20 * s, 12 * s}, 26 * s, 1.0f, {214, 170, 96, 255});
        for (size_t i = 0; i < views.size(); ++i)
        {
            const AchievementView &v = views[i];
            const auto done = record.find(static_cast<int>(i) + 1);
            const bool on = done != record.end();
            const float x = 20 * s + (i >= 10 ? colWidth + 20 * s : 0.0f);
            const float y = 50 * s + static_cast<float>(i % 10) * rowHeight;
            const Texture2D &icon = on ? v.unlocked : v.locked;
            const float size = 44 * s;
            if (icon.id != 0)
                DrawTexturePro(icon, {0, 0, static_cast<float>(icon.width), static_cast<float>(icon.height)}, {x, y, size, size}, {0, 0}, 0.0f, WHITE);
            const Color title = on ? Color{232, 214, 170, 255} : Color{130, 120, 110, 255};
            const float textX = x + size + 8 * s, textWidth = colWidth - size - 12 * s;
            DrawTextEx(font, fitText(font, v.title, 16 * s, textWidth).c_str(), {textX, y + 2 * s}, 16 * s, 1.0f, title);
            const std::string line = on ? done->second + " - " + v.description : v.description;
            DrawTextEx(font, fitText(font, line, 11 * s, textWidth).c_str(), {textX, y + 24 * s}, 11 * s, 1.0f, {150, 140, 130, 255});
        }
        EndDrawing();
    }
    for (AchievementView &v : views)
    {
        UnloadTexture(v.unlocked);
        UnloadTexture(v.locked);
    }
    UnloadFont(font);
}
