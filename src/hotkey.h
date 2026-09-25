// hotkey.h - 全局快捷键
#pragma once

#include <windows.h>

class GlobalHotkey {
public:
    static GlobalHotkey& instance();
    // 注销旧的
    void unregister();
    // 切换悬浮窗显隐
    void toggle();
    // 重新注册（用当前 config 的值）
    bool reregister(HWND ownerWnd);
    // 获取最后一次错误码
    DWORD getLastError() const;
    // 消息 ID
    static constexpr UINT WM_HOTKEY_PRESSED = WM_USER + 200;

private:
    GlobalHotkey() = default;
    HWND owner_ = nullptr;
    int hotkeyId_ = 1;
    bool registered_ = false;
    DWORD lastError_ = 0;
};
