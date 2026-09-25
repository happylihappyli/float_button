// settings_dialog.h - 设置窗口
#pragma once

#include <windows.h>

class SettingsDialog {
public:
    // 异步显示（不阻塞调用方）
    static void show(HINSTANCE hInst);
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

private:
    static void loadToUI(HWND hwnd);
    static void saveFromUI(HWND hwnd);
    static LPCWSTR kClassName;
    // 工作线程
    static DWORD WINAPI threadProc(LPVOID param);
};
