// hotkey.cpp
#include "hotkey.h"
#include "config.h"
#include "float_window.h"

GlobalHotkey& GlobalHotkey::instance() {
    static GlobalHotkey h;
    return h;
}

void GlobalHotkey::unregister() {
    if (registered_ && owner_) {
        UnregisterHotKey(owner_, hotkeyId_);
    }
    registered_ = false;
}

bool GlobalHotkey::reregister(HWND ownerWnd) {
    // 先注销旧的
    unregister();

    owner_ = ownerWnd;
    UINT mods = AppConfig::instance().hotkeyModifiers();
    UINT vk = AppConfig::instance().hotkeyVk();

    // 去掉 NOREPEAT 标志（有些系统不支持）
    mods = mods & 0x000F;

    BOOL ok = RegisterHotKey(ownerWnd, hotkeyId_, mods, vk);
    if (!ok) {
        lastError_ = GetLastError();
    } else {
        lastError_ = 0;
    }
    registered_ = (ok != FALSE);
    return registered_;
}

DWORD GlobalHotkey::getLastError() const {
    return lastError_;
}

void GlobalHotkey::toggle() {
    FloatWindow::instance().toggle();
}
