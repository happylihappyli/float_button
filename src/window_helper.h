// window_helper.h - 窗口辅助函数
#pragma once

#include <windows.h>
#include <string>

namespace WinHelper {

// 获取主显示器工作区大小（不含任务栏）
void getWorkArea(RECT& out);

// 屏幕边缘吸附：将窗口贴到最近的屏幕边缘
void snapToEdge(HWND hwnd, int threshold = 30);

// 边缘自动隐藏：如果窗口贴边，鼠标离开后延迟隐藏
void autoHideIfAtEdge(HWND hwnd, POINT cursorPos);

// 取消隐藏（鼠标靠近贴边窗口时）
void showIfHidden(HWND hwnd, POINT cursorPos);

// 淡入动画（使用 AnimateWindow，避免破坏普通窗口属性）
void fadeIn(HWND hwnd, int durationMs = 200);

// 淡出动画
void fadeOut(HWND hwnd, int durationMs = 200);

// 复制文本到剪贴板
bool copyToClipboard(const std::wstring& text);

// UTF-8 <-> UTF-16 转换
std::wstring utf8ToUtf16(const std::string& s);
std::string utf16ToUtf8(const std::wstring& s);

// 检查窗口是否在屏幕左/右边缘
bool isAtLeftEdge(const RECT& rc);
bool isAtRightEdge(const RECT& rc);

}  // namespace WinHelper
