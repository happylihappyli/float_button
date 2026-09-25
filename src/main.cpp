// main.cpp - 程序入口
#include <windows.h>
#include <gdiplus.h>
#include <fstream>
#include <string>
#include "float_window.h"
#include "tray.h"
#include "hotkey.h"
#include "phrase_mgr.h"
#include "shortcut_mgr.h"
#include "config.h"
#include "settings_dialog.h"
#include "phrase_edit_dialog.h"
#include "shortcut_edit_dialog.h"
#include "window_helper.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

// 自定义消息 ID
static constexpr UINT WM_TRAYICON       = WM_USER + 1;
static constexpr UINT WM_TRAY_TOGGLE    = WM_USER + 2;
static constexpr UINT WM_TRAY_SETTINGS  = WM_USER + 3;
static constexpr UINT WM_TRAY_EDIT      = WM_USER + 4;
static constexpr UINT WM_TRAY_SHORTCUTS = WM_USER + 5;
static constexpr UINT WM_HOTKEY_PRESSED = WM_USER + 200;

// 简易日志
static std::ofstream g_log;
static std::wstring g_logPath;

void logMsg(const wchar_t* msg) {
    if (!g_log.is_open()) {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p = path;
        size_t pos = p.find_last_of(L'\\');
        if (pos != std::wstring::npos) p = p.substr(0, pos + 1);
        p += L"float_button.log";
        g_logPath = p;
        g_log.open(p, std::ios::out | std::ios::app);
    }
    if (g_log.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        g_log << "[" << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] "
              << WinHelper::utf16ToUtf8(msg) << std::endl;
        g_log.flush();
    }
}

// 全局异常处理
LONG WINAPI crashHandler(EXCEPTION_POINTERS* p) {
    logMsg(L"!!! CRASH !!!");
    return EXCEPTION_EXECUTE_HANDLER;
}

// 全局实例句柄（在 WinMain 中赋值）
static HINSTANCE g_hInst = nullptr;

// 隐藏消息窗口（用于接收托盘和热键消息）
LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:  // 托盘消息
        if (lParam == WM_RBUTTONUP) {
            SystemTray::instance().showContextMenu(hwnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            FloatWindow::instance().toggle();
        }
        return 0;
    case WM_HOTKEY_PRESSED:  // 全局热键
        GlobalHotkey::instance().toggle();
        return 0;
    case WM_TRAY_TOGGLE:
        FloatWindow::instance().toggle();
        return 0;
    case WM_TRAY_SETTINGS:
        // 独立窗口（不阻塞托盘）
        SettingsDialog::show(g_hInst);
        return 0;
    case WM_TRAY_EDIT:
        FloatWindow::instance().hidePanel();
        // 独立窗口（不阻塞托盘）
        PhraseEditDialog::show(g_hInst);
        FloatWindow::instance().refreshPhrases();
        return 0;
    case WM_TRAY_SHORTCUTS:
        FloatWindow::instance().hidePanel();
        // 独立窗口（不阻塞托盘）
        ShortcutEditDialog::show(g_hInst);
        FloatWindow::instance().refreshShortcuts();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetUnhandledExceptionFilter(crashHandler);
    logMsg(L"=== WinMain start ===");

    // 加载常用语
    try {
        PhraseManager::instance().load();
        logMsg(L"PhraseManager loaded");
    } catch (...) {
        logMsg(L"PhraseManager load FAILED");
    }

    // 加载快捷程序
    try {
        ShortcutManager::instance().load();
        logMsg(L"ShortcutManager loaded");
    } catch (...) {
        logMsg(L"ShortcutManager load FAILED");
    }

    // 注册隐藏消息窗口类
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"FloatButtonHostClass";
    if (!RegisterClassExW(&wc)) {
        logMsg(L"RegisterClass failed");
        MessageBoxW(nullptr, L"注册窗口类失败", L"错误", MB_ICONERROR);
        return 1;
    }
    logMsg(L"Window class registered");

    // 创建隐藏窗口
    HWND hHost = CreateWindowExW(0, L"FloatButtonHostClass", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!hHost) {
        logMsg(L"CreateWindow host failed");
        MessageBoxW(nullptr, L"创建消息窗口失败", L"错误", MB_ICONERROR);
        return 1;
    }
    logMsg(L"Host window created");

    // 创建悬浮窗
    try {
        if (!FloatWindow::instance().create()) {
            logMsg(L"FloatWindow create returned false");
            MessageBoxW(nullptr, L"创建悬浮窗失败", L"错误", MB_ICONERROR);
            return 1;
        }
        logMsg(L"FloatWindow created");
        FloatWindow::instance().show();
        logMsg(L"FloatWindow shown");
    } catch (...) {
        logMsg(L"FloatWindow EXCEPTION");
        MessageBoxW(nullptr, L"创建悬浮窗异常", L"错误", MB_ICONERROR);
        return 1;
    }

    // 系统托盘
    try {
        SystemTray::instance().create(hHost, hInst);
        logMsg(L"Tray created");
    } catch (...) {
        logMsg(L"Tray EXCEPTION");
    }

    // 测试模式：命令行参数 --test-edit 自动打开编辑窗口
    // 用 GetCommandLineW 替代 __argv（WinMain 不一定接 argv）
    {
        const wchar_t* cmd = GetCommandLineW();
        if (cmd && wcsstr(cmd, L"--test-edit")) {
            logMsg(L"Test mode: auto-opening edit window");
            PhraseEditDialog::show(g_hInst);
            // 不 return，进入主消息循环保持进程运行
        }
        if (cmd && wcsstr(cmd, L"--test-settings")) {
            logMsg(L"Test mode: auto-opening settings window");
            SettingsDialog::show(g_hInst);
            // 不 return，进入主消息循环保持进程运行
        }
    }

    // 全局快捷键
    try {
        if (!GlobalHotkey::instance().reregister(hHost)) {
            DWORD err = GlobalHotkey::instance().getLastError();
            wchar_t errMsg[256];
            swprintf_s(errMsg,
                L"Hotkey [%s] registration FAILED, error=%u. Right-click tray -> Settings to change.",
                AppConfig::instance().hotkeyDescription().c_str(), err);
            logMsg(errMsg);
        } else {
            wchar_t msg[128];
            swprintf_s(msg, L"Hotkey [%s] registered OK",
                AppConfig::instance().hotkeyDescription().c_str());
            logMsg(msg);
        }
    } catch (...) {
        logMsg(L"Hotkey EXCEPTION");
    }

    logMsg(L"Entering message loop");

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    logMsg(L"Message loop exited, cleaning up");

    // 清理
    GlobalHotkey::instance().unregister();
    SystemTray::instance().destroy();
    FloatWindow::instance().destroy();
    DestroyWindow(hHost);
    return 0;
}
