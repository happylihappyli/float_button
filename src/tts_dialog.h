// tts_dialog.h - TTS（文本转语音）朗读对话框
#pragma once

#include <windows.h>

/**
 * TTS 朗读对话框
 *  - 基于 Microsoft SAPI (ISpVoice) 实现
 *  - 可编辑文本，支持播放 / 暂停 / 继续 / 停止
 *  - 一键"读取剪贴板"把当前剪贴板内容填入文本框
 *  - 语速通过 5 个等级按钮选择（很慢 / 慢 / 正常 / 快 / 很快）
 *  - 音量通过滑块调节
 *  - 工具栏：字体设置 / 翻译（调用 LLM）/ LLM 设置
 *  - 异步显示：调用后立即返回，窗口独立线程运行
 *  - 全局单例：同时只允许打开一个 TTS 对话框
 *  - 窗口可最大化，控件跟随窗口尺寸变化
 */
class TtsDialog {
public:
    // 异步显示（不阻塞调用方）；自动读取当前剪贴板
    static void show(HINSTANCE hInst);
    // 异步显示并预填文本
    static void showWithText(HINSTANCE hInst, const wchar_t* text);
    // 关闭已打开的对话框（如有）
    static void closeExisting();

    // 窗口过程
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

    // 当前是否在朗读 / 暂停中
    static bool isSpeaking();
    static bool isPaused();

    // 公共 TTS 控制
    static void speakText(const wchar_t* text);
    static void pauseSpeak();
    static void resumeSpeak();
    static void stopSpeak();

    // 子类化回调（自绘按钮 + 防闪烁）。声明为 public
    // 以便 .cpp 内的工厂函数能直接传给 SetWindowSubclass。
    static LRESULT CALLBACK btnSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK rateBtnSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

private:
    // 构建窗口（首次创建）
    static void buildWindow(HWND hwnd, HINSTANCE hInst);
    // 响应 WM_SIZE 重设控件尺寸（最大化/拉伸时让编辑框占满）
    static void relayout(HWND hwnd);
    // 应用配置到 UI（包括字体设置）
    static void applyConfigToUI(HWND hwnd);
    static void saveConfigFromUI(HWND hwnd);
    static void setRateFromButton(HWND hwnd, int rateBtnIdx);

    // 字体按钮 / 翻译按钮 / LLM 设置按钮的回调
    static void onFontButton(HWND hwnd);
    static void onTranslateButton(HWND hwnd);
    static void onLlmSettingsButton(HWND hwnd);

    static LPCWSTR kClassName;
    static DWORD WINAPI threadProc(LPVOID param);

    static HWND hwnd_;  // 当前窗口句柄
};