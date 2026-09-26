// settings_dialog.cpp
#include "settings_dialog.h"
#include "config.h"
#include "hotkey.h"
#include "autostart.h"
#include "tts_dialog.h"
#include <windowsx.h>
#include <commctrl.h>
#include <string>

LPCWSTR SettingsDialog::kClassName = L"SettingsDialogClass";

extern void logMsg(const wchar_t* msg);

namespace {
constexpr int kBtnRecordId    = 2001;     // "按下录制" 按钮
constexpr int kBtnSaveId      = 2002;     // "保存"
constexpr int kBtnCancelId    = 2003;     // "取消"
constexpr int kBtnTtsId       = 2004;     // "打开 TTS 对话框"
constexpr int kStaticHotkeyId = 2010;     // 显示当前快捷键
constexpr int kStaticAlphaId  = 2020;     // 透明度滑块标签
constexpr int kSliderAlphaId  = 2021;     // 透明度滑块
constexpr int kChkAutoStartId = 2030;     // 开机自动启动
constexpr int kChkAutoSpeakId = 2031;     // 剪贴板变化自动朗读
constexpr int kStaticStatusId = 2040;     // 底部状态提示
}

// 录制快捷键用的状态
static bool g_recording = false;
static UINT g_recMods = 0;
static UINT g_recVk = 0;

void SettingsDialog::show(HINSTANCE hInst) {
    // 防止重复打开
    HWND existing = FindWindowW(kClassName, L"Settings");
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }

    // 异步：开线程运行
    HANDLE hThread = CreateThread(nullptr, 0, threadProc, (LPVOID)hInst, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    }
}

DWORD WINAPI SettingsDialog::threadProc(LPVOID param) {
    HINSTANCE hInst = (HINSTANCE)param;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int w = 480, h = 440;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        kClassName, L"Settings",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!IsWindow(hwnd)) break;
    }

    UnregisterClassW(kClassName, hInst);
    return 0;
}

void SettingsDialog::loadToUI(HWND hwnd) {
    // 当前快捷键显示
    SetWindowTextW(GetDlgItem(hwnd, kStaticHotkeyId),
                   AppConfig::instance().hotkeyDescription().c_str());

    // 透明度
    SendMessageW(GetDlgItem(hwnd, kSliderAlphaId), TBM_SETRANGE, FALSE, MAKELONG(80, 255));
    SendMessageW(GetDlgItem(hwnd, kSliderAlphaId), TBM_SETPOS, TRUE,
                 AppConfig::instance().floatButtonAlpha());

    wchar_t buf[32];
    swprintf_s(buf, L"Float button alpha: %d / 255", AppConfig::instance().floatButtonAlpha());
    SetWindowTextW(GetDlgItem(hwnd, kStaticAlphaId), buf);

    // 自动启动复选框：以注册表实际状态为准（避免 config 与系统不一致）
    bool enabled = AutoStart::isEnabled();
    AppConfig::instance().setAutoStart(enabled);
    SendMessageW(GetDlgItem(hwnd, kChkAutoStartId), BM_SETCHECK,
                 enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    // 剪贴板自动朗读
    bool autoSpeak = AppConfig::instance().autoSpeakClipboard();
    SendMessageW(GetDlgItem(hwnd, kChkAutoSpeakId), BM_SETCHECK,
                 autoSpeak ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsDialog::saveFromUI(HWND hwnd) {
    // 如果在录制状态，先保存录制的
    if (g_recording && g_recVk != 0) {
        AppConfig::instance().setHotkey(g_recMods, g_recVk);
    }

    int alpha = (int)SendMessageW(GetDlgItem(hwnd, kSliderAlphaId), TBM_GETPOS, 0, 0);
    AppConfig::instance().setFloatButtonAlpha(alpha);

    // 自动启动
    bool wantAutoStart = (SendMessageW(GetDlgItem(hwnd, kChkAutoStartId), BM_GETCHECK, 0, 0)
                          == BST_CHECKED);
    bool autoStartOk = false;
    if (wantAutoStart) {
        if (AutoStart::enable()) {
            autoStartOk = true;
        } else {
            MessageBoxW(hwnd, L"无法写入注册表，启动项设置失败。\n请确认有 HKCU 写入权限。",
                        L"提示", MB_ICONWARNING);
        }
    } else {
        if (!AutoStart::disable()) {
            MessageBoxW(hwnd, L"无法删除启动项，请手动清理注册表。",
                        L"提示", MB_ICONWARNING);
        }
    }
    AppConfig::instance().setAutoStart(autoStartOk ? wantAutoStart : false);

    // 剪贴板自动朗读
    bool wantAutoSpeak = (SendMessageW(GetDlgItem(hwnd, kChkAutoSpeakId), BM_GETCHECK, 0, 0)
                          == BST_CHECKED);
    AppConfig::instance().setAutoSpeakClipboard(wantAutoSpeak);

    AppConfig::instance().save();
}

LRESULT CALLBACK SettingsDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        // 标题：快捷键设置
        CreateWindowExW(0, L"STATIC", L"Hotkey (press to toggle floating button):",
            WS_CHILD | WS_VISIBLE,
            12, 12, 440, 20, hwnd, nullptr, hInst, nullptr);

        // 当前快捷键显示
        CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 38, 300, 32, hwnd, (HMENU)kStaticHotkeyId, hInst, nullptr);

        // 录制按钮
        CreateWindowExW(0, L"BUTTON", L"Press to record",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            322, 38, 110, 32, hwnd, (HMENU)kBtnRecordId, hInst, nullptr);

        // 透明度
        wchar_t buf[64];
        swprintf_s(buf, L"Alpha: 220 / 255");
        CreateWindowExW(0, L"STATIC", buf,
            WS_CHILD | WS_VISIBLE,
            12, 86, 440, 20, hwnd, (HMENU)kStaticAlphaId, hInst, nullptr);

        // 透明度滑块
        CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            12, 110, 440, 40, hwnd, (HMENU)kSliderAlphaId, hInst, nullptr);

        // 分隔说明
        CreateWindowExW(0, L"STATIC", L"Startup & TTS:",
            WS_CHILD | WS_VISIBLE,
            12, 160, 440, 20, hwnd, nullptr, hInst, nullptr);

        // 开机自动启动复选框
        CreateWindowExW(0, L"BUTTON", L"开机自动启动 (Run on Windows startup)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            12, 184, 440, 22, hwnd, (HMENU)kChkAutoStartId, hInst, nullptr);

        // 剪贴板变化自动朗读
        CreateWindowExW(0, L"BUTTON", L"剪贴板变化时自动朗读 (Auto speak clipboard)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            12, 210, 440, 22, hwnd, (HMENU)kChkAutoSpeakId, hInst, nullptr);

        // 打开 TTS 对话框
        CreateWindowExW(0, L"BUTTON", L"打开 TTS 朗读对话框 (Open TTS dialog)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 244, 280, 32, hwnd, (HMENU)kBtnTtsId, hInst, nullptr);

        // 状态提示
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 286, 440, 20, hwnd, (HMENU)kStaticStatusId, hInst, nullptr);

        // 保存 / 取消
        CreateWindowExW(0, L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            220, 330, 90, 32, hwnd, (HMENU)kBtnSaveId, hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            325, 330, 90, 32, hwnd, (HMENU)kBtnCancelId, hInst, nullptr);

        loadToUI(hwnd);

        // 底部状态：当前自动启动注册表实际路径（便于诊断）
        std::wstring regPath = AutoStart::getRegisteredPath();
        if (!regPath.empty()) {
            std::wstring status = L"AutoStart: " + regPath;
            if (status.size() > 80) status = status.substr(0, 77) + L"...";
            SetWindowTextW(GetDlgItem(hwnd, kStaticStatusId), status.c_str());
        } else {
            SetWindowTextW(GetDlgItem(hwnd, kStaticStatusId), L"AutoStart: (not registered)");
        }
        return 0;
    }

    case WM_HSCROLL: {
        if (GetWindowLongPtr((HWND)lParam, GWLP_ID) == kSliderAlphaId) {
            int pos = (int)SendMessageW((HWND)lParam, TBM_GETPOS, 0, 0);
            wchar_t buf[64];
            swprintf_s(buf, L"Alpha: %d / 255", pos);
            SetWindowTextW(GetDlgItem(hwnd, kStaticAlphaId), buf);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == kBtnRecordId) {
            g_recording = true;
            g_recMods = 0;
            g_recVk = 0;
            SetWindowTextW(GetDlgItem(hwnd, kStaticHotkeyId), L"... Press any key combo ...");
            SetFocus(hwnd);
        } else if (id == kBtnSaveId) {
            saveFromUI(hwnd);
            // 重新注册快捷键
            GlobalHotkey::instance().reregister(GetParent(hwnd));
            DestroyWindow(hwnd);
        } else if (id == kBtnCancelId) {
            DestroyWindow(hwnd);
        } else if (id == kBtnTtsId) {
            // 立即打开 TTS 对话框（独立线程）
            TtsDialog::show((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (g_recording) {
            UINT vk = (UINT)wParam;
            // 忽略单独的修饰键
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
                return 0;
            }

            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;

            if (mods == 0) {
                MessageBoxW(hwnd, L"Please include at least one modifier (Ctrl/Alt/Shift/Win)",
                            L"Invalid hotkey", MB_ICONWARNING);
                return 0;
            }

            g_recMods = mods;
            g_recVk = vk;
            g_recording = false;

            // 临时保存到 config 用于显示
            AppConfig::instance().setHotkey(mods, vk);
            SetWindowTextW(GetDlgItem(hwnd, kStaticHotkeyId),
                           AppConfig::instance().hotkeyDescription().c_str());
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_recording = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}