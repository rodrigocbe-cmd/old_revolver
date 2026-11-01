// "Add to Steam" do lancador: cria (ou atualiza/remove) o jogo como atalho nao-Steam em
// <Steam>\userdata\<conta>\config\shortcuts.vdf (VDF binario) e copia as artes da colecao SteamGridDB
// (pasta steam\ do pacote, de assets/SGDB) para config\grid\<appid>{p,,_hero,_logo,_icon}.png.
// O Steam reescreve shortcuts.vdf ao fechar e nao recarrega: ele e fechado antes (-shutdown) e reaberto depois.
// RDR_STEAM_DIR aponta para outra pasta do Steam (testes; nesse caso o processo do Steam nao e tocado).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace fs = std::filesystem;

namespace
{
    constexpr const char *kAppName = "Red Dead Revolver";

    // No do VDF binario: 0x00 mapa, 0x01 string, 0x02 int32, 0x03 float, 0x07 uint64; 0x08 fecha o mapa
    struct VdfNode
    {
        uint8_t type = 0;
        std::string key, str;
        uint64_t num = 0;
        std::vector<VdfNode> kids;
    };

    bool readCString(const std::string &d, size_t &p, std::string &out)
    {
        const size_t end = d.find('\0', p);
        if (end == std::string::npos)
            return false;
        out.assign(d, p, end - p);
        p = end + 1;
        return true;
    }

    bool parseMap(const std::string &d, size_t &p, std::vector<VdfNode> &out)
    {
        while (p < d.size())
        {
            VdfNode n;
            n.type = static_cast<uint8_t>(d[p++]);
            if (n.type == 0x08)
                return true;
            if (!readCString(d, p, n.key))
                return false;
            const size_t width = n.type == 0x02 || n.type == 0x03 ? 4 : n.type == 0x07 ? 8 : 0;
            if (n.type == 0x00)
            {
                if (!parseMap(d, p, n.kids))
                    return false;
            }
            else if (n.type == 0x01)
            {
                if (!readCString(d, p, n.str))
                    return false;
            }
            else if (width && p + width <= d.size())
            {
                for (size_t i = 0; i < width; ++i)
                    n.num |= static_cast<uint64_t>(static_cast<uint8_t>(d[p + i])) << (8 * i);
                p += width;
            }
            else
                return false;
            out.push_back(std::move(n));
        }
        return false;
    }

    void writeMap(std::string &o, const std::vector<VdfNode> &nodes)
    {
        for (const VdfNode &n : nodes)
        {
            o += static_cast<char>(n.type);
            o += n.key;
            o += '\0';
            if (n.type == 0x00)
                writeMap(o, n.kids);
            else if (n.type == 0x01)
                o += n.str, o += '\0';
            else
                for (size_t i = 0; i < (n.type == 0x07 ? 8u : 4u); ++i)
                    o += static_cast<char>((n.num >> (8 * i)) & 0xFF);
        }
        o += '\x08';
    }

    bool iequals(const std::string &a, const std::string &b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    const VdfNode *field(const VdfNode &m, const char *key)
    {
        for (const VdfNode &k : m.kids)
            if (iequals(k.key, key))
                return &k;
        return nullptr;
    }

    VdfNode str(const char *key, const std::string &v)
    {
        VdfNode n;
        n.type = 0x01, n.key = key, n.str = v;
        return n;
    }

    VdfNode i32(const char *key, uint32_t v)
    {
        VdfNode n;
        n.type = 0x02, n.key = key, n.num = v;
        return n;
    }

    uint32_t crc32(const std::string &s)
    {
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned char ch : s)
        {
            c ^= ch;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }

    std::string u8(const fs::path &p)
    {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char *>(s.data()), s.size());
    }

    fs::path steamRoot()
    {
        if (const char *env = std::getenv("RDR_STEAM_DIR"); env && env[0])
            return fs::u8path(env);
#if defined(_WIN32)
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
            return fs::path(buf).make_preferred();
#endif
        return {};
    }

    // Contas do Steam neste PC: userdata\<id numerico> (0 = sem conta)
    std::vector<fs::path> steamUsers(const fs::path &steam)
    {
        std::vector<fs::path> users;
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(steam / "userdata", ec))
        {
            const std::string n = e.path().filename().string();
            if (e.is_directory(ec) && !n.empty() && n != "0" && std::all_of(n.begin(), n.end(), ::isdigit))
                users.push_back(e.path());
        }
        return users;
    }

    bool readFile(const fs::path &p, std::string &out)
    {
        std::ifstream in(p, std::ios::binary);
        if (!in)
            return false;
        out.assign(std::istreambuf_iterator<char>(in), {});
        return true;
    }

    // Entrada do jogo em shortcuts: pelo exe (com ou sem aspas) ou pelo nome
    bool isOurs(const VdfNode &e, const std::string &exeQuoted)
    {
        const VdfNode *exe = field(e, "exe"), *name = field(e, "appname");
        return (exe && (iequals(exe->str, exeQuoted) || iequals("\"" + exe->str + "\"", exeQuoted))) ||
               (name && name->str == kAppName);
    }

#if defined(_WIN32)
    DWORD findProcess(const wchar_t *exeName)
    {
        DWORD pid = 0;
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok && !pid; ok = Process32NextW(snap, &pe))
            if (_wcsicmp(pe.szExeFile, exeName) == 0)
                pid = pe.th32ProcessID;
        CloseHandle(snap);
        return pid;
    }

    bool runDetached(const fs::path &exe, const std::wstring &args)
    {
        std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                            exe.parent_path().c_str(), &si, &pi))
            return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
#endif
} // namespace

bool rdrSteamRunning()
{
#if defined(_WIN32)
    if (const char *env = std::getenv("RDR_STEAM_DIR"); env && env[0])
        return false;
    return findProcess(L"steam.exe") != 0;
#else
    return false;
#endif
}

// Steam aberto: em vez de fechar o Steam, roda este exe oculto ("--steam-add-later"), que espera o Steam fechar
// e grava o atalho; aparece na proxima vez que o Steam abrir
bool rdrSteamAddWhenClosed(const fs::path &exe)
{
#if defined(_WIN32)
    std::wstring cmd = L"\"" + exe.wstring() + L"\" --steam-add-later";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr,
                        exe.parent_path().c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)exe;
    return false;
#endif
}

void rdrSteamWaitForExit()
{
#if defined(_WIN32)
    while (rdrSteamRunning())
        Sleep(2000);
    Sleep(2000); // o Steam grava shortcuts.vdf ao sair
#endif
}

// add = true: cria/atualiza o atalho e as artes; false: remove (desinstalador). Retorna "" ou a mensagem de erro.
// Sem entrada a remover, nao fecha o Steam.
std::string rdrSteamShortcut(const fs::path &root, bool add)
{
    const fs::path steam = steamRoot();
    if (steam.empty() || !fs::exists(steam))
        return "Steam is not installed.";
    const std::vector<fs::path> users = steamUsers(steam);
    if (users.empty())
        return "No Steam account found. Log in to Steam once and try again.";

    const fs::path exe = root / L"RedDeadRevolver.exe";
    const std::string exeQuoted = "\"" + u8(exe) + "\"";
    const uint32_t newAppId = crc32(exeQuoted + kAppName) | 0x80000000u;

    // Le e altera tudo antes de fechar o Steam; grava depois
    struct Pending
    {
        fs::path vdf, grid;
        std::vector<VdfNode> rootNodes;
        uint32_t appId = 0;
        std::vector<uint32_t> removedIds;
    };
    std::vector<Pending> work;
    for (const fs::path &user : users)
    {
        Pending w;
        w.vdf = user / "config" / "shortcuts.vdf";
        w.grid = user / "config" / "grid";
        std::string data;
        size_t p = 0;
        if (readFile(w.vdf, data) && !data.empty())
        {
            if (!parseMap(data, p, w.rootNodes))
                return "Could not read " + u8(w.vdf) + " (unknown format); nothing was changed.";
        }
        if (w.rootNodes.empty() || !iequals(w.rootNodes[0].key, "shortcuts"))
        {
            VdfNode s;
            s.type = 0x00, s.key = "shortcuts";
            w.rootNodes.insert(w.rootNodes.begin(), s);
        }
        std::vector<VdfNode> &list = w.rootNodes[0].kids;
        uint32_t keepId = 0;
        bool found = false;
        for (auto it = list.begin(); it != list.end();)
        {
            if (it->type == 0x00 && isOurs(*it, exeQuoted))
            {
                found = true;
                if (const VdfNode *id = field(*it, "appid"); id && id->num)
                    keepId = static_cast<uint32_t>(id->num), w.removedIds.push_back(keepId);
                it = list.erase(it);
            }
            else
                ++it;
        }
        if (!add && !found)
            continue;
        if (add)
        {
            w.appId = keepId ? keepId : newAppId;
            VdfNode e;
            e.type = 0x00;
            e.kids = {i32("appid", w.appId),
                      str("AppName", kAppName),
                      str("Exe", exeQuoted),
                      str("StartDir", "\"" + u8(root) + "\""),
                      str("icon", u8(w.grid / (std::to_string(w.appId) + "_icon.png"))),
                      str("ShortcutPath", ""),
                      str("LaunchOptions", ""),
                      i32("IsHidden", 0),
                      i32("AllowDesktopConfig", 1),
                      i32("AllowOverlay", 1),
                      i32("OpenVR", 0),
                      i32("Devkit", 0),
                      str("DevkitGameID", ""),
                      i32("DevkitOverrideAppID", 0),
                      i32("LastPlayTime", 0),
                      str("FlatpakAppID", "")};
            VdfNode tags;
            tags.type = 0x00, tags.key = "tags";
            e.kids.push_back(tags);
            list.push_back(std::move(e));
        }
        for (size_t i = 0; i < list.size(); ++i)
            list[i].key = std::to_string(i);
        work.push_back(std::move(w));
    }
    if (work.empty())
        return {};

    // Steam aberto: fecha (ele sobrescreveria o arquivo ao sair) e reabre no fim
    bool reopen = false;
#if defined(_WIN32)
    if (rdrSteamRunning())
    {
        reopen = true;
        runDetached(steam / L"steam.exe", L"-shutdown");
        for (int i = 0; i < 60 && findProcess(L"steam.exe"); ++i)
            Sleep(500);
        if (findProcess(L"steam.exe"))
            return "Steam did not close. Close Steam and try again.";
        Sleep(1000);
    }
#endif

    const fs::path art = root / "steam";
    const std::array<std::pair<const char *, const char *>, 5> slots = {{{"capsule.png", "p.png"},
                                                                          {"wide.png", ".png"},
                                                                          {"hero.png", "_hero.png"},
                                                                          {"logo.png", "_logo.png"},
                                                                          {"icon.png", "_icon.png"}}};
    std::string err;
    for (Pending &w : work)
    {
        std::error_code ec;
        fs::create_directories(w.vdf.parent_path(), ec);
        if (fs::exists(w.vdf, ec))
            fs::copy_file(w.vdf, fs::path(w.vdf).concat(".rdr_backup"), fs::copy_options::overwrite_existing, ec);
        std::string out;
        writeMap(out, w.rootNodes);
        {
            std::ofstream f(w.vdf, std::ios::binary | std::ios::trunc);
            f << out;
            if (!f)
                err = "Could not write " + u8(w.vdf) + ".";
        }
        for (uint32_t id : w.removedIds)
            if (!add || id != w.appId)
                for (const auto &s : slots)
                    fs::remove(w.grid / (std::to_string(id) + s.second), ec);
        if (add)
        {
            fs::create_directories(w.grid, ec);
            for (const auto &s : slots)
                if (fs::exists(art / s.first, ec))
                    fs::copy_file(art / s.first, w.grid / (std::to_string(w.appId) + s.second),
                                  fs::copy_options::overwrite_existing, ec);
        }
    }
#if defined(_WIN32)
    if (reopen)
        runDetached(steam / L"steam.exe", L"");
#endif
    return err;
}
