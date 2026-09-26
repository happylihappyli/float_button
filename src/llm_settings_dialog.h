// llm_settings_dialog.h - LLM 设置对话框
#pragma once

#include <windows.h>

/**
 * LLM 设置对话框
 *  - 设置 Base URL / API Key / 模型名 / 翻译目标语言
 *  - 测试连接按钮（发送一个简单 ping 请求）
 *  - 异步显示：独立线程运行
 */
class LlmSettingsDialog {
public:
    static void show(HINSTANCE hInst);
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

private:
    static void loadToUI(HWND hwnd);
    static void saveFromUI(HWND hwnd);

    static LPCWSTR kClassName;
    static DWORD WINAPI threadProc(LPVOID param);

    // 测试连接（在按钮点击处发起，会弹窗显示结果）
    static void doTestConnection(HWND hwnd);
};