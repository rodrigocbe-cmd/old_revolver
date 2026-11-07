// Menu de opcoes do port (docs/OPCOES.md): mesmo estilo dos menus Options do jogo (titulo vermelho na
// regulatorcondensed, itens na berrylium, valor com setas ou barra, teclas no rodape), com o texto menor para caber
// mais opcoes. No menu principal do jogo abre no lugar do Options original (o botao Options nao chega ao jogo); as
// opcoes de Picture e Sound Options estao aqui, ligadas aos valores do jogo (*0x742120, salvos no perfil), e sao
// aplicadas pelos proprios handlers do jogo. Em qualquer tela abre com F1 na versao de distribuicao (ou F4 sempre). O jogo nao
// recebe entradas enquanto esta aberto, nem depois, ate todas as teclas serem soltas. As opcoes ficam em Documentos\Red Dead Revolver\settings.ini. As que dependem da criacao da janela ou
// do GS valem na proxima abertura do jogo; as outras valem na hora, inclusive modo de tela e resolucao. Opcoes ainda nao implementadas aparecem com o
// aviso "Coming soon".
#include "ps2_runtime.h"
#include "raylib.h"

#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
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
int rdrControlsMenuKeyForPadBit(int bit);                                         // rdr_controls.cpp
float rdrControlsWalkTilt();                                                      // rdr_controls.cpp
void rdrControlsSetWalkTilt(float t);                                             // rdr_controls.cpp
void rdrControlsApply();                                                          // rdr_controls.cpp
void rdrWin32ApplyWindowMode(void *window, bool fullscreen, int width, int height); // rdr_win32.cpp
void rdrMenuButtonsPump();                                                        // rdr_overrides.cpp
void rdrAchievementsPump(uint8_t *rdram);                                          // rdr_achievements.cpp
void rdrFmvOverlay();                                                             // rdr_fmv.cpp
int rdrTextLanguageCount();                                                       // rdr_text.cpp
std::string rdrTextLanguageName(int index);                                       // rdr_text.cpp
std::string rdrTextLanguageId(int index);                                         // rdr_text.cpp
int rdrTextLanguageIndex(const std::string &id);                                  // rdr_text.cpp
int rdrTextDefaultLanguage();                                                     // rdr_text.cpp
void rdrTextSetLanguage(int index);                                               // rdr_text.cpp
void rdrAchievementsOverlay();                                                    // rdr_achievements.cpp
void rdrTitleLogoPump(uint8_t *rdram);                                            // rdr_overrides.cpp
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
        int language = 0;    // indice em rdrTextLanguage*; no settings.ini vai o id (EN, PT-BR...)
        // Opcoes do jogo (nao vao ao settings.ini: o jogo as salva no perfil; lidas ao abrir o menu)
        int brightness = 10; // 0..20: fracao do slider original
        int posX = 25;       // 0..50
        int posY = 25;       // 0..50
        int sfxVolume = 10;  // 0..10
        int musicVolume = 10;
        int audioMode = 0;   // 0 estereo, 1 surround, 2 mono
        int subtitles = 0;
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

    std::string g_languageId; // como veio do settings.ini; vazio = ainda nao escolhido (usa o do Windows)

    // Indice do idioma: o salvo, senao o do Windows. Valores 0/1/2 sao da versao com "Auto".
    void stResolveLanguage()
    {
        const std::string id = g_languageId == "1" ? "EN" : g_languageId == "2" ? "PT-BR" : g_languageId;
        const int index = id.empty() || id == "0" ? -1 : rdrTextLanguageIndex(id);
        g_st.language = index >= 0 ? index : rdrTextDefaultLanguage();
    }

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
            if (key == "language")
            {
                g_languageId = line.substr(eq + 1);
                g_languageId.erase(std::remove_if(g_languageId.begin(), g_languageId.end(), ::isspace), g_languageId.end());
            }
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
        out << "language = " << rdrTextLanguageId(g_st.language) << "\r\n";
        return static_cast<bool>(out);
    }

    // Vale na hora (thread da janela)
    void stApplyLive()
    {
        // Modo da janela: so na versao de distribuicao (os testes controlam a propria janela)
        static int s_mode = -1, s_resolution = -1;
        if (ps2DistMode() && (s_mode != g_st.windowMode || s_resolution != g_st.resolution))
        {
            s_mode = g_st.windowMode;
            s_resolution = g_st.resolution;
            rdrWin32ApplyWindowMode(GetWindowHandle(), g_st.windowMode == 0, kStResW[g_st.resolution], kStResH[g_st.resolution]);
        }
        SetTargetFPS(g_st.frameLimit == 0 ? 30 : g_st.frameLimit == 1 ? 60 : 0);
        if (g_st.vsync)
            SetWindowState(FLAG_VSYNC_HINT);
        else
            ClearWindowState(FLAG_VSYNC_HINT);
        SetMasterVolume(g_st.volume / 10.0f);
        ps2SetFrameFilterSharp(g_st.filter == 1);
        ps2PadSetMouseTuning(static_cast<float>(g_st.mouseSens), g_st.invertY != 0);
    }

    // ---------------------------------------------------------------- opcoes do jogo
    // Estrutura de opcoes em *0x742120 (FUN_0012B800/FUN_001310E8): +0x44 modo de audio (0x20 estereo, 0x22 surround,
    // 0x1F mono), +0x48 efeitos, +0x4C musica, +0x50 brilho (0,5..1,5), +0x54 horizontal (-128..127), +0x58 vertical
    // (-48..47), +0x5C bit 0 widescreen, bit 1 legendas. O byte 0x4F6EF0 pede para o jogo salvar o perfil.
    // Para aplicar, o menu chama os handlers das telas originais com um objeto falso: eles so leem
    // obj+0x4xx -> widget, widget+0x160 (slider 0..1) e widget+0x1290 (combo). Rodam na thread do jogo, um por quadro.
    constexpr uint32_t kGameOptionsPtr = 0x00742120u, kGameSaveDirty = 0x004F6EF0u;
    constexpr uint32_t kFakeObj = 0u, kFakeWidgetA = 0x800u, kFakeWidgetB = 0x2000u, kFakeSize = 0x3400u;

    struct StGuestCall
    {
        uint32_t handler = 0u;
        uint32_t slotA = 0u, slotB = 0u; // deslocamentos no objeto
        float sliderA = 0.0f, sliderB = 0.0f;
        uint32_t combo = 0u;
    };
    std::mutex g_guestMutex;
    std::deque<StGuestCall> g_guestCalls;
    std::atomic<uint8_t *> g_rdram{nullptr};
    uint32_t g_fakeBase = 0u;
    bool g_gameReady = false; // estrutura de opcoes existe (lido ao abrir o menu)
    PS2Runtime::RecompiledFunction g_origFramePump = nullptr;

    void stQueue(const StGuestCall &call)
    {
        std::lock_guard<std::mutex> lock(g_guestMutex);
        g_guestCalls.push_back(call);
    }

    // Inicio de FUN_0025F960 (todo quadro, inclusive nos menus e videos), na thread do jogo. A invocacao devolve o
    // controle a esta mesma entrada, que segue para a proxima chamada ou para o original.
    void stFramePump(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_rdram.store(rdram, std::memory_order_release);
        rdrTitleLogoPump(rdram);
        rdrMenuButtonsPump();
        rdrAchievementsPump(rdram);
        // O jogo comeca com Widescreen desligado no perfil: a proporcao do menu manda (bit 0 de +0x5C), via handler original
        static int s_widescreenTick = 0, s_widescreenTries = 0;
        if (++s_widescreenTick >= 60 && s_widescreenTries < 20)
        {
            s_widescreenTick = 0;
            uint32_t base = 0u, flags = 0u;
            std::memcpy(&base, rdram + kGameOptionsPtr, 4);
            if (base != 0u)
            {
                std::memcpy(&flags, rdram + (base & 0x1FFFFFFu) + 0x5Cu, 4);
                const bool want = g_st.aspect == 0;
                if (((flags & 1u) != 0u) != want)
                {
                    StGuestCall c;
                    c.handler = 0x0012BEF0u;
                    c.slotA = 0x44Cu;
                    c.combo = want ? 1u : 0u;
                    stQueue(c);
                    ++s_widescreenTries;
                    std::fprintf(stderr, "[rdr] widescreen do jogo -> %d\n", want ? 1 : 0);
                }
            }
        }
        StGuestCall call;
        {
            std::lock_guard<std::mutex> lock(g_guestMutex);
            if (!g_guestCalls.empty())
            {
                call = g_guestCalls.front();
                g_guestCalls.pop_front();
            }
        }
        if (call.handler == 0u)
        {
            g_origFramePump(rdram, ctx, runtime);
            return;
        }
        if (g_fakeBase == 0u)
            g_fakeBase = runtime->guestMalloc(kFakeSize, 16u);
        uint8_t *fake = rdram + (g_fakeBase & 0x1FFFFFFu);
        std::memset(fake, 0, kFakeSize);
        const uint32_t widgetA = g_fakeBase + kFakeWidgetA, widgetB = g_fakeBase + kFakeWidgetB;
        std::memcpy(fake + kFakeObj + call.slotA, &widgetA, 4);
        if (call.slotB != 0u)
            std::memcpy(fake + kFakeObj + call.slotB, &widgetB, 4);
        std::memcpy(fake + kFakeWidgetA + 0x160u, &call.sliderA, 4);
        std::memcpy(fake + kFakeWidgetB + 0x160u, &call.sliderB, 4);
        std::memcpy(fake + kFakeWidgetA + 0x1290u, &call.combo, 4);

        GuestInvocation invocation{};
        invocation.kind = GuestInvocationKind::HleCall;
        invocation.context = *ctx;
        invocation.context.pc = call.handler;
        SET_GPR_U32(&invocation.context, 4, g_fakeBase);
        SET_GPR_U32(&invocation.context, 29, 0u); // o escalonador usa a pilha de invocacao
        SET_GPR_U32(&invocation.context, 31, 0u);
        runtime->eeScheduler().invokeCurrent(std::move(invocation));
    }

    // Le os valores atuais do jogo para o menu
    void stReadGameOptions()
    {
        g_gameReady = false;
        const uint8_t *rdram = g_rdram.load(std::memory_order_acquire);
        if (!rdram)
            return;
        uint32_t base = 0u;
        std::memcpy(&base, rdram + kGameOptionsPtr, 4);
        if (base == 0u)
            return;
        const uint8_t *o = rdram + (base & 0x1FFFFFFu);
        float sfx, music, bright;
        int32_t mode, h, v;
        uint32_t flags;
        std::memcpy(&mode, o + 0x44, 4);
        std::memcpy(&sfx, o + 0x48, 4);
        std::memcpy(&music, o + 0x4C, 4);
        std::memcpy(&bright, o + 0x50, 4);
        std::memcpy(&h, o + 0x54, 4);
        std::memcpy(&v, o + 0x58, 4);
        std::memcpy(&flags, o + 0x5C, 4);
        g_st.audioMode = mode == 0x22 ? 1 : mode == 0x1F ? 2 : 0;
        g_st.sfxVolume = std::clamp(static_cast<int>(std::lround(sfx * 10.0f)), 0, 10);
        g_st.musicVolume = std::clamp(static_cast<int>(std::lround(music * 10.0f)), 0, 10);
        g_st.brightness = std::clamp(static_cast<int>(std::lround((bright - 0.5f) * 20.0f)), 0, 20);
        g_st.posX = std::clamp(static_cast<int>(std::lround((h + 128) / 255.0f * 50.0f)), 0, 50);
        g_st.posY = std::clamp(static_cast<int>(std::lround((v + 48) / 96.0f * 50.0f)), 0, 50);
        g_st.subtitles = (flags & 2u) != 0u ? 1 : 0;
        g_gameReady = true;
    }

    void stSendBrightness()
    {
        StGuestCall c;
        c.handler = 0x0012BDD8u;
        c.slotA = 0x450u;
        c.sliderA = g_st.brightness / 20.0f;
        stQueue(c);
    }
    void stSendPosition()
    {
        StGuestCall c;
        c.handler = 0x0012BE58u;
        c.slotA = 0x458u; // horizontal
        c.slotB = 0x454u; // vertical
        c.sliderA = g_st.posX / 50.0f;
        c.sliderB = g_st.posY / 50.0f;
        stQueue(c);
    }
    void stSendCombo(uint32_t handler, uint32_t slot, uint32_t value)
    {
        StGuestCall c;
        c.handler = handler;
        c.slotA = slot;
        c.combo = value;
        stQueue(c);
    }
    void stSendVolume(uint32_t handler, uint32_t slot, int value)
    {
        StGuestCall c;
        c.handler = handler;
        c.slotA = slot;
        c.sliderA = value / 10.0f;
        stQueue(c);
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
        bool game = false;       // valor do jogo: so existe depois que o jogo cria as opcoes
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
                         {{"Window Mode", "Fullscreen uses the whole monitor. Alt+Tab switches to other windows in both modes.", {"Fullscreen", "Window"}, &StSettings::windowMode},
                          {"Resolution", "Size of the game window. Fullscreen always uses the monitor resolution.",
                           std::vector<std::string>(std::begin(kStResolutions), std::end(kStResolutions)), &StSettings::resolution},
                          {"Aspect Ratio", "16:9 widescreen or the original 4:3 picture.", {"16:9", "4:3"}, &StSettings::aspect, 0, true},
                          {"Frame Rate Limit", "Limits how often the window is refreshed.", {"30", "60", "Unlimited"}, &StSettings::frameLimit},
                          {"VSync", "Synchronizes the picture with the monitor to avoid tearing.", offOn, &StSettings::vsync},
                          {"Brightness", "Picture brightness.", {}, &StSettings::brightness, 20},
                          {"Horizontal Position", "Moves the picture left or right.", {}, &StSettings::posX, 50},
                          {"Vertical Position", "Moves the picture up or down.", {}, &StSettings::posY, 50}}});
        pages.push_back({"Graphics Options",
                         {{"Quality Preset", "Sets all graphics options at once (Low, Medium, High, Ultra).", {}, nullptr, 0, false, true},
                          {"Supersampling", "Renders the game at a higher internal resolution. 16x is the sharpest and the slowest.",
                           {"Off", "4x", "16x"}, &StSettings::ssaa, 0, true},
                          {"HD Textures", "Samples textures at the internal resolution (needs supersampling).", offOn, &StSettings::hdTextures, 0, true},
                          {"High Resolution Output", "Shows the internal resolution instead of the original 640 x 448.", offOn, &StSettings::hiresOutput, 0, true},
                          {"Image Filter", "Smooth or sharp scaling of the picture to the window.", {"Smooth", "Sharp"}, &StSettings::filter},
                          {"Show Frame Rate", "Frame rate counter in the corner of the screen.", {}, nullptr, 0, false, true}}});
        pages.push_back({"Audio Options",
                         {{"Master Volume", "Volume of all game audio.", {}, &StSettings::volume, 10},
                          {"Music Volume", "Volume of the music.", {}, &StSettings::musicVolume, 10},
                          {"SFX Volume", "Volume of the sound effects and voices.", {}, &StSettings::sfxVolume, 10},
                          {"Audio Mode", "Stereo, surround or mono output.", {"Stereo", "Surround", "Mono"}, &StSettings::audioMode},
                          {"Output Device", "Speakers or headphones used by the game.", {}, nullptr, 0, false, true}}});
        pages.push_back({"Controls Options",
                         {{"Mouse Sensitivity", "How fast the mouse turns the camera and aims.", {}, &StSettings::mouseSens, 20},
                          {"Invert Mouse Y", "Moving the mouse up looks down.", offOn, &StSettings::invertY},
                          {"Walk Speed", "Stick tilt used when walking with the keyboard. Hold Run to run.", {}, &StSettings::walkSpeed, 9},
                          {"Key Bindings", "Change the keyboard and mouse keys (for now in Documents\\Red Dead Revolver\\controls.ini).",
                           {}, nullptr, 0, false, true},
                          {"Toggle Run / Aim", "Press once instead of holding.", {}, nullptr, 0, false, true}}});
        pages.push_back({"Interface Options",
                         {{"Subtitles", "Shows the dialogue as text.", offOn, &StSettings::subtitles},
                          {"HUD Size", "Makes the game menus and HUD smaller on large screens.", {}, nullptr, 0, false, true},
                          {"Language", "Language of the game text and menus. Files in the text folder add languages. Open menus change when reopened.",
                           [] {
                               std::vector<std::string> names;
                               for (int i = 0; i < rdrTextLanguageCount(); ++i)
                                   names.push_back(rdrTextLanguageName(i));
                               return names;
                           }(),
                           &StSettings::language}}});
        pages[4].items[2].onChange = [] { rdrTextSetLanguage(g_st.language); };
        pages[3].items[2].onChange = [] {
            rdrControlsSetWalkTilt(stWalkTilt(g_st.walkSpeed));
            rdrControlsApply();
        };
        auto game = [&](int page, int item, std::function<void()> send) {
            pages[page].items[item].game = true;
            pages[page].items[item].onChange = std::move(send);
        };
        // Aspect Ratio continua valendo na proxima abertura (janela e -widescreen); o jogo troca na hora
        pages[0].items[2].onChange = [] {
            if (g_gameReady)
                stSendCombo(0x0012BEF0u, 0x44Cu, g_st.aspect == 0 ? 1u : 0u);
        };
        game(0, 5, stSendBrightness);
        game(0, 6, stSendPosition);
        game(0, 7, stSendPosition);
        game(2, 1, [] { stSendVolume(0x001314C0u, 0x454u, g_st.musicVolume); });
        game(2, 2, [] { stSendVolume(0x00131460u, 0x450u, g_st.sfxVolume); });
        game(2, 3, [] { stSendCombo(0x001313B0u, 0x458u, static_cast<uint32_t>(g_st.audioMode)); });
        game(4, 0, [] { stSendCombo(0x00131418u, 0x45Cu, static_cast<uint32_t>(g_st.subtitles)); });
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
        const int code = rdrControlsMenuKeyForPadBit(padBit);
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
        const bool unavailable = item.game && !g_gameReady;
        const Color c = item.soon || unavailable ? kStDim : sel ? kStWhite : kStGray;
        if (item.soon || unavailable)
        {
            stText(1, item.soon ? "Coming soon" : "Not available yet", (lx + rx) / 2.0f, baseline, cap, kStDim, true);
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
        const std::string accept = rdrKeyShortLabel(rdrControlsMenuKeyForPadBit(14));
        const std::string back = rdrKeyShortLabel(rdrControlsMenuKeyForPadBit(13));
        if (g_menu.page < 0)
        {
            stText(0, "PC Settings", v.X(0.5f), v.Y(0.20f), v.h * 0.07f, kStRed, true);
            for (size_t i = 0; i < pages.size(); ++i)
            {
                const std::string name = pages[i].title.substr(0, pages[i].title.find(' '));
                stText(0, name, v.X(0.5f), v.Y(0.34f + 0.07f * i), v.h * 0.045f,
                       static_cast<int>(i) == g_menu.selected ? kStWhite : kStGray, true);
            }
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
            return false;
        }
        // Nos menus o triangulo abre as opcoes e o circulo volta (FUNCOES.md #101)
        const bool optionsButton = g_mainMenu && (stMappedPressed(12) || stPad(GAMEPAD_BUTTON_RIGHT_FACE_UP));
        if (!g_menu.open)
        {
            if (!openKey && !optionsButton)
                return false;
            g_menu = {};
            g_menu.open = true;
            g_menu.openedAt = GetTime();
            stReadGameOptions();
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
        const bool back = IsKeyPressed(KEY_ESCAPE) || stMappedPressed(13) || stPad(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || openKey;

        if (g_menu.page < 0)
        {
            const int n = static_cast<int>(pages.size());
            if (up)
                g_menu.selected = (g_menu.selected + n - 1) % n;
            if (down)
                g_menu.selected = (g_menu.selected + 1) % n;
            if (accept)
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
            if ((left || right) && !item.soon && item.field && !(item.game && !g_gameReady))
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
                bool gameChanged = false;
                for (const StItem &it : page.items)
                {
                    if (it.restart && it.field && g_st.*it.field != g_menu.backup.*it.field)
                        g_menu.restartPending = true;
                    gameChanged = gameChanged || (it.game && g_st.*it.field != g_menu.backup.*it.field);
                }
                if (uint8_t *rdram = g_rdram.load(std::memory_order_acquire); rdram && gameChanged)
                    rdram[kGameSaveDirty] = 1u;
                stSave();
                rdrControlsSave();
                g_menu.page = -1;
                g_menu.selected = g_menu.pageSelected;
            }
            else if (back)
            {
                const StSettings edited = g_st;
                g_st = g_menu.backup;
                stApplyLive();
                for (const StItem &it : page.items)
                    if (it.onChange && it.field && edited.*it.field != g_st.*it.field)
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
    rdrSetEnvDefault("PS2X_FULLSCREEN", "0"); // a janela sem borda vem de rdrWin32ApplyWindowMode no primeiro quadro
    rdrSetEnvDefault("PS2X_ASPECT", g_st.aspect == 0 ? "16:9" : "4:3");
    if (g_st.aspect == 1)
        rdrSetEnvDefault("RDR_PARAMS", "nowidescreen");
    rdrSetEnvDefault("PS2X_GS_SSAA", g_st.ssaa == 0 ? "1" : g_st.ssaa == 1 ? "4" : "16");
    rdrSetEnvDefault("PS2X_GS_SS_TEXTURES", g_st.hdTextures ? "1" : "0");
    rdrSetEnvDefault("PS2X_GS_HIRES", g_st.hiresOutput ? "1" : "0");
}

// Com a janela criada (applyRdrOverrides): registra o menu; o resto vale no primeiro quadro
void rdrSettingsInstall(PS2Runtime &runtime)
{
    stLoad();
    stResolveLanguage();
    rdrTextSetLanguage(g_st.language);
    g_origFramePump = runtime.lookupFunction(0x0025F960u);
    runtime.replaceFunction(0x0025F960u, stFramePump);
    g_st.walkSpeed = std::clamp(static_cast<int>((rdrControlsWalkTilt() - 0.40f) / 0.04f + 0.5f), 0, 9);
    ps2SetOverlayHandler([](const float game[4]) {
        static bool s_applied = false;
        if (!s_applied)
        {
            s_applied = true;
            stApplyLive(); // primeiro quadro: a janela ja existe
        }
        rdrFmvOverlay();
        rdrAchievementsOverlay();
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
