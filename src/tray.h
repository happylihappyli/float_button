// tray.h - 系统托盘
#pragma once

#include <windows.h>

class SystemTray {
public:
    static SystemTray& instance();
    bool create(HWND ownerWnd, HINSTANCE hInst);
    void destroy();
    void show();
    void hide();
    void showContextMenu(HWND hwnd);

    // 消息 ID（与 main.cpp 同步）
    static constexpr UINT WM_TRAYICON      = WM_USER + 1;
    static constexpr UINT WM_TRAY_TOGGLE   = WM_USER + 2;
    static constexpr UINT WM_TRAY_SETTINGS = WM_USER + 3;
    static constexpr UINT WM_TRAY_EDIT     = WM_USER + 4;
    static constexpr UINT WM_TRAY_SHORTCUTS = WM_USER + 5;

private:
    SystemTray() = default;
    NOTIFYICONDATAW nid_{};
    bool created_ = false;
};
