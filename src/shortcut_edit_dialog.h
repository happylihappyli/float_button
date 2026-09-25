// shortcut_edit_dialog.h - 快捷程序编辑窗口
#pragma once

#include <windows.h>

/**
 * 快捷程序编辑窗口
 *  - 列表显示所有快捷程序（图标/名称/路径/参数）
 *  - 添加 / 修改 / 删除 / 上移 / 下移
 *  - 浏览文件按钮（弹出文件选择对话框）
 *  - 异步显示（不阻塞调用方）
 */
class ShortcutEditDialog {
public:
    static void show(HINSTANCE hInst);
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

private:
    static void refreshList(HWND hList);
    static void loadSelectionToForm(HWND hwnd, int selIndex);
    static void applyFormToManager(HWND hwnd, int selIndex);
    static void clearForm(HWND hwnd);
    static LPCWSTR kClassName;
    static DWORD WINAPI threadProc(LPVOID param);
};
