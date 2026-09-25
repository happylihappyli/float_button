// window_helper.cpp - 窗口辅助函数实现
#include "window_helper.h"
#include <algorithm>

namespace WinHelper {

void getWorkArea(RECT& out) {
    SystemParametersInfo(SPI_GETWORKAREA, 0, &out, 0);
}

bool isAtLeftEdge(const RECT& rc) {
    RECT work;
    getWorkArea(work);
    return rc.left <= work.left + 5;
}

bool isAtRightEdge(const RECT& rc) {
    RECT work;
    getWorkArea(work);
    return rc.right >= work.right - 5;
}

void snapToEdge(HWND hwnd, int threshold) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    RECT work;
    getWorkArea(work);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int newX = rc.left;
    int newY = rc.top;

    // 水平吸附
    if (abs(rc.left - work.left) < threshold) {
        newX = work.left;
    } else if (abs(rc.right - work.right) < threshold) {
        newX = work.right - width;
    }
    // 垂直吸附
    if (abs(rc.top - work.top) < threshold) {
        newY = work.top;
    } else if (abs(rc.bottom - work.bottom) < threshold) {
        newY = work.bottom - height;
    }

    // 限制在工作区内
    if (newX < work.left) newX = work.left;
    if (newX > work.right - width) newX = work.right - width;
    if (newY < work.top) newY = work.top;
    if (newY > work.bottom - height) newY = work.bottom - height;

    SetWindowPos(hwnd, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void autoHideIfAtEdge(HWND hwnd, POINT cursorPos) {
    RECT rc;
    GetWindowRect(hwnd, &rc);

    if (isAtLeftEdge(rc) || isAtRightEdge(rc)) {
        // 鼠标在窗口内，不隐藏
        if (PtInRect(&rc, cursorPos)) return;
        // 贴边后缩进到屏幕外
        int newX = rc.left;
        if (isAtLeftEdge(rc)) {
            newX = rc.left - (rc.right - rc.left) + 5;  // 留 5px 露出
        } else if (isAtRightEdge(rc)) {
            newX = rc.right - 5;
        }
        SetWindowPos(hwnd, nullptr, newX, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void showIfHidden(HWND hwnd, POINT cursorPos) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    RECT work;
    getWorkArea(work);

    // 检查是否处于贴边隐藏状态（窗口大部分在屏幕外）
    bool wasHiddenLeft = (rc.right < work.left + 10 && rc.left < work.left);
    bool wasHiddenRight = (rc.left > work.right - 10 && rc.right > work.right);

    if (!wasHiddenLeft && !wasHiddenRight) return;

    // 鼠标靠近贴边区域
    bool cursorNear = (cursorPos.x < 20) || (cursorPos.x > work.right - 20);
    if (cursorNear) {
        int newX = rc.left;
        if (wasHiddenLeft) newX = work.left;
        else if (wasHiddenRight) newX = work.right - (rc.right - rc.left);
        SetWindowPos(hwnd, nullptr, newX, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        ShowWindow(hwnd, SW_SHOW);
    }
}

void fadeIn(HWND hwnd, int durationMs) {
    // 使用 AnimateWindow 实现简单的滑动+淡入效果
    AnimateWindow(hwnd, durationMs, AW_BLEND | AW_ACTIVATE);
}

void fadeOut(HWND hwnd, int durationMs) {
    AnimateWindow(hwnd, durationMs, AW_BLEND | AW_HIDE);
}

bool copyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    memcpy(GlobalLock(hMem), text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

std::wstring utf8ToUtf16(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
    return out;
}

std::string utf16ToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
}

}  // namespace WinHelper
