// config.h - 应用配置（快捷键、自启动、TTS 等）
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

    // 开机自动启动
    bool autoStart() const { return autoStart_; }
    void setAutoStart(bool v) { autoStart_ = v; }

    // 剪贴板变化时自动朗读
    bool autoSpeakClipboard() const { return autoSpeakClipboard_; }
    void setAutoSpeakClipboard(bool v) { autoSpeakClipboard_ = v; }

    // TTS 语速 (-10 ~ 10, 0 默认)
    int ttsRate() const { return ttsRate_; }
    void setTtsRate(int v) { ttsRate_ = (v < -10 ? -10 : (v > 10 ? 10 : v)); }

    // TTS 音量 (0 ~ 100)
    int ttsVolume() const { return ttsVolume_; }
    void setTtsVolume(int v) { ttsVolume_ = (v < 0 ? 0 : (v > 100 ? 100 : v)); }

    // 朗读文本框的字体名（如 "Microsoft YaHei UI"）
    std::wstring textFontName() const { return textFontName_; }
    void setTextFontName(const std::wstring& v) { textFontName_ = v; }

    // 朗读文本框的字体大小（pt）
    int textFontSize() const { return textFontSize_; }
    void setTextFontSize(int v) { textFontSize_ = (v < 8 ? 8 : (v > 48 ? 48 : v)); }

    // LLM API base url（如 https://api.openai.com/v1）
    std::wstring llmBaseUrl() const { return llmBaseUrl_; }
    void setLlmBaseUrl(const std::wstring& v) { llmBaseUrl_ = v; }

    // LLM API key
    std::wstring llmApiKey() const { return llmApiKey_; }
    void setLlmApiKey(const std::wstring& v) { llmApiKey_ = v; }

    // LLM 模型名（如 gpt-4o-mini, qwen-turbo 等）
    std::wstring llmModel() const { return llmModel_; }
    void setLlmModel(const std::wstring& v) { llmModel_ = v; }

    // 翻译目标语言（ISO 代码或自然语言描述）
    std::wstring translateTargetLang() const { return translateTargetLang_; }
    void setTranslateTargetLang(const std::wstring& v) { translateTargetLang_ = v; }

    // 字符串描述
    std::wstring hotkeyDescription() const;

private:
    AppConfig() = default;
    std::wstring getConfigPath() const;

    UINT hotkeyModifiers_ = MOD_WIN | MOD_SHIFT | MOD_NOREPEAT;  // 默认 Win+Shift+F
    UINT hotkeyVk_ = 'F';
    int floatButtonAlpha_ = 220;

    bool autoStart_ = false;             // 开机自启动
    bool autoSpeakClipboard_ = false;    // 剪贴板变化自动朗读
    int ttsRate_ = 0;                    // TTS 语速
    int ttsVolume_ = 100;                // TTS 音量

    std::wstring textFontName_ = L"Microsoft YaHei UI";  // 朗读文本框字体
    int textFontSize_ = 14;                                // 朗读文本框字号

    std::wstring llmBaseUrl_ = L"https://api.openai.com/v1";  // LLM base URL
    std::wstring llmApiKey_ = L"";                            // LLM API key
    std::wstring llmModel_ = L"gpt-4o-mini";                  // LLM 模型
    std::wstring translateTargetLang_ = L"中文";              // 翻译目标语言
};