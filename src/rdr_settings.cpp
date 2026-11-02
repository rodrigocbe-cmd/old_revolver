// Menu de opcoes do port (docs/OPCOES.md): mesmo estilo dos menus Options do jogo (titulo vermelho na
// regulatorcondensed, itens na berrylium, valor com setas ou barra, teclas no rodape), com o texto menor para caber
// mais opcoes. No menu principal do jogo abre no lugar do Options original (o botao Options nao chega ao jogo; o item
// "Game Options" abre o original); em qualquer tela abre com F1 na versao de distribuicao (ou F4 sempre). O jogo nao
// recebe entradas enquanto esta aberto, nem depois, ate todas as teclas serem soltas. As opcoes ficam em Documentos\Red Dead Revolver\settings.ini. As que dependem da criacao da janela ou
// do GS valem na proxima abertura do jogo; as outras valem na hora. Opcoes ainda nao implementadas aparecem com o
// aviso "Coming soon".
#include "ps2_runtime.h"
#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

fs::path rdrDocumentsDirectory();                                                 // rdr_win32.cpp
void rdrSetEnvDefault(const char *name, const std::string &value);                // rdr_win32.cpp
bool rdrUiReady();                                                                // rdr_quit_dialog.cpp
float rdrUiScaleFor(int font, char ref, float pixelHeight);                       // rdr_quit_dialog.cpp
float rdrUiTextWidth(int font, const std::string &text, float scale);             // rdr_quit_dialog.cpp
void rdrUiDrawText(int font, const std::string &text, float x, float baseline, float scale, Color color);
std::string rdrKeyShortLabel(int code);                                           // rdr_keyicons.cpp
int rdrControlsPrimaryForPadBit(int bit);                                         // rdr_controls.cpp
float rdrControlsWalkTilt();                                                      // rdr_controls.cpp
void rdrControlsSetWalkTilt(float t);                                             // rdr_controls.cpp
void rdrControlsApply();                                                          // rdr_controls.cpp
bool rdrControlsSave();                                                           // rdr_controls.cpp

namespace
{
    // ---------------------------------------------------------------- opcoes
    struct StSettings
    {
        int windowMode = 0;  // 0 tela cheia sem borda, 1 janela
        int resolution = 0;  // indice de kStResolutions
        int aspect = 0;      // 0 16:9, 1 4:3
        int frameLimit = 1;  // 0 30, 1 60, 2 sem limite
        int vsync = 0;       // 0 desligado, 1 ligado
        int ssaa = 1;        // 0 desligado, 1 4x, 2 16x
        int hdTextures = 1;  // texturas supersampled
        int hiresOutput = 1; // saida na resolucao interna
        int filter = 0;      // 0 suave, 1 nitido
        int volume = 10;     // 0..10
        int mouseSens = 6;   // 1..20 (unidades do analogico por pixel)
        int invertY = 0;
        int walkSpeed = 9;   // 0..9 -> walk_tilt 0,40..0,76 (docs/CONTROLES.md)
    };

    const char *const kStResolutions[] = {"Native", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160"};
    const int kStResW[] = {0, 1280, 1600, 1920, 2560, 3840};
    const int kStResH[] = {0, 720, 900, 1080, 1440, 2160};

    StSettings g_st;

    float stWalkTilt(int speed) { return 0.40f + 0.04f * std::clamp(speed, 0, 9); }

    fs::path stFile()
    {
        if (const char *env = std::getenv("RDR_SETTINGS_INI"); env && env[0])
            return fs::u8path(env);
        const fs::path docs = rdrDocumentsDirectory();
        return docs.empty() ? fs::path("settings.ini") : docs / "Red Dead Revolver" / "settings.ini";
    }

    struct StKey
    {
        const char *name;
        int StSettings::*field;
        int lo, hi;
    };
    const StKey kStKeys[] = {
        {"window_mode", &StSettings::windowMode, 0, 1}, {"resolution", &StSettings::resolution, 0, 5},
        {"aspect", &StSettings::aspect, 0, 1},          {"frame_limit", &StSettings::frameLimit, 0, 2},
        {"vsync", &StSettings::vsync, 0, 1},            {"supersampling", &StSettings::ssaa, 0, 2},
        {"hd_textures", &StSettings::hdTextures, 0, 1}, {"hires_output", &StSettings::hiresOutput, 0, 1},
        {"image_filter", &StSettings::filter, 0, 1},    {"master_volume", &StSettings::volume, 0, 10},
        {"mouse_sensitivity", &StSettings::mouseSens, 1, 20}, {"invert_mouse_y", &StSettings::invertY, 0, 1},
        {"walk_speed", &StSettings::walkSpeed, 0, 9}};

    void stLoad()
    {
        std::ifstream in(stFile());
        std::string line;
        while (std::getline(in, line))
        {
            const size_t eq = line.find('=');
            if (line.empty() || line[0] == ';' || line[0] == '[' || eq == std::string::npos)
                continue;
            std::string key = line.substr(0, eq);
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            const int value = std::atoi(line.c_str() + eq + 1);
            for (const StKey &k : kStKeys)
                if (key == k.name)
                    g_st.*k.field = std::clamp(value, k.lo, k.hi);
        }
    }

    bool stSave()
    {
        const fs::path file = stFile();
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "; Red Dead Revolver - PC settings (edited by the in-game menu: F1)\r\n"
               "; window_mode 0 fullscreen 1 window | resolution 0 native 1 720p 2 900p 3 1080p 4 1440p 5 2160p\r\n"
               "; aspect 0 16:9 1 4:3 | frame_limit 0 30 1 60 2 unlimited | supersampling 0 off 1 4x 2 16x\r\n"
               "; image_filter 0 smooth 1 sharp | master_volume 0-10 | mouse_sensitivity 1-20 | walk_speed 0-9\r\n\r\n";
        for (const StKey &k : kStKeys)
            out << k.name << " = " << g_st.*k.field << "\r\n";
        return static_cast<bool>(out);
    }

    // Vale na hora (thread da janela)
    void stApplyLive()
    {
        SetTargetFPS(g_st.frameLimit == 0 ? 30 : g_st.frameLimit == 1 ? 60 : 0);
        if (g_st.vsync)
            SetWindowState(FLAG_VSYNC_HINT);
        else
            ClearWindowState(FLAG_VSYNC_HINT);
        SetMasterVolume(g_st.volume / 10.0f);
        ps2SetFrameFilterSharp(g_st.filter == 1);
        ps2PadSetMouseTuning(static_cast<float>(g_st.mouseSens), g_st.invertY != 0);
    }

    // ---------------------------------------------------------------- itens do menu
    struct StItem
    {
        std::string label, help;
        std::vector<std::string> values; // escolha; vazio + slider = barra
        int StSettings::*field = nullptr;
        int sliderMax = 0;       // > 0: barra de 0..sliderMax
        bool restart = false;    // vale na proxima abertura do jogo
        bool soon = false;       // ainda nao implementado
        std::function<void()> onChange;
    };

    struct StPage
    {
        std::string title;
        std::vector<StItem> items;
    };

    std::vector<StPage> stPages()
    {
        std::vector<StPage> pages;
        const std::vector<std::string> offOn = {"Off", "On"};
        pages.push_back({"Display Options",
                         {{"Window Mode", "Borderless fullscreen or a window.", {"Fullscreen", "Window"}, &StSettings::windowMode, 0, true},
                          {"Resolution", "Window size. Native uses your monitor resolution.",
                           std::vector<std::string>(std::begin(kStResolutions), std::end(kStResolutions)), &StSettings::resolution, 0, true},
                          {"Aspect Ratio", "16:9 widescreen or the original 4:3 picture.", {"16:9", "4:3"}, &StSettings::aspect, 0, true},
                          {"Frame Rate Limit", "Limits how often the window is refreshed.", {"30", "60", "Unlimited"}, &StSettings::frameLimit},
                          {"VSync", "Synchronizes the picture with the monitor to avoid tearing.", offOn, &StSettings::vsync},
                          {"Brightness", "Picture brightness and calibration.", {}, nullptr, 0, false, true},
                          {"HUD Size", "Makes the game menus and HUD smaller on large screens.", {}, nullptr, 0, false, true}}});
        pages.push_back({"Graphics Options",
                         {{"Quality Preset", "Sets all graphics options at once (Low, Medium, High, Ultra).", {}, nullptr, 0, false, true},
                          {"Supersampling", "Renders the game at a higher internal resolution. 16x is the sharpest and the slowest.",
                           {"Off", "4x", "16x"}, &StSettings::ssaa, 0, true},
                          {"HD Textures", "Samples textures at the internal resolution (needs supersampling).", offOn, &StSettings::hdTextures, 0, true},
                          {"High Resolution Output", "Shows the internal resolution instead of the original 640 x 448.", offOn, &StSettings::hiresOutput, 0, true},
                          {"Image Filter", "Smooth or sharp scaling of the picture to the window.", {"Smooth", "Sharp"}, &StSettings::filter},
                          {"Show Frame Rate", "Frame rate counter in the corner of the screen.", {}, nullptr, 0, false, true}}});
        pages.push_back({"Audio Options",
                         {{"Master Volume", "Volume of all game audio. Music and effects are in the game's Sound Options.",
                           {}, &StSettings::volume, 10},
                          {"Output Device", "Speakers or headphones used by the game.", {}, nullptr, 0, false, true}}});
        pages.push_back({"Controls Options",
                         {{"Mouse Sensitivity", "How fast the mouse turns the camera and aims.", {}, &StSettings::mouseSens, 20},
                          {"Invert Mouse Y", "Moving the mouse up looks down.", offOn, &StSettings::invertY},
                          {"Walk Speed", "Stick tilt used when walking with the keyboard. Hold Run to run.", {}, &StSettings::walkSpeed, 9},
                          {"Key Bindings", "Change the keyboard and mouse keys (for now in Documents\\Red Dead Revolver\\controls.ini).",
                           {}, nullptr, 0, false, true},
                          {"Toggle Run / Aim", "Press once instead of holding.", {}, nullptr, 0, false, true}}});
        pages[3].items[2].onChange = [] {
            rdrControlsSetWalkTilt(stWalkTilt(g_st.walkSpeed));
            rdrControlsApply();
        };
        return pages;
    }

    // ---------------------------------------------------------------- estado e entrada
    struct StMenu
    {
        bool open = false;
        int page = -1; // -1 = lista de paginas
        int selected = 0;
        int pageSelected = 0;
        StSettings backup;
        bool restartPending = false;
        double openedAt = 0.0;
        bool releasing = false;   // fechado, esperando soltar as teclas
        bool openGameOptions = false; // ao soltar, abre o Options original do jogo
    } g_menu;

    bool g_mainMenu = false; // menu principal do jogo (escritorio): o botao Options abre este menu
    constexpr uint16_t kStPadCircle = 0x2000u;

    // Alguma tecla, botao do mouse ou do controle ainda pressionado
    bool stAnyHeld()
    {
        for (int k = KEY_SPACE; k <= KEY_KB_MENU; ++k)
            if (IsKeyDown(k))
                return true;
        for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_BACK; ++b)
            if (IsMouseButtonDown(b))
                return true;
        if (IsGamepadAvailable(0))
            for (int b = GAMEPAD_BUTTON_LEFT_FACE_UP; b <= GAMEPAD_BUTTON_RIGHT_THUMB; ++b)
                if (IsGamepadButtonDown(0, b))
                    return true;
        return false;
    }

    bool stPressed(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

    bool stPad(int button) { return IsGamepadAvailable(0) && IsGamepadButtonPressed(0, button); }

    bool stMappedPressed(int padBit)
    {
        const int code = rdrControlsPrimaryForPadBit(padBit);
        return code > 0 && IsKeyPressed(code);
    }

    // ---------------------------------------------------------------- desenho
    const Color kStRed{110, 14, 6, 255}, kStWhite{232, 232, 228, 255}, kStGray{120, 120, 120, 255},
        kStDim{70, 70, 70, 255};

    struct StView
    {
        float x, y, w, h; // area da imagem do jogo
        float X(float f) const { return x + f * w; }
        float Y(float f) const { return y + f * h; }
    };

    void stText(int font, const std::string &t, float x, float baseline, float capHeight, Color c, bool center = false)
    {
        const float s = rdrUiScaleFor(font, font == 0 ? 'E' : 'B', capHeight);
        const float w = rdrUiTextWidth(font, t, s);
        rdrUiDrawText(font, t, center ? x - w / 2.0f : x, baseline, s, c);
    }

    // Tecla desenhada como nos icones do jogo (caixa com o rotulo)
    float stKeyCap(const std::string &label, float x, float baseline, float h)
    {
        const float w = std::max(h * 1.6f, rdrUiTextWidth(1, label, rdrUiScaleFor(1, 'B', h * 0.42f)) + h * 0.5f);
        const Rectangle r{x, baseline - h * 0.8f, w, h};
        DrawRectangleLinesEx(r, std::max(1.0f, h * 0.05f), kStGray);
        stText(1, label, x + w / 2.0f, baseline - h * 0.1f, h * 0.42f, kStWhite, true);
        return w;
    }

    void stPrompts(const StView &v, const std::vector<std::pair<std::string, std::string>> &items)
    {
        const float h = v.h * 0.034f, cap = v.h * 0.024f;
        float total = 0;
        for (const auto &[key, text] : items)
            total += std::max(h * 1.6f, rdrUiTextWidth(1, key, rdrUiScaleFor(1, 'B', h * 0.42f)) + h * 0.5f) + h * 0.3f +
                     rdrUiTextWidth(1, text, rdrUiScaleFor(1, 'B', cap)) + h * 1.2f;
        float x = v.X(0.5f) - total / 2.0f;
        const float base = v.Y(0.92f);
        for (const auto &[key, text] : items)
        {
            x += stKeyCap(key, x, base, h) + h * 0.3f;
            stText(1, text, x, base, cap, kStWhite);
            x += rdrUiTextWidth(1, text, rdrUiScaleFor(1, 'B', cap)) + h * 1.2f;
        }
    }

    void stDrawValue(const StView &v, const StItem &item, float baseline, bool sel)
    {
        const float lx = v.X(0.56f), rx = v.X(0.88f), cap = v.h * 0.026f;
        const Color c = item.soon ? kStDim : sel ? kStWhite : kStGray;
        if (item.soon)
        {
            stText(1, "Coming soon", (lx + rx) / 2.0f, baseline, cap, kStDim, true);
            return;
        }
        if (item.sliderMax > 0)
        {
            // < ------X------ > como no menu original
            const float midY = baseline - cap * 0.45f;
            stText(1, "<", lx, baseline, cap, c);
            stText(1, ">", rx - rdrUiTextWidth(1, ">", rdrUiScaleFor(1, 'B', cap)), baseline, cap, c);
            const float a = lx + cap * 1.0f, b = rx - cap * 1.0f;
            DrawLineEx({a, midY}, {b, midY}, std::max(1.0f, v.h * 0.002f), kStWhite);
            const float t = static_cast<float>(g_st.*item.field) / item.sliderMax;
            stText(0, "X", a + (b - a) * t, baseline + cap * 0.1f, cap * 1.1f, sel ? kStRed : kStGray, true);
            return;
        }
        const float tri = cap * 0.55f, midY = baseline - cap * 0.5f;
        DrawTriangle({lx, midY}, {lx + tri, midY + tri}, {lx + tri, midY - tri}, c);
        DrawTriangle({rx, midY}, {rx - tri, midY - tri}, {rx - tri, midY + tri}, c);
        const int idx = std::clamp(g_st.*item.field, 0, static_cast<int>(item.values.size()) - 1);
        stText(1, item.values[idx], (lx + rx) / 2.0f, baseline, cap, c, true);
    }

    void stDraw(const StView &v, const std::vector<StPage> &pages)
    {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);
        const std::string accept = rdrKeyShortLabel(rdrControlsPrimaryForPadBit(14));
        const std::string back = rdrKeyShortLabel(rdrControlsPrimaryForPadBit(12));
        if (g_menu.page < 0)
        {
            stText(0, "PC Settings", v.X(0.5f), v.Y(0.20f), v.h * 0.07f, kStRed, true);
            for (size_t i = 0; i <= pages.size(); ++i)
            {
                const std::string name = i < pages.size() ? pages[i].title.substr(0, pages[i].title.find(' ')) : "Game Options";
                stText(0, name, v.X(0.5f), v.Y(0.34f + 0.07f * i), v.h * 0.045f,
                       static_cast<int>(i) == g_menu.selected ? kStWhite : kStGray, true);
            }
            if (g_menu.selected == static_cast<int>(pages.size()))
                stText(1, "The game's original options: brightness, screen position, subtitles, music and effects volume.",
                       v.X(0.5f), v.Y(0.80f), v.h * 0.02f, kStGray, true);
            stPrompts(v, {{accept, "Select"}, {back, "Back"}});
            return;
        }
        const StPage &page = pages[g_menu.page];
        stText(0, page.title, v.X(0.5f), v.Y(0.16f), v.h * 0.06f, kStRed, true);
        const float row = v.h * 0.056f, top = v.Y(0.28f);
        for (size_t i = 0; i < page.items.size(); ++i)
        {
            const StItem &item = page.items[i];
            const bool sel = static_cast<int>(i) == g_menu.selected;
            const float base = top + row * i;
            stText(1, item.label, v.X(0.12f), base, v.h * 0.026f, item.soon ? kStDim : sel ? kStWhite : kStGray);
            stDrawValue(v, item, base, sel);
        }
        // descricao do item selecionado
        const StItem &cur = page.items[std::clamp(g_menu.selected, 0, static_cast<int>(page.items.size()) - 1)];
        std::string help = cur.help;
        if (cur.soon)
            help += "  This option will be added in a future update.";
        else if (cur.restart)
            help += "  Takes effect the next time the game starts.";
        stText(1, help, v.X(0.5f), v.Y(0.80f), v.h * 0.02f, kStGray, true);
        if (g_menu.restartPending)
            stText(1, "Some changes will apply after restarting the game.", v.X(0.5f), v.Y(0.84f), v.h * 0.018f, kStRed, true);
        stPrompts(v, {{accept, "Accept"}, {back, "Cancel"}, {"< >", "Change"}});
    }

    // ---------------------------------------------------------------- laco (thread da janela)
    bool stOverlay(const float game[4])
    {
        static const std::vector<StPage> pages = stPages();
        const bool openKey = IsKeyPressed(KEY_F4) || (ps2DistMode() && IsKeyPressed(KEY_F1));
        if (g_menu.releasing)
        {
            if (stAnyHeld())
                return true; // ainda bloqueado: a tecla que fechou o menu nao chega ao jogo
            g_menu.releasing = false;
            if (g_menu.openGameOptions)
            {
                g_menu.openGameOptions = false;
                ps2PadInjectButtons(kStPadCircle, 250); // Options original do jogo
            }
            return false;
        }
        const bool optionsButton = g_mainMenu && (stMappedPressed(13) || stPad(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT));
        if (!g_menu.open)
        {
            if (!openKey && !optionsButton)
                return false;
            g_menu = {};
            g_menu.open = true;
            g_menu.openedAt = GetTime();
            return true; // o mesmo aperto nao conta como entrada no menu
        }
        if (!rdrUiReady())
        {
            g_menu.open = false;
            return false;
        }
        const bool up = stPressed(KEY_UP) || stPressed(KEY_W) || stPad(GAMEPAD_BUTTON_LEFT_FACE_UP);
        const bool down = stPressed(KEY_DOWN) || stPressed(KEY_S) || stPad(GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        const bool left = stPressed(KEY_LEFT) || stPressed(KEY_A) || stPad(GAMEPAD_BUTTON_LEFT_FACE_LEFT);
        const bool right = stPressed(KEY_RIGHT) || stPressed(KEY_D) || stPad(GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
        const bool accept = IsKeyPressed(KEY_ENTER) || stMappedPressed(14) || stPad(GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        const bool back = IsKeyPressed(KEY_ESCAPE) || stMappedPressed(12) || stPad(GAMEPAD_BUTTON_RIGHT_FACE_UP) ||
                          stPad(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || openKey;

        if (g_menu.page < 0)
        {
            const int n = static_cast<int>(pages.size()) + 1; // + "Game Options"
            if (up)
                g_menu.selected = (g_menu.selected + n - 1) % n;
            if (down)
                g_menu.selected = (g_menu.selected + 1) % n;
            if (accept && g_menu.selected == n - 1)
            {
                g_menu.open = false;
                g_menu.openGameOptions = true;
            }
            else if (accept)
            {
                g_menu.page = g_menu.selected;
                g_menu.pageSelected = g_menu.selected;
                g_menu.selected = 0;
                g_menu.backup = g_st;
            }
            else if (back)
                g_menu.open = false;
        }
        else
        {
            const StPage &page = pages[g_menu.page];
            const int n = static_cast<int>(page.items.size());
            if (up)
                g_menu.selected = (g_menu.selected + n - 1) % n;
            if (down)
                g_menu.selected = (g_menu.selected + 1) % n;
            const StItem &item = page.items[g_menu.selected];
            if ((left || right) && !item.soon && item.field)
            {
                int &value = g_st.*item.field;
                const int lo = item.field == &StSettings::mouseSens ? 1 : 0;
                const int hi = item.sliderMax > 0 ? item.sliderMax : static_cast<int>(item.values.size()) - 1;
                value = std::clamp(value + (right ? 1 : -1), lo, hi);
                if (item.onChange)
                    item.onChange();
                if (!item.restart)
                    stApplyLive();
            }
            if (accept)
            {
                for (const StItem &it : page.items)
                    if (it.restart && it.field && g_st.*it.field != g_menu.backup.*it.field)
                        g_menu.restartPending = true;
                stSave();
                rdrControlsSave();
                g_menu.page = -1;
                g_menu.selected = g_menu.pageSelected;
            }
            else if (back)
            {
                g_st = g_menu.backup;
                stApplyLive();
                for (const StItem &it : page.items)
                    if (it.onChange)
                        it.onChange();
                g_menu.page = -1;
                g_menu.selected = g_menu.pageSelected;
            }
        }
        if (g_menu.open)
        {
            stDraw({game[0], game[1], game[2], game[3]}, pages);
            return true;
        }
        g_menu.releasing = true; // fechou agora: segura as entradas ate soltar tudo
        return true;
    }
}

// Antes de criar a janela (setPlayDefaults do lancador): o que depende da janela e do GS vai por variaveis de ambiente
void rdrSettingsApplyAtBoot()
{
    stLoad();
    int sw = 0, sh = 0;
    if (g_st.resolution > 0)
        sw = kStResW[g_st.resolution], sh = kStResH[g_st.resolution];
    if (sw > 0)
        rdrSetEnvDefault("PS2X_WINDOW_SIZE", std::to_string(sw) + "x" + std::to_string(sh));
    rdrSetEnvDefault("PS2X_FULLSCREEN", g_st.windowMode == 0 ? "1" : "0");
    rdrSetEnvDefault("PS2X_ASPECT", g_st.aspect == 0 ? "16:9" : "4:3");
    if (g_st.aspect == 1)
        rdrSetEnvDefault("RDR_PARAMS", "nowidescreen");
    rdrSetEnvDefault("PS2X_GS_SSAA", g_st.ssaa == 0 ? "1" : g_st.ssaa == 1 ? "4" : "16");
    rdrSetEnvDefault("PS2X_GS_SS_TEXTURES", g_st.hdTextures ? "1" : "0");
    rdrSetEnvDefault("PS2X_GS_HIRES", g_st.hiresOutput ? "1" : "0");
}

// Com a janela criada (applyRdrOverrides): registra o menu; o resto vale no primeiro quadro
void rdrSettingsInstall()
{
    stLoad();
    g_st.walkSpeed = std::clamp(static_cast<int>((rdrControlsWalkTilt() - 0.40f) / 0.04f + 0.5f), 0, 9);
    ps2SetOverlayHandler([](const float game[4]) {
        static bool s_applied = false;
        if (!s_applied)
        {
            s_applied = true;
            stApplyLive(); // primeiro quadro: a janela ja existe
        }
        return stOverlay(game);
    });
}

// Menu principal do jogo (modo dos prompts book/poster/journal, rdr_overrides.cpp): o botao Options abre este menu e
// nao chega ao jogo
void rdrSettingsSetMainMenu(bool mainMenu)
{
    g_mainMenu = mainMenu;
    ps2PadSetSuppressedButtons(mainMenu ? kStPadCircle : 0u);
}
