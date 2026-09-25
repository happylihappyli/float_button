// config.h - 应用配置（快捷键等）
#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

class AppConfig {
public:
    static AppConfig& instance();

    void load();
    void save();

    // 快捷键：修饰键（MOD_CONTROL/MOD_ALT/MOD_SHIFT/MOD_WIN）
    UINT hotkeyModifiers() const { return hotkeyModifiers_; }
    UINT hotkeyVk() const { return hotkeyVk_; }
    void setHotkey(UINT mods, UINT vk) { hotkeyModifiers_ = mods; hotkeyVk_ = vk; }

    // 浮按钮透明度 0-255
    int floatButtonAlpha() const { return floatButtonAlpha_; }
    void setFloatButtonAlpha(int a) { floatButtonAlpha_ = (a < 50 ? 50 : (a > 255 ? 255 : a)); }

    // 字符串描述
    std::wstring hotkeyDescription() const;

private:
    AppConfig() = default;
    std::wstring getConfigPath() const;

    UINT hotkeyModifiers_ = MOD_WIN | MOD_SHIFT | MOD_NOREPEAT;  // 默认 Win+Shift+F
    UINT hotkeyVk_ = 'F';
    int floatButtonAlpha_ = 220;
};
