// Funcoes do Windows usadas pelo lancador e pelos saves (separadas: windows.h conflita com raylib).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

// Modo da janela do jogo. O ToggleBorderlessWindowed do raylib deixa a janela sempre no topo: o Alt+Tab nao a tirava
// da frente e, sem foco ao abrir, a barra de tarefas ficava por cima. Aqui a janela sem borda e uma janela comum do
// tamanho do monitor; com foco, o Windows a trata como tela cheia e a barra some. width/height = area do jogo em
// janela (0 = 3/4 da area de trabalho).
void rdrWin32ApplyWindowMode(void *window, bool fullscreen, int width, int height)
{
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(window);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!hwnd || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    RECT r = mi.rcMonitor;
    if (fullscreen)
    {
        style = (style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP | WS_VISIBLE;
    }
    else
    {
        style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        const int workW = mi.rcWork.right - mi.rcWork.left, workH = mi.rcWork.bottom - mi.rcWork.top;
        const int w = width > 0 ? width : workW * 3 / 4, h = height > 0 ? height : workH * 3 / 4;
        r = {0, 0, w, h};
        AdjustWindowRectEx(&r, static_cast<DWORD>(style), FALSE, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
        const int outerW = (std::min)(static_cast<int>(r.right - r.left), workW);
        const int outerH = (std::min)(static_cast<int>(r.bottom - r.top), workH);
        r.left = mi.rcWork.left + (workW - outerW) / 2;
        r.top = mi.rcWork.top + (workH - outerH) / 2;
        r.right = r.left + outerW;
        r.bottom = r.top + outerH;
    }
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
#else
    (void)window, (void)fullscreen, (void)width, (void)height;
#endif
}

std::filesystem::path rdrExePath()
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf))
        return std::filesystem::path(buf);
#endif
    return {};
}

std::filesystem::path rdrExeDirectory()
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf))
        return std::filesystem::path(buf).parent_path();
#endif
    return std::filesystem::current_path();
}

// Idioma da interface do Windows (pt-BR e pt-PT usam a mesma traducao)
bool rdrSystemLanguageIsPortuguese()
{
#if defined(_WIN32)
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE;
#else
    return false;
#endif
}

// Pasta Documentos do usuario (como o GTA Vice City: Documentos\<jogo>)
std::filesystem::path rdrDocumentsDirectory()
{
#if defined(_WIN32)
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw)) && raw)
    {
        std::filesystem::path p(raw);
        CoTaskMemFree(raw);
        return p;
    }
#endif
    return {};
}

// Dialogo "abrir arquivo" para a ISO; vazio se cancelado
std::filesystem::path rdrPickIsoFile()
{
#if defined(_WIN32)
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"PS2 disc image (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = L"Select your Red Dead Revolver disc image (SLUS-20500)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn))
        return std::filesystem::path(file);
#endif
    return {};
}

// Resolucao nativa do monitor principal
void rdrNativeScreenSize(int &w, int &h)
{
    w = 1920;
    h = 1080;
#if defined(_WIN32)
    SetProcessDPIAware();
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw >= 640 && sh >= 448)
        w = sw, h = sh;
#endif
}

// Aberto pelo Explorer (sem console nem redirecionamento): stdout/stderr vao para um arquivo de log
void rdrRedirectLogIfDetached(const std::filesystem::path &logFile)
{
#if defined(_WIN32)
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err != nullptr && err != INVALID_HANDLE_VALUE)
        return;
    std::error_code ec;
    std::filesystem::create_directories(logFile.parent_path(), ec);
    // Arquivos separados: o mesmo arquivo aberto duas vezes falha (compartilhamento) e deixa o stream invalido,
    // e o 1o printf derruba o processo (0xC0000409)
    FILE *f = nullptr;
    if (_wfreopen_s(&f, logFile.c_str(), L"w", stderr) != 0)
        _wfreopen_s(&f, L"NUL", L"w", stderr);
    std::filesystem::path outFile = logFile;
    outFile.replace_extension(L".out.log");
    if (_wfreopen_s(&f, outFile.c_str(), L"w", stdout) != 0)
        _wfreopen_s(&f, L"NUL", L"w", stdout);
    // Sem buffer no stderr: o log chega ao arquivo mesmo se o jogo for fechado a forca
    setvbuf(stderr, nullptr, _IONBF, 0);
#else
    (void)logFile;
#endif
}

// Pasta do jogo no menu Iniciar do usuario (como o GTA Vice City)
std::filesystem::path rdrStartMenuDirectory()
{
#if defined(_WIN32)
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &raw)) && raw)
    {
        std::filesystem::path p(raw);
        CoTaskMemFree(raw);
        return p / L"Red Dead Revolver";
    }
#endif
    return {};
}

bool rdrCreateShortcut(const std::filesystem::path &lnk, const std::filesystem::path &target, const std::wstring &args,
                       const std::wstring &description)
{
#if defined(_WIN32)
    std::error_code ec;
    std::filesystem::create_directories(lnk.parent_path(), ec);
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW *link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link))))
    {
        link->SetPath(target.c_str());
        link->SetArguments(args.c_str());
        link->SetWorkingDirectory(target.parent_path().c_str());
        link->SetDescription(description.c_str());
        link->SetIconLocation(target.c_str(), 0);
        IPersistFile *file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file))))
        {
            ok = SUCCEEDED(file->Save(lnk.c_str(), TRUE));
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return ok;
#else
    (void)lnk, (void)target, (void)args, (void)description;
    return false;
#endif
}

bool rdrConfirm(const std::wstring &text, const std::wstring &title)
{
#if defined(_WIN32)
    return MessageBoxW(GetActiveWindow(), text.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST) == IDYES;
#else
    (void)text, (void)title;
    return false;
#endif
}

// Remove os itens da instalacao depois que este processo fechar (o exe em uso nao pode ser apagado agora):
// um cmd oculto espera ~2 s, apaga cada item e por ultimo a pasta, se ficar vazia.
bool rdrScheduleRemoval(const std::filesystem::path &root, const std::vector<std::wstring> &items)
{
#if defined(_WIN32)
    // Script temporario (UTF-8, chcp 65001: caminhos com acento): espera este processo fechar, apaga cada item e a
    // pasta (rmdir sem /s so remove se ficou vazia) e se apaga
    std::error_code ec;
    const std::filesystem::path script = std::filesystem::temp_directory_path(ec) / L"rdr_uninstall.cmd";
    auto u8 = [](const std::filesystem::path &p) {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char *>(s.data()), s.size());
    };
    std::string body = "@echo off\r\nchcp 65001 >nul\r\nping -n 3 127.0.0.1 >nul\r\n";
    for (const std::wstring &item : items)
    {
        const std::filesystem::path p = root / item;
        if (!std::filesystem::exists(p, ec))
            continue;
        body += std::filesystem::is_directory(p, ec) ? "rmdir /s /q \"" + u8(p) + "\"\r\n" : "del /f /q \"" + u8(p) + "\"\r\n";
    }
    body += "rmdir \"" + u8(root) + "\" 2>nul\r\n(goto) 2>nul & del \"%~f0\"\r\n";
    {
        std::ofstream out(script, std::ios::binary);
        out << body;
        if (!out)
            return false;
    }
    std::wstring cmd = L"cmd.exe /d /c \"\"" + script.wstring() + L"\"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    const std::wstring tempDir = std::filesystem::temp_directory_path(ec).wstring();
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                        tempDir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)root, (void)items;
    return false;
#endif
}

void rdrSetEnvDefault(const char *name, const std::string &value)
{
    if (const char *cur = std::getenv(name); cur && cur[0])
        return;
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 0);
#endif
}
