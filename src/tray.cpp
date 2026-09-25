// tray.cpp - 系统托盘实现
#include "tray.h"
#include "../resource.h"

SystemTray& SystemTray::instance() {
    static SystemTray t;
    return t;
}

bool SystemTray::create(HWND ownerWnd, HINSTANCE hInst) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = ownerWnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    // 使用 .rc 中嵌入的自定义图标；找不到则回退到系统默认图标
    nid_.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON1));
    if (!nid_.hIcon) {
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(nid_.szTip, L"Float Button - Right click for menu");
    bool ok = Shell_NotifyIconW(NIM_ADD, &nid_);
    if (!ok) {
        // Windows 11 可能因为资源管理器未启动而失败，重试
        nid_.uFlags |= NIF_STATE;
        nid_.dwState = 0;
        nid_.dwStateMask = NIS_HIDDEN;
        ok = Shell_NotifyIconW(NIM_ADD, &nid_);
    }
    created_ = ok;
    return ok;
}

void SystemTray::destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        created_ = false;
    }
}

void SystemTray::show() {
    if (!created_) return;
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void SystemTray::hide() {
    if (!created_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
}

void SystemTray::showContextMenu(HWND hwnd) {
    POINT p;
    GetCursorPos(&p);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Show/Hide (显示/隐藏)");
    AppendMenuW(menu, MF_STRING, 3, L"Settings... (设置...)");
    AppendMenuW(menu, MF_STRING, 4, L"Edit phrases (编辑常用语)");
    AppendMenuW(menu, MF_STRING, 5, L"Edit shortcuts (编辑快捷程序)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"Exit (退出)");

    // 关键：让菜单能正常关闭
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             p.x, p.y, 0, hwnd, nullptr);
    // 发送一个空消息让菜单正确关闭
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case 1:
        PostMessage(hwnd, WM_TRAY_TOGGLE, 0, 0);
        break;
    case 2:
        PostQuitMessage(0);
        break;
    case 3:
        PostMessage(hwnd, WM_TRAY_SETTINGS, 0, 0);
        break;
    case 4:
        PostMessage(hwnd, WM_TRAY_EDIT, 0, 0);
        break;
    case 5:
        PostMessage(hwnd, WM_TRAY_SHORTCUTS, 0, 0);
        break;
    }
}
