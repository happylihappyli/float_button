// phrase_edit_dialog.h - 常用语编辑窗口
#pragma once

#include <windows.h>

/**
 * 常用语编辑窗口
 *  - 列表显示所有常用语
 *  - 添加 / 编辑 / 删除 / 上移 / 下移 / 恢复默认
 *  - 用 CreateWindow 手动创建
 *  - 独立顶级窗口（不阻塞按钮窗口）
 *  - 异步显示：调用后立即返回，窗口独立运行
 */
class PhraseEditDialog {
public:
    // 异步显示（不阻塞调用方）
    static void show(HINSTANCE hInst);
    // 窗口过程
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

private:
    static void refreshList(HWND hList);
    static HWND hList_, hEdit_;
    static LPCWSTR kClassName;
    // 工作线程（运行窗口消息循环）
    static DWORD WINAPI threadProc(LPVOID param);
};

