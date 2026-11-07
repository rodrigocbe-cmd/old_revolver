// Controles de teclado e mouse do Red Dead Revolver (docs/CONTROLES.md, FUNCOES.md #93): acoes do jogo com
// teclas padrao no estilo do RDR2 para PC, lidas e gravadas em Documentos\Red Dead Revolver\controls.ini.
// WASD anda (analogico pouco inclinado, walk_tilt) e com Shift corre (todo inclinado).
// RDR_CONTROLS=legacy mantem o mapa padrao do runtime (scripts de teste em tools/ mandam as teclas antigas).
#include "ps2_runtime.h"
#include "raylib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

fs::path rdrDocumentsDirectory();
void rdrKeyIconsInvalidate(); // rdr_keyicons.cpp

namespace
{
    // Bits da mascara de botoes do PS2 (indice em Ps2PadKeyMap::buttons)
    enum PadBit
    {
        kSelect = 0, kL3 = 1, kR3 = 2, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7,
        kL2 = 8, kR2 = 9, kL1 = 10, kR1 = 11, kTriangle = 12, kCircle = 13, kCross = 14, kSquare = 15,
        kMoveUp = 100, kMoveDown, kMoveLeft, kMoveRight, kRun
    };

    struct CtlAction
    {
        const char *id;    // chave no controls.ini
        const char *label; // nome na tela de ajuste
        int target;        // PadBit
        int defaults[Ps2PadKeyMap::kSlots];
    };

    // Ordem = ordem da tela de ajuste
    const CtlAction kActions[] = {
        {"move_forward", "Move Forward", kMoveUp, {KEY_W}},
        {"move_back", "Move Back", kMoveDown, {KEY_S}},
        {"move_left", "Move Left", kMoveLeft, {KEY_A}},
        {"move_right", "Move Right", kMoveRight, {KEY_D}},
        {"run", "Run", kRun, {KEY_LEFT_SHIFT}},
        {"aim", "Draw / Aim", kL1, {PS2_INPUT_MOUSE_RIGHT}},
        {"fire", "Fire", kR1, {PS2_INPUT_MOUSE_LEFT}},
        {"dead_eye", "Dead Eye", kR2, {KEY_CAPS_LOCK, PS2_INPUT_MOUSE_MIDDLE}},
        {"switch_weapon", "Switch Weapon", kL2, {KEY_TAB}},
        {"next_weapon", "Next Weapon", kRight, {PS2_INPUT_WHEEL_UP, KEY_RIGHT}},
        {"prev_weapon", "Previous Weapon", kLeft, {PS2_INPUT_WHEEL_DOWN, KEY_LEFT}},
        {"menu_up", "Menu Up", kUp, {KEY_UP}},
        {"menu_down", "Menu Down", kDown, {KEY_DOWN}},
        {"jump", "Jump / Dodge", kCross, {KEY_SPACE}},
        {"use", "Use / Cover", kSquare, {KEY_E, KEY_Q}},
        {"melee", "Melee", kCircle, {KEY_F}},
        {"cancel", "Cancel / Skip", kTriangle, {KEY_BACKSPACE}},
        {"crouch", "Crouch", kL3, {KEY_LEFT_CONTROL, KEY_C}},
        {"zoom", "Zoom", kR3, {KEY_Z}},
        {"pause", "Pause", kStart, {KEY_ESCAPE, KEY_ENTER}},
        {"select", "Select", kSelect, {KEY_M}},
    };
    constexpr int kActionCount = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));
    // Medido (tools/walk_test.ps1, 2026-11-01): ate 0,30 parado (zona morta), 0,40-0,76 anda sempre na mesma
    // velocidade (~1 u/s), 0,78+ corre (~4,4 u/s). Padrao: o maior andar com margem antes da corrida.
    constexpr float kDefaultWalkTilt = 0.74f;

    struct CtlKeyName
    {
        int code;
        const char *name;
    };

    const std::vector<CtlKeyName> &ctlKeyNames()
    {
        static const std::vector<CtlKeyName> names = [] {
            std::vector<CtlKeyName> v = {
                {PS2_INPUT_MOUSE_LEFT, "MOUSE1"}, {PS2_INPUT_MOUSE_RIGHT, "MOUSE2"}, {PS2_INPUT_MOUSE_MIDDLE, "MOUSE3"},
                {PS2_INPUT_MOUSE_SIDE, "MOUSE4"}, {PS2_INPUT_MOUSE_EXTRA, "MOUSE5"}, {PS2_INPUT_WHEEL_UP, "WHEELUP"},
                {PS2_INPUT_WHEEL_DOWN, "WHEELDOWN"}, {KEY_SPACE, "SPACE"}, {KEY_ENTER, "ENTER"},
                {KEY_ESCAPE, "ESCAPE"}, {KEY_BACKSPACE, "BACKSPACE"}, {KEY_TAB, "TAB"}, {KEY_CAPS_LOCK, "CAPSLOCK"},
                {KEY_LEFT_SHIFT, "LSHIFT"}, {KEY_RIGHT_SHIFT, "RSHIFT"}, {KEY_LEFT_CONTROL, "LCTRL"},
                {KEY_RIGHT_CONTROL, "RCTRL"}, {KEY_LEFT_ALT, "LALT"}, {KEY_RIGHT_ALT, "RALT"}, {KEY_UP, "UP"},
                {KEY_DOWN, "DOWN"}, {KEY_LEFT, "LEFT"}, {KEY_RIGHT, "RIGHT"}, {KEY_INSERT, "INSERT"},
                {KEY_DELETE, "DELETE"}, {KEY_HOME, "HOME"}, {KEY_END, "END"}, {KEY_PAGE_UP, "PAGEUP"},
                {KEY_PAGE_DOWN, "PAGEDOWN"}, {KEY_MINUS, "MINUS"}, {KEY_EQUAL, "EQUAL"},
                {KEY_LEFT_BRACKET, "LBRACKET"}, {KEY_RIGHT_BRACKET, "RBRACKET"}, {KEY_SEMICOLON, "SEMICOLON"},
                {KEY_APOSTROPHE, "APOSTROPHE"}, {KEY_COMMA, "COMMA"}, {KEY_PERIOD, "PERIOD"}, {KEY_SLASH, "SLASH"},
                {KEY_BACKSLASH, "BACKSLASH"}, {KEY_GRAVE, "GRAVE"}};
            for (int c = KEY_A; c <= KEY_Z; ++c)
                v.push_back(CtlKeyName{c, nullptr});
            for (int c = KEY_ZERO; c <= KEY_NINE; ++c)
                v.push_back(CtlKeyName{c, nullptr});
            for (int i = 0; i < 12; ++i)
                v.push_back(CtlKeyName{KEY_F1 + i, nullptr});
            for (int i = 0; i < 10; ++i)
                v.push_back(CtlKeyName{KEY_KP_0 + i, nullptr});
            return v;
        }();
        return names;
    }

    std::string ctlUpper(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    }

    std::string ctlTrim(const std::string &s)
    {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return {};
        return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
    }

    struct CtlBindings
    {
        int inputs[kActionCount][Ps2PadKeyMap::kSlots] = {};
        float walkTilt = kDefaultWalkTilt;
    } g_bindings;
}

// Nome da tecla no controls.ini (e base dos rotulos dos icones)
std::string rdrKeyName(int code)
{
    if (code >= KEY_A && code <= KEY_Z)
        return std::string(1, static_cast<char>('A' + (code - KEY_A)));
    if (code >= KEY_ZERO && code <= KEY_NINE)
        return std::string(1, static_cast<char>('0' + (code - KEY_ZERO)));
    if (code >= KEY_F1 && code <= KEY_F12)
        return "F" + std::to_string(code - KEY_F1 + 1);
    if (code >= KEY_KP_0 && code <= KEY_KP_9)
        return "NUMPAD" + std::to_string(code - KEY_KP_0);
    for (const CtlKeyName &k : ctlKeyNames())
        if (k.code == code && k.name)
            return k.name;
    return {};
}

int rdrKeyFromName(const std::string &nameIn)
{
    const std::string name = ctlUpper(ctlTrim(nameIn));
    if (name.empty() || name == "NONE")
        return PS2_INPUT_NONE;
    for (const CtlKeyName &k : ctlKeyNames())
        if (rdrKeyName(k.code) == name)
            return k.code;
    return PS2_INPUT_NONE;
}

fs::path rdrControlsFile()
{
    if (const char *env = std::getenv("RDR_CONTROLS_INI"); env && env[0])
        return fs::u8path(env);
    const fs::path docs = rdrDocumentsDirectory();
    return docs.empty() ? fs::path("controls.ini") : docs / "Red Dead Revolver" / "controls.ini";
}

// Passa as teclas atuais para o runtime
// Menu inicial (FUNCOES.md #109): teclas proprias, nao passam pelo controls.ini. Fora de triangulo, circulo e xis, valem as do jogo.
int rdrControlsPrimaryForPadBit(int bit);

int rdrControlsMenuKeyForPadBit(int bit)
{
    return bit == kTriangle ? KEY_Z : bit == kCircle ? KEY_BACKSPACE : bit == kCross ? KEY_ENTER : rdrControlsPrimaryForPadBit(bit);
}

void rdrControlsSetMenuKeysActive(bool active)
{
    ps2PadSetMenuKeys(active, KEY_Z, KEY_BACKSPACE, KEY_ENTER);
}

void rdrControlsApply()
{
    Ps2PadKeyMap map;
    for (int a = 0; a < kActionCount; ++a)
    {
        int *dst = nullptr;
        switch (kActions[a].target)
        {
        case kMoveUp: dst = map.moveUp; break;
        case kMoveDown: dst = map.moveDown; break;
        case kMoveLeft: dst = map.moveLeft; break;
        case kMoveRight: dst = map.moveRight; break;
        case kRun: dst = map.run; break;
        default: dst = map.buttons[kActions[a].target]; break;
        }
        // varias acoes podem cair no mesmo botao: junta sem passar de kSlots
        for (int k = 0; k < Ps2PadKeyMap::kSlots; ++k)
        {
            const int in = g_bindings.inputs[a][k];
            if (in == PS2_INPUT_NONE)
                continue;
            for (int d = 0; d < Ps2PadKeyMap::kSlots; ++d)
                if (dst[d] == PS2_INPUT_NONE || dst[d] == in)
                {
                    dst[d] = in;
                    break;
                }
        }
    }
    map.walkTilt = g_bindings.walkTilt;
    ps2PadSetKeyMap(map);
}

bool rdrControlsSave()
{
    const fs::path file = rdrControlsFile();
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << "; Red Dead Revolver - keyboard and mouse controls\r\n"
           "; Up to 3 keys per action, separated by commas. Mouse: MOUSE1 (left) MOUSE2 (right) MOUSE3 (middle)\r\n"
           "; MOUSE4 MOUSE5 WHEELUP WHEELDOWN. Keys: A-Z 0-9 F1-F12 SPACE ENTER ESCAPE BACKSPACE TAB CAPSLOCK\r\n"
           "; LSHIFT RSHIFT LCTRL RCTRL LALT RALT UP DOWN LEFT RIGHT NUMPAD0-9 ... NONE clears an action.\r\n"
           "\r\n[keys]\r\n";
    for (int a = 0; a < kActionCount; ++a)
    {
        std::string line;
        for (int k = 0; k < Ps2PadKeyMap::kSlots; ++k)
            if (g_bindings.inputs[a][k] != PS2_INPUT_NONE)
                line += (line.empty() ? "" : ", ") + rdrKeyName(g_bindings.inputs[a][k]);
        out << kActions[a].id << " = " << (line.empty() ? "NONE" : line) << "\r\n";
    }
    char tilt[32];
    std::snprintf(tilt, sizeof(tilt), "%.2f", g_bindings.walkTilt);
    out << "\r\n[options]\r\n; how far the left stick tilts while walking (0.1-1.0); holding Run tilts it fully\r\nwalk_tilt = "
        << tilt << "\r\n";
    return static_cast<bool>(out);
}

void rdrControlsResetDefaults()
{
    for (int a = 0; a < kActionCount; ++a)
        for (int k = 0; k < Ps2PadKeyMap::kSlots; ++k)
            g_bindings.inputs[a][k] = kActions[a].defaults[k];
    g_bindings.walkTilt = kDefaultWalkTilt;
}

// Le o controls.ini (cria com os padroes se faltar; acoes ausentes ficam no padrao) e aplica
void rdrControlsLoad()
{
    if (const char *mode = std::getenv("RDR_CONTROLS"); mode && ctlUpper(mode) == "LEGACY")
    {
        std::fprintf(stderr, "[rdr] controles: mapa padrao do runtime (RDR_CONTROLS=legacy)\n");
        return;
    }
    rdrControlsResetDefaults();
    const fs::path file = rdrControlsFile();
    std::ifstream in(file);
    if (!in)
    {
        rdrControlsSave();
        std::fprintf(stderr, "[rdr] controles: padrao gravado em %s\n", file.string().c_str());
    }
    else
    {
        std::string line;
        while (std::getline(in, line))
        {
            line = ctlTrim(line);
            if (line.empty() || line[0] == ';' || line[0] == '[')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = ctlTrim(line.substr(0, eq)), value = ctlTrim(line.substr(eq + 1));
            if (key == "walk_tilt")
            {
                g_bindings.walkTilt = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.1f, 1.0f);
                continue;
            }
            for (int a = 0; a < kActionCount; ++a)
            {
                if (key != kActions[a].id)
                    continue;
                int parsed[Ps2PadKeyMap::kSlots] = {};
                int n = 0;
                std::stringstream ss(value);
                std::string item;
                while (std::getline(ss, item, ',') && n < Ps2PadKeyMap::kSlots)
                    if (const int code = rdrKeyFromName(item); code != PS2_INPUT_NONE)
                        parsed[n++] = code;
                std::copy(parsed, parsed + Ps2PadKeyMap::kSlots, g_bindings.inputs[a]);
            }
        }
        std::fprintf(stderr, "[rdr] controles: %s\n", file.string().c_str());
    }
    rdrControlsApply();
    // Recarrega quando o arquivo muda (edicao a mao com o jogo aberto; tools/walk_test.ps1)
    static bool s_watching = false;
    if (!s_watching)
    {
        s_watching = true;
        std::thread([] {
            std::error_code ec;
            auto last = fs::last_write_time(rdrControlsFile(), ec);
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const auto now = fs::last_write_time(rdrControlsFile(), ec);
                if (ec || now == last)
                    continue;
                last = now;
                rdrControlsLoad();
                rdrKeyIconsInvalidate();
                std::fprintf(stderr, "[rdr] controles recarregados (walk_tilt=%.2f)\n", g_bindings.walkTilt);
            }
        }).detach();
    }
}

// Acesso para a tela de ajuste e os icones
int rdrControlsActionCount() { return kActionCount; }
const char *rdrControlsActionLabel(int a) { return a >= 0 && a < kActionCount ? kActions[a].label : ""; }
int rdrControlsActionPadBit(int a) { return a >= 0 && a < kActionCount ? kActions[a].target : -1; }
int rdrControlsInput(int a, int slot)
{
    return a >= 0 && a < kActionCount && slot >= 0 && slot < Ps2PadKeyMap::kSlots ? g_bindings.inputs[a][slot] : 0;
}
void rdrControlsSetInput(int a, int slot, int code)
{
    if (a >= 0 && a < kActionCount && slot >= 0 && slot < Ps2PadKeyMap::kSlots)
        g_bindings.inputs[a][slot] = code;
}
float rdrControlsWalkTilt() { return g_bindings.walkTilt; }
void rdrControlsSetWalkTilt(float t) { g_bindings.walkTilt = std::clamp(t, 0.1f, 1.0f); }

// Primeira tecla da acao ligada a um botao do PS2 (icones de tecla); 0 se nenhuma
int rdrControlsPrimaryForPadBit(int bit)
{
    for (int a = 0; a < kActionCount; ++a)
        if (kActions[a].target == bit)
            for (int k = 0; k < Ps2PadKeyMap::kSlots; ++k)
                if (g_bindings.inputs[a][k] != PS2_INPUT_NONE)
                    return g_bindings.inputs[a][k];
    return 0;
}
