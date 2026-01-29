#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cwctype>

// ============================
// 0) 小工具宏：兼容 MinGW
// ============================
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static void ToLowerInPlace(std::wstring& s) {
    for (auto& ch : s) ch = (wchar_t)towlower(ch);
}

// ============================
// 1) 配置：从 INI 读取
//    Win32 原生 API：GetPrivateProfileXXX
// ============================
struct Cfg {
    std::wstring iniPath;

    // [Paths]
    std::wstring appPath;
    std::wstring flagPath;
    std::wstring logPath;
    std::wstring workDir;

    // [Maintenance]
    bool maintEnable = true;
    int  maintWindowMs = 5000;
    int  maintHoldMs = 1500;
    int  cornerPx = 120;

    // [Behavior]
    std::wstring onAppExit = L"explorer"; // exit / explorer / restart

    // [IPC]
    bool ipcEnable = true;
    std::wstring pipeName = L"\\\\.\\pipe\\DeviceLauncher";

    // [Power]
    bool allowShutdown = true;
    bool allowReboot = true;
    int  killAppBeforePowerMs = 3000;
};

static std::wstring ReadStr(const wchar_t* ini, const wchar_t* sec, const wchar_t* key, const wchar_t* defv) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(sec, key, defv, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini);
    return buf;
}
static int ReadInt(const wchar_t* ini, const wchar_t* sec, const wchar_t* key, int defv) {
    return GetPrivateProfileIntW(sec, key, defv, ini);
}
static bool ReadBool(const wchar_t* ini, const wchar_t* sec, const wchar_t* key, bool defv) {
    return ReadInt(ini, sec, key, defv ? 1 : 0) != 0;
}

static Cfg LoadCfg(const wchar_t* iniPath) {
    Cfg c; c.iniPath = iniPath;

    c.appPath  = ReadStr(iniPath, L"Paths", L"AppPath",  L"D:\\Device\\MyQtApp.exe");
    c.flagPath = ReadStr(iniPath, L"Paths", L"FlagPath", L"D:\\Device\\maintenance.flag");
    c.logPath  = ReadStr(iniPath, L"Paths", L"LogPath",  L"D:\\Device\\launcher.log");
    c.workDir  = ReadStr(iniPath, L"Paths", L"WorkDir",  L"D:\\Device");

    c.maintEnable   = ReadBool(iniPath, L"Maintenance", L"Enable", true);
    c.maintWindowMs = ReadInt (iniPath, L"Maintenance", L"WindowMs", 5000);
    c.maintHoldMs   = ReadInt (iniPath, L"Maintenance", L"HoldMs", 1500);
    c.cornerPx      = ReadInt (iniPath, L"Maintenance", L"CornerPx", 120);

    c.onAppExit     = ReadStr(iniPath, L"Behavior", L"OnAppExit", L"explorer");

    c.ipcEnable     = ReadBool(iniPath, L"IPC", L"Enable", true);
    c.pipeName      = ReadStr (iniPath, L"IPC", L"PipeName", L"\\\\.\\pipe\\DeviceLauncher");

    c.allowShutdown = ReadBool(iniPath, L"Power", L"AllowShutdown", true);
    c.allowReboot   = ReadBool(iniPath, L"Power", L"AllowReboot", true);
    c.killAppBeforePowerMs = ReadInt(iniPath, L"Power", L"KillAppBeforePowerMs", 3000);

    return c;
}

// ============================
// 2) 日志：最简单可靠的 append
// ============================
static void AppendLog(const std::wstring& logPath, const wchar_t* msg) {
    HANDLE h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[2048];
    int n = wsprintfW(buf, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    DWORD written = 0;
    WriteFile(h, buf, (DWORD)(n * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
}

// ============================
// 3) 系统操作：文件存在、进桌面、关机/重启
// ============================
static bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void StartExplorer(const Cfg& cfg) {
    AppendLog(cfg.logPath, L"StartExplorer: entering desktop (explorer.exe)");
    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

static bool EnableShutdownPrivilege(const Cfg& cfg) {
    HANDLE hToken{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        AppendLog(cfg.logPath, L"ERROR: OpenProcessToken failed.");
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        AppendLog(cfg.logPath, L"ERROR: LookupPrivilegeValue failed.");
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(hToken);

    if (GetLastError() != ERROR_SUCCESS) {
        AppendLog(cfg.logPath, L"ERROR: AdjustTokenPrivileges failed.");
        return false;
    }
    return true;
}

static void DoPowerAction(const Cfg& cfg, bool reboot) {
    EnableShutdownPrivilege(cfg);
    AppendLog(cfg.logPath, reboot ? L"PowerAction: reboot" : L"PowerAction: shutdown");
    ExitWindowsEx(reboot ? EWX_REBOOT : EWX_POWEROFF, SHTDN_REASON_MAJOR_APPLICATION);
}

// ============================
// 4) 维护模式：左上角小窗口长按
//    - 窗口几乎透明，不在任务栏/Alt+Tab
//    - 只存在 maintWindowMs 这段时间
// ============================
struct MaintenanceDetector {
    const Cfg* cfg = nullptr;

    HWND hwnd = nullptr;
    bool pressed = false;
    DWORD pressStartTick = 0;

    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = (MaintenanceDetector*)GetWindowLongPtrW(h, GWLP_USERDATA);

        switch (msg) {
        case WM_CREATE:
            return 0;

        case WM_LBUTTONDOWN:
            if (self && self->cfg) {
                SetCapture(h); // 保证长按过程不丢
                self->pressed = true;
                self->pressStartTick = GetTickCount();
            }
            return 0;

        case WM_LBUTTONUP:
            if (self) {
                self->pressed = false;
                self->pressStartTick = 0;
            }
            ReleaseCapture();
            return 0;

        case WM_CAPTURECHANGED:
            // 系统打断/焦点变化：当作抬起
            if (self) {
                self->pressed = false;
                self->pressStartTick = 0;
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(h, msg, w, l);
    }

    bool Run() {
        if (!cfg || !cfg->maintEnable) return false;

        // 注册窗口类
        const wchar_t* cls = L"LauncherMaintenanceCornerWnd";
        WNDCLASSW wc{};
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);

        // 只创建左上角 cornerPx x cornerPx 小窗口
        int w = cfg->cornerPx;
        int h = cfg->cornerPx;

        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            cls, L"", WS_POPUP,
            0, 0, w, h,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
        );
        if (!hwnd) return false;

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)this);

        // alpha=1 -> 肉眼不可见，但仍可接收鼠标（触摸通常映射为鼠标）
        SetLayeredWindowAttributes(hwnd, 0, 1, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW);

        DWORD startTick = GetTickCount();
        MSG msg{};

        while ((int)(GetTickCount() - startTick) < cfg->maintWindowMs) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (pressed && pressStartTick != 0) {
                DWORD held = GetTickCount() - pressStartTick;
                if ((int)held >= cfg->maintHoldMs) {
                    AppendLog(cfg->logPath, L"Maintenance: hold top-left corner triggered.");
                    DestroyWindow(hwnd);
                    hwnd = nullptr;
                    return true;
                }
            }
            Sleep(10);
        }

        DestroyWindow(hwnd);
        hwnd = nullptr;
        return false;
    }
};

// ============================
// 5) 启动 Qt：CreateProcess + Wait
//    - CreateProcessW 的 cmdline 必须是可写缓冲区（vector）
// ============================
static bool StartQtProcess(const Cfg& cfg, PROCESS_INFORMATION& pi) {
    if (!FileExists(cfg.appPath)) {
        AppendLog(cfg.logPath, L"ERROR: AppPath not found.");
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWDEFAULT;

    std::wstring cmd = L"\"" + cfg.appPath + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0'); // CreateProcessW 要求可写 + NUL 结尾

    AppendLog(cfg.logPath, L"StartQtProcess: launching Qt app...");
    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr, nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        cfg.workDir.empty() ? nullptr : cfg.workDir.c_str(),
        &si,
        &pi
    );

    if (!ok) {
        DWORD e = GetLastError();
        wchar_t m[256];
        wsprintfW(m, L"ERROR: CreateProcess failed. GetLastError=%lu", e);
        AppendLog(cfg.logPath, m);
        return false;
    }
    return true;
}

// ============================
// 6) IPC：命名管道（Qt -> Launcher）
//    命令：maintenance / shutdown / reboot / exitapp
//    注意：这里的 “kill app” 是强制 TerminateProcess，
//         更优雅的退出建议 Qt 自己先保存后再发 shutdown。
// ============================
struct IpcState {
    Cfg cfg{};
    HANDLE appProcess = nullptr;     // 主线程写入，IPC 线程读取
    CRITICAL_SECTION cs{};
    volatile bool running = true;
};

static std::wstring ReadPipeMessageUtf8(HANDLE hPipe) {
    std::wstring out;
    char buf[512];
    DWORD read = 0;

    // 命令很短：读到 \n 或连接结束
    while (true) {
        BOOL ok = ReadFile(hPipe, buf, sizeof(buf) - 1, &read, nullptr);
        if (!ok || read == 0) break;
        buf[read] = 0;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)read, nullptr, 0);
        if (wlen > 0) {
            std::vector<wchar_t> wbuf((size_t)wlen + 1);
            MultiByteToWideChar(CP_UTF8, 0, buf, (int)read, wbuf.data(), wlen);
            wbuf[wlen] = 0;
            out.append(wbuf.data(), wlen);
        }

        if (out.find(L'\n') != std::wstring::npos) break;
    }

    // trim
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ' || out.back() == L'\t'))
        out.pop_back();
    while (!out.empty() && (out.front() == L' ' || out.front() == L'\t'))
        out.erase(out.begin());

    return out;
}

static void KillAppIfAny(const Cfg& cfg, HANDLE hProc, DWORD waitMs) {
    if (!hProc) return;
    AppendLog(cfg.logPath, L"IPC: terminating Qt app...");
    TerminateProcess(hProc, 0xBEEF);
    WaitForSingleObject(hProc, waitMs);
}

static void WakeIpcThreadOnce(const Cfg& cfg)
{
    if (!cfg.ipcEnable) return;

    // 以客户端身份连接一次管道，唤醒阻塞在 ConnectNamedPipe 的服务端线程
    HANDLE h = CreateFileW(
        cfg.pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        // 如果服务端还没 CreateNamedPipe / 暂时不可用，忽略即可
        return;
    }

    // 发一个无害命令，让服务端读到后立刻返回
    const char* msg = "noop\n";
    DWORD written = 0;
    WriteFile(h, msg, (DWORD)lstrlenA(msg), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
}

static DWORD WINAPI PipeServerThread(LPVOID p) {
    IpcState* st = (IpcState*)p;
    if (!st) return 0;

    const Cfg& cfg = st->cfg;
    AppendLog(cfg.logPath, L"IPC: thread started.");

    while (st->running && cfg.ipcEnable) {
        HANDLE hPipe = CreateNamedPipeW(
            cfg.pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096,
            0,
            nullptr
        );
        if (hPipe == INVALID_HANDLE_VALUE) {
            AppendLog(cfg.logPath, L"ERROR: CreateNamedPipe failed.");
            Sleep(1000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(hPipe);
            continue;
        }

        std::wstring cmd = ReadPipeMessageUtf8(hPipe);
        ToLowerInPlace(cmd);

        if (!cmd.empty()) {
            std::wstring logLine = L"IPC: cmd=" + cmd;
            AppendLog(cfg.logPath, logLine.c_str());
        }

        if (cmd == L"noop") {
			// 仅用于唤醒/退出，不做任何事
		}
		else if (cmd == L"maintenance")  {
            StartExplorer(cfg);
        }
        else if (cmd == L"shutdown") {
            if (!cfg.allowShutdown) {
                AppendLog(cfg.logPath, L"IPC: shutdown denied by config.");
            } else {
                HANDLE hProc = nullptr;
                EnterCriticalSection(&st->cs);
                hProc = st->appProcess;
                LeaveCriticalSection(&st->cs);

                if (hProc && cfg.killAppBeforePowerMs > 0) {
                    KillAppIfAny(cfg, hProc, (DWORD)cfg.killAppBeforePowerMs);
                }
                DoPowerAction(cfg, false);
            }
        }
        else if (cmd == L"reboot") {
            if (!cfg.allowReboot) {
                AppendLog(cfg.logPath, L"IPC: reboot denied by config.");
            } else {
                HANDLE hProc = nullptr;
                EnterCriticalSection(&st->cs);
                hProc = st->appProcess;
                LeaveCriticalSection(&st->cs);

                if (hProc && cfg.killAppBeforePowerMs > 0) {
                    KillAppIfAny(cfg, hProc, (DWORD)cfg.killAppBeforePowerMs);
                }
                DoPowerAction(cfg, true);
            }
        }
        else if (cmd == L"exitapp") {
            HANDLE hProc = nullptr;
            EnterCriticalSection(&st->cs);
            hProc = st->appProcess;
            LeaveCriticalSection(&st->cs);

            if (hProc) {
                AppendLog(cfg.logPath, L"IPC: exitapp -> terminate Qt");
                TerminateProcess(hProc, 0xC0DE);
            }
        }

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    AppendLog(cfg.logPath, L"IPC: thread exiting.");
    return 0;
}

// ============================
// 7) wWinMain：主流程入口
// ============================
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // launcher.ini 默认：与 Launcher.exe 同目录
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring dir = exePath;
    auto pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) dir.resize(pos);

    std::wstring iniPath = dir + L"\\launcher.ini";
    Cfg cfg = LoadCfg(iniPath.c_str());

    AppendLog(cfg.logPath, L"Launcher: started (Win32, no watchdog).");

    // (1) flag 强制维护
    if (FileExists(cfg.flagPath)) {
        AppendLog(cfg.logPath, L"Maintenance: flag exists -> explorer.");
        StartExplorer(cfg);
        return 0;
    }

    // (2) 开机维护窗口：左上角长按
    if (cfg.maintEnable) {
        MaintenanceDetector det;
        det.cfg = &cfg;
        if (det.Run()) {
            StartExplorer(cfg);
            return 0;
        }
    }

    // (3) 启动 IPC 线程（命名管道）
    IpcState st;
    st.cfg = cfg;
    InitializeCriticalSection(&st.cs);

    HANDLE hIpcThread = nullptr;
    if (cfg.ipcEnable) {
        hIpcThread = CreateThread(nullptr, 0, PipeServerThread, &st, 0, nullptr);
    }

    // (4) 主循环：运行 Qt，根据 OnAppExit 决策是否重启/进桌面/退出
    while (true) {
        PROCESS_INFORMATION pi{};
        if (!StartQtProcess(cfg, pi)) {
            AppendLog(cfg.logPath, L"Qt start failed -> explorer.");
            StartExplorer(cfg);
            break;
        }

        // 把 app 句柄交给 IPC 线程（用于 shutdown/exitapp）
        EnterCriticalSection(&st.cs);
        st.appProcess = pi.hProcess;
        LeaveCriticalSection(&st.cs);

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        // 清理 appProcess
        EnterCriticalSection(&st.cs);
        st.appProcess = nullptr;
        LeaveCriticalSection(&st.cs);

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        wchar_t m[256];
        wsprintfW(m, L"Qt exited. exitCode=%lu", exitCode);
        AppendLog(cfg.logPath, m);

        std::wstring action = cfg.onAppExit;
        ToLowerInPlace(action);

        if (action == L"restart") {
            AppendLog(cfg.logPath, L"OnAppExit=restart -> restarting Qt.");
            Sleep(300);
            continue;
        }
        if (action == L"explorer") {
            AppendLog(cfg.logPath, L"OnAppExit=explorer -> enter desktop.");
            StartExplorer(cfg);
            break;
        }

        AppendLog(cfg.logPath, L"OnAppExit=exit -> launcher exit.");
        break;
    }

    // (5) 退出：收尾 IPC
    st.running = false;

	// 唤醒一次：让阻塞在 ConnectNamedPipe 的线程立刻返回，从而尽快退出
	WakeIpcThreadOnce(cfg);

	if (hIpcThread) {
		// 可选：稍微等一下，让线程有机会退出（不想等也可以不等）
		WaitForSingleObject(hIpcThread, 200);
		CloseHandle(hIpcThread);
	}
	DeleteCriticalSection(&st.cs);

    AppendLog(cfg.logPath, L"Launcher: exit.");
    return 0;
}
