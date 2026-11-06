// Lancador da versao de distribuicao (v0.1.0): tela no estilo do Red Dead Revolver onde o jogador escolhe a
// propria ISO (SLUS-20500). Na 1a vez instala os arquivos em pastas por tipo (rdr_install.cpp, docs/LAYOUT.md);
// depois "Play" inicia o jogo em tela cheia na resolucao nativa com a maior qualidade. Aberto com o ELF em argv
// (scripts de desenvolvimento), o lancador nao aparece.
#include "ps2_runtime.h"
#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using RdrInstallProgress = std::function<void(uint64_t done, uint64_t total, const std::string &label)>;
bool rdrIsInstalled(const fs::path &root);
void rdrAchievementsScreen(const fs::path &root); // rdr_achievements.cpp
std::string rdrInstallFromIso(const fs::path &isoPath, const fs::path &root, const RdrInstallProgress &progress);
fs::path rdrExeDirectory();
fs::path rdrExePath();
fs::path rdrPickIsoFile();
void rdrNativeScreenSize(int &w, int &h);
void rdrSetEnvDefault(const char *name, const std::string &value);
void rdrRedirectLogIfDetached(const fs::path &logFile);
fs::path rdrDocumentsDirectory();
fs::path rdrStartMenuDirectory();
bool rdrCreateShortcut(const fs::path &lnk, const fs::path &target, const std::wstring &args, const std::wstring &description);
bool rdrConfirm(const std::wstring &text, const std::wstring &title);
bool rdrScheduleRemoval(const fs::path &root, const std::vector<std::wstring> &items);
bool rdrSteamRunning();
void rdrSettingsApplyAtBoot();
std::string rdrSteamShortcut(const fs::path &root, bool add);
bool rdrSteamAddWhenClosed(const fs::path &exe);
void rdrSteamWaitForExit();

namespace
{
    constexpr const char *kVersion = "v0.1.0";
    const Color kInk{235, 220, 190, 255};     // osso
    const Color kRed{150, 22, 10, 255};        // vermelho do titulo
    const Color kDim{150, 130, 105, 255};
    const Color kSky0{18, 6, 4, 255}, kSky1{120, 34, 10, 255}, kSun{232, 108, 30, 255};

    struct Install
    {
        std::thread worker;
        std::atomic<bool> running{false}, finished{false};
        std::atomic<uint64_t> done{0}, total{1};
        std::mutex m;
        std::string label, error;
    };

    // Atalhos no menu Iniciar (pasta "Red Dead Revolver"), como o GTA Vice City: jogo, lancador e desinstalador
    void createStartMenuShortcuts(const fs::path &root)
    {
        const fs::path menu = rdrStartMenuDirectory();
        if (menu.empty())
            return;
        rdrCreateShortcut(menu / L"Red Dead Revolver.lnk", root / L"RedDeadRevolver.exe", L"", L"Play Red Dead Revolver");
        rdrCreateShortcut(menu / L"RDR Launcher.lnk", root / L"RDR Launcher.exe", L"", L"Install from your ROM or start the game");
        rdrCreateShortcut(menu / L"Uninstall Red Dead Revolver.lnk", root / L"RDR Launcher.exe", L"--uninstall",
                          L"Remove Red Dead Revolver from this computer");
    }

    // Desinstalador: remove so o que a instalacao e o pacote criaram (a pasta escolhida pode ter outras coisas),
    // os atalhos e, se ficar vazia, a pasta. Os saves em Documentos ficam (como no GTA Vice City).
    bool uninstall(const fs::path &root, bool askFirst = true)
    {
        if (askFirst && !rdrConfirm(L"Remove Red Dead Revolver from this computer?\n\nThe game files, the launcher and the Start menu "
                        L"shortcuts will be deleted. Your saves in Documents\\Red Dead Revolver are kept.",
                        L"Uninstall Red Dead Revolver"))
            return false;
        std::error_code ec;
        fs::remove_all(rdrStartMenuDirectory(), ec);
        // Atalho nao-Steam e artes (so fecha o Steam se o jogo estiver la)
        rdrSteamShortcut(root, false);
        std::vector<std::wstring> items = {L"movies", L"audio", L"data", L"frontend", L"levels", L"extras", L"system",
                                           L"launcher", L"steam", L"mc0", L"mc1", L"README.txt", L"RDR Launcher.exe",
                                           L"RedDeadRevolver.exe"};
        for (const auto &e : fs::directory_iterator(root, ec))
            if (e.path().extension() == ".dll")
                items.push_back(e.path().filename().wstring());
        return rdrScheduleRemoval(root, items);
    }

    void drawScene(int w, int h, float t)
    {
        const int horizon = static_cast<int>(h * 0.70f);
        DrawRectangleGradientV(0, 0, w, horizon, kSky0, kSky1);
        DrawCircle(static_cast<int>(w * 0.70f), horizon, h * 0.16f, kSun);
        DrawCircle(static_cast<int>(w * 0.70f), horizon, h * 0.16f + 6.0f + 2.0f * std::sin(t * 1.5f), Fade(kSun, 0.25f));
        DrawRectangle(0, horizon, w, h - horizon, Color{14, 6, 4, 255});
        // mesas e cactos em silhueta
        const Color sil{10, 4, 3, 255};
        DrawRectangle(static_cast<int>(w * 0.80f), horizon - h / 14, w / 7, h / 14, sil);
        auto cactus = [&](float x, float s) {
            const float base = static_cast<float>(horizon);
            DrawRectangleRounded({x, base - 90 * s, 16 * s, 90 * s}, 0.5f, 6, sil);
            DrawRectangleRounded({x - 20 * s, base - 62 * s, 12 * s, 30 * s}, 0.5f, 6, sil);
            DrawRectangle(static_cast<int>(x - 20 * s), static_cast<int>(base - 40 * s), static_cast<int>(28 * s), static_cast<int>(8 * s), sil);
            DrawRectangleRounded({x + 24 * s, base - 70 * s, 12 * s, 34 * s}, 0.5f, 6, sil);
            DrawRectangle(static_cast<int>(x + 8 * s), static_cast<int>(base - 44 * s), static_cast<int>(28 * s), static_cast<int>(8 * s), sil);
        };
        cactus(w * 0.56f, h / 540.0f);
        cactus(w * 0.92f, h / 720.0f);
        // vinheta
        DrawRectangleGradientH(0, 0, w / 5, h, Fade(BLACK, 0.55f), Fade(BLACK, 0.0f));
        DrawRectangleGradientH(w - w / 5, 0, w / 5, h, Fade(BLACK, 0.0f), Fade(BLACK, 0.55f));
    }

    void text(const Font &f, const std::string &s, float x, float y, float size, Color c, float spacing = 1.0f)
    {
        DrawTextEx(f, s.c_str(), {x, y}, size, spacing, c);
    }

    float width(const Font &f, const std::string &s, float size, float spacing = 1.0f)
    {
        return MeasureTextEx(f, s.c_str(), size, spacing).x;
    }

    void setPlayDefaults(const fs::path &root, fs::path &elfOut, std::string &titleOut);

    int runLauncher(fs::path &elfOut, std::string &titleOut)
    {
        const fs::path root = rdrExeDirectory();
        // Sem SetConfigFlags: as flags do raylib continuam valendo no InitWindow do jogo depois (HIGHDPI/MSAA
        // mudavam a janela do jogo). Tamanho em pixels reais: ~45% da largura do monitor, 16:9.
        int mw = 1920, mh = 1080;
        rdrNativeScreenSize(mw, mh);
        const int lw = std::max(960, mw * 45 / 100);
        InitWindow(lw, lw * 9 / 16, "Red Dead Revolver");
        SetExitKey(KEY_NULL);
        SetTargetFPS(60);
        const fs::path fontPath = root / "launcher" / "Rye-Regular.ttf";
        Font big = GetFontDefault(), small = GetFontDefault();
        if (fs::exists(fontPath))
        {
            big = LoadFontEx(fontPath.string().c_str(), 112, nullptr, 0);
            small = LoadFontEx(fontPath.string().c_str(), 40, nullptr, 0);
            SetTextureFilter(big.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(small.texture, TEXTURE_FILTER_BILINEAR);
        }

        Install inst;
        bool installed = rdrIsInstalled(root);
        std::string status = installed ? "Ready to ride." : "Select your Red Dead Revolver (USA) disc image to install the game.";
        int selected = installed ? 0 : 1;
        constexpr int kItems = 6;
        std::thread steamWorker;
        std::atomic<bool> steamBusy{false}, steamDone{false};
        std::string steamResult;
        int result = 0;
        Vector2 lastMouse = GetMousePosition();

        while (result == 0)
        {
            if (WindowShouldClose())
            {
                result = -1;
                break;
            }
            const int w = GetScreenWidth(), h = GetScreenHeight();
            const float s = h / 576.0f, t = static_cast<float>(GetTime());
            // ADD TO STEAM so depois da instalacao pela ISO (antes o jogo ainda nao existe na pasta)
            const char *labels[kItems] = {"PLAY", "SELECT ROM", "ACHIEVEMENTS", "ADD TO STEAM", "UNINSTALL", "QUIT"};
            const bool idle = !inst.running && !steamBusy;
            const bool enabled[kItems] = {installed && idle, idle, idle, installed && idle, installed && idle, idle};

            if (inst.finished.exchange(false))
            {
                if (inst.worker.joinable())
                    inst.worker.join();
                std::lock_guard<std::mutex> lock(inst.m);
                installed = inst.error.empty() && rdrIsInstalled(root);
                if (installed)
                    createStartMenuShortcuts(root);
                status = installed ? "Installation complete. Ready to ride." : inst.error;
                if (installed)
                    selected = 0;
            }
            if (steamDone.exchange(false))
            {
                if (steamWorker.joinable())
                    steamWorker.join();
                std::lock_guard<std::mutex> lock(inst.m);
                status = steamResult.empty() ? "Added to Steam (non-Steam game, with artwork)." : steamResult;
                steamBusy = false;
            }

            // entrada: teclado, controle e mouse
            const bool up = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
            const bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
            bool activate = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            if (IsKeyPressed(KEY_ESCAPE) && !inst.running && !steamBusy)
                result = -1;
            for (int step = 0; step < kItems && (up || down); ++step)
            {
                selected = (selected + (up ? kItems - 1 : 1)) % kItems;
                if (enabled[selected])
                    break;
            }
            const float menuX = 70 * s, menuY = 268 * s, itemH = 41 * s, itemSize = 32 * s;
            const Vector2 mouse = GetMousePosition();
            const bool mouseMoved = mouse.x != lastMouse.x || mouse.y != lastMouse.y;
            lastMouse = mouse;
            for (int i = 0; i < kItems; ++i)
            {
                const Rectangle r{menuX - 10 * s, menuY + i * itemH - 6 * s, width(small, labels[i], itemSize) + 60 * s, itemH};
                if (enabled[i] && CheckCollisionPointRec(mouse, r))
                {
                    if (mouseMoved)
                        selected = i;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                        selected = i, activate = true;
                }
            }
            if (activate && enabled[selected])
            {
                if (selected == 0)
                    result = 1;
                else if (selected == 5)
                    result = -1;
                else if (selected == 2)
                    rdrAchievementsScreen(root);
                else if (selected == 3)
                {
                    // Steam aberto: nao fecha; um processo oculto grava o atalho quando o Steam fechar
                    if (rdrSteamRunning())
                        status = rdrSteamAddWhenClosed(rdrExePath())
                                     ? "Steam is open: the game will be added when you close Steam."
                                     : "Could not add to Steam. Close Steam and try again.";
                    else
                    {
                        steamBusy = true;
                        status = "Adding to Steam...";
                        steamWorker = std::thread([&, root] {
                            const std::string r = rdrSteamShortcut(root, true);
                            {
                                std::lock_guard<std::mutex> lock(inst.m);
                                steamResult = r;
                            }
                            steamDone = true;
                        });
                    }
                }
                else if (selected == 4)
                {
                    if (uninstall(root))
                        result = -1;
                }
                else
                {
                    const fs::path iso = rdrPickIsoFile();
                    if (!iso.empty())
                    {
                        inst.running = true;
                        inst.done = 0;
                        status = "Installing...";
                        inst.worker = std::thread([&inst, iso, root] {
                            const std::string err = rdrInstallFromIso(iso, root, [&inst](uint64_t d, uint64_t tot, const std::string &l) {
                                inst.done = d;
                                inst.total = tot ? tot : 1;
                                std::lock_guard<std::mutex> lock(inst.m);
                                inst.label = l;
                            });
                            {
                                std::lock_guard<std::mutex> lock(inst.m);
                                inst.error = err;
                            }
                            inst.running = false;
                            inst.finished = true;
                        });
                    }
                }
            }

            BeginDrawing();
            ClearBackground(BLACK);
            drawScene(w, h, t);
            // titulo
            const float titleSize = 92 * s;
            text(big, "RED DEAD", 64 * s + 4 * s, 60 * s + 4 * s, titleSize, Fade(BLACK, 0.6f), 4 * s);
            text(big, "RED DEAD", 64 * s, 60 * s, titleSize, kRed, 4 * s);
            text(big, "REVOLVER", 64 * s + 4 * s, 60 * s + titleSize * 0.92f + 4 * s, titleSize, Fade(BLACK, 0.6f), 4 * s);
            text(big, "REVOLVER", 64 * s, 60 * s + titleSize * 0.92f, titleSize, kInk, 4 * s);
            text(small, std::string("PC  ") + kVersion, 70 * s, 60 * s + titleSize * 1.95f, 20 * s, kDim, 2 * s);

            for (int i = 0; i < kItems; ++i)
            {
                const float y = menuY + i * itemH;
                const bool sel = i == selected && enabled[i];
                Color c = enabled[i] ? (sel ? kInk : Fade(kInk, 0.6f)) : Fade(kDim, 0.35f);
                if (sel)
                {
                    const float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
                    text(small, ">", menuX - 36 * s + pulse * 6 * s, y, itemSize, kRed);
                    const float lw = width(small, labels[i], itemSize, 2 * s);
                    DrawRectangle(static_cast<int>(menuX), static_cast<int>(y + itemSize + 2 * s), static_cast<int>(lw),
                                  static_cast<int>(3 * s), Fade(kRed, 0.6f + 0.4f * pulse));
                }
                text(small, labels[i], menuX, y, itemSize, c, 2 * s);
            }

            // estado / progresso
            const float statusY = h - 70 * s;
            if (inst.running)
            {
                const float frac = static_cast<float>(static_cast<double>(inst.done) / static_cast<double>(inst.total.load()));
                const float bw = w * 0.5f;
                DrawRectangle(static_cast<int>(70 * s), static_cast<int>(statusY), static_cast<int>(bw), static_cast<int>(10 * s), Fade(BLACK, 0.6f));
                DrawRectangle(static_cast<int>(70 * s), static_cast<int>(statusY), static_cast<int>(bw * frac), static_cast<int>(10 * s), kRed);
                std::string label;
                {
                    std::lock_guard<std::mutex> lock(inst.m);
                    label = inst.label;
                }
                text(small, "Installing  " + std::to_string(static_cast<int>(frac * 100.0f)) + "%   " + label, 70 * s,
                     statusY + 16 * s, 18 * s, kDim, 1.5f * s);
            }
            else
                text(small, status, 70 * s, statusY + 8 * s, 20 * s, installed ? kInk : kDim, 1.5f * s);
            text(small, "Uses your own copy of the game.  Saves: Documents\\Red Dead Revolver", 70 * s, h - 30 * s, 15 * s,
                 Fade(kDim, 0.8f), 1.2f * s);
            EndDrawing();
        }

        if (inst.worker.joinable())
            inst.worker.join();
        if (steamWorker.joinable())
            steamWorker.join();
        if (big.texture.id != GetFontDefault().texture.id)
            UnloadFont(big), UnloadFont(small);
        CloseWindow();
        if (result != 1)
            return -1;
        setPlayDefaults(root, elfOut, titleOut);
        return 1;
    }

    void setPlayDefaults(const fs::path &root, fs::path &elfOut, std::string &titleOut)
    {
        // Tela cheia na resolucao do monitor, 16:9. Antes sem SSAA (o loading parecia travado, mas a causa eram as flags
        // do lancador vazando para a janela do jogo, FUNCOES.md #89).
        int sw = 1920, sh = 1080;
        rdrNativeScreenSize(sw, sh);
        rdrSettingsApplyAtBoot(); // settings.ini (menu de opcoes, F1) antes dos padroes abaixo
        rdrSetEnvDefault("PS2X_DIST", "1");
        rdrSetEnvDefault("PS2X_FULLSCREEN", "1");
        rdrSetEnvDefault("PS2X_WINDOW_SIZE", std::to_string(sw) + "x" + std::to_string(sh));
        rdrSetEnvDefault("PS2X_ASPECT", "16:9");
        // Qualidade (2026-11-01): SSAA 4 (2x por eixo) com texturas supersampled e saida na resolucao interna. SSAA 16
        // deixou o tutorial em ~11 fps numa RTX 3060 em 4K (antes ~26). PS2X_GS_SSAA=1 desliga, 16 = maximo.
        rdrSetEnvDefault("PS2X_GS_SSAA", "4");
        elfOut = root / "system" / "SLUS_205.00";
        titleOut = "Red Dead Revolver";
        // Saves em Documentos desde o inicio: o padrao do runtime (<pasta atual>\mc0) criava mc0/mc1 vazias na
        // pasta do jogo antes do override definir o caminho
        if (const fs::path docs = rdrDocumentsDirectory(); !docs.empty())
        {
            PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
            paths.mcRoot = docs / "Red Dead Revolver";
            PS2Runtime::setIoPaths(paths);
        }
    }

    // Linha de comando: "--install <iso>" instala sem interface; "--play" pula o lancador (atalhos, testes).
    // Qualquer outro argumento e o ELF dos scripts de desenvolvimento (fluxo normal do runtime).
    int rdrPreBoot(int argc, char *argv[], fs::path &elfOut, std::string &titleOut)
    {
        const fs::path root = rdrExeDirectory();
        // Steam: antes do log do jogo (o processo que espera o Steam fechar nao pode tomar o last_run.log)
        if (argc >= 2 && (std::string(argv[1]) == "--steam-add" || std::string(argv[1]) == "--steam-add-later" ||
                          std::string(argv[1]) == "--steam-remove"))
        {
            const bool add = std::string(argv[1]) != "--steam-remove";
            if (add && !rdrIsInstalled(root))
                std::exit(2);
            if (std::string(argv[1]) == "--steam-add-later")
                rdrSteamWaitForExit();
            const std::string err = rdrSteamShortcut(root, add);
            std::fprintf(stderr, "[steam] %s\n", err.empty() ? "ok" : err.c_str());
            std::exit(err.empty() ? 0 : 1);
        }
        if (const fs::path docs = rdrDocumentsDirectory(); !docs.empty())
            rdrRedirectLogIfDetached(docs / "Red Dead Revolver" / "last_run.log");
        if (argc >= 3 && std::string(argv[1]) == "--install")
        {
            int last = -1;
            const std::string err = rdrInstallFromIso(fs::u8path(argv[2]), root, [&last](uint64_t d, uint64_t t, const std::string &) {
                const int pct = t ? static_cast<int>(d * 100 / t) : 0;
                if (pct / 10 != last / 10)
                    std::fprintf(stderr, "[install] %d%%\n", pct);
                last = pct;
            });
            std::fprintf(stderr, "[install] %s\n", err.empty() ? "ok" : err.c_str());
            if (err.empty())
                createStartMenuShortcuts(root);
            std::exit(err.empty() ? 0 : 1);
        }
        if (argc >= 2 && std::string(argv[1]) == "--uninstall")
        {
            // "--uninstall --yes": sem a pergunta (scripts)
            uninstall(root, !(argc >= 3 && std::string(argv[2]) == "--yes"));
            return -1;
        }
        if (argc >= 2 && std::string(argv[1]) == "--play")
        {
            if (!rdrIsInstalled(root))
                return runLauncher(elfOut, titleOut);
            setPlayDefaults(root, elfOut, titleOut);
            return 1;
        }
        if (argc >= 2)
            return 0;
        // Como o GTA Vice City: o exe do jogo abre o jogo direto; "RDR Launcher.exe" (mesmo binario, outro nome)
        // abre o lancador. Sem instalacao, o jogo tambem abre o lancador para escolher a ROM.
        std::string stem = rdrExePath().stem().string();
        for (char &c : stem)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (stem.find("launcher") == std::string::npos && rdrIsInstalled(root))
        {
            setPlayDefaults(root, elfOut, titleOut);
            return 1;
        }
        return runLauncher(elfOut, titleOut);
    }

    struct RegisterLauncher
    {
        RegisterLauncher() { ps2SetPreBootHook(rdrPreBoot); }
    } g_registerLauncher;
}
