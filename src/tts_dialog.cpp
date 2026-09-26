// tts_dialog.cpp - TTS（文本转语音）朗读对话框实现
// 使用 Microsoft SAPI (ISpVoice) 进行朗读，支持暂停 / 继续 / 停止
// 使用 GDI+ 自绘按钮，状态机式防闪烁；窗口支持最大化，控件自适应大小
// 工具栏：字体设置 / 翻译（调 LLM）/ LLM 设置
#include "tts_dialog.h"
#include "config.h"
#include "window_helper.h"
#include "llm_client.h"
#include "llm_settings_dialog.h"
#include <sapi.h>     // SAPI 头文件
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdlib>    // std::abs
#include <climits>    // INT_MAX

#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

using namespace Gdiplus;

LPCWSTR TtsDialog::kClassName = L"TtsDialogClass";
HWND    TtsDialog::hwnd_      = nullptr;

extern void logMsg(const wchar_t* msg);

namespace {
// ============== 控件 ID ==============
constexpr int kEditTextId       = 1001;  // 编辑文本框
constexpr int kBtnSpeakId       = 1010;
constexpr int kBtnPauseId       = 1011;
constexpr int kBtnResumeId      = 1012;
constexpr int kBtnStopId        = 1013;
constexpr int kBtnClipboardId   = 1014;
constexpr int kBtnPasteId       = 1015;
constexpr int kBtnClearId       = 1016;
constexpr int kBtnCloseId       = 1017;
constexpr int kSliderVolumeId   = 1021;
constexpr int kStaticVolumeId   = 1031;
constexpr int kStaticStatusId   = 1032;

// 工具栏
constexpr int kBtnFontId        = 1040;  // 字体设置
constexpr int kBtnTranslateId   = 1041;  // 翻译
constexpr int kBtnLlmSettingsId = 1042;  // LLM 设置

// 语速按钮
constexpr int kRateBtnBase      = 1050;
constexpr int kRateBtnCount     = 5;
constexpr int kRateValues[kRateBtnCount]  = { -5, -2, 0, 3, 7 };
constexpr LPCWSTR kRateLabels[kRateBtnCount] = {
    L"很慢 🐢", L"慢", L"正常", L"快", L"很快 🐇"
};

// 按钮分类（配色）
constexpr int kBtnClassPlain    = 0;
constexpr int kBtnClassPrimary  = 1;
constexpr int kBtnClassSuccess  = 2;
constexpr int kBtnClassWarn     = 3;
constexpr int kBtnClassDanger   = 4;
constexpr int kBtnClassTool     = 5;  // 工具栏按钮（蓝紫色）

// ============== 状态 ==============
ISpVoice* g_voice = nullptr;
ULONG_PTR g_gdiplusToken = 0;

// 记录所有控件句柄，方便 relayout
struct LayoutState {
    HWND hEdit = nullptr;
    HWND hToolbar[8] = {};   // 工具栏 8 个按钮
    int  toolbarCount = 0;
    HWND hRateBtns[kRateBtnCount] = {};
    HWND hSpeakBtn = nullptr;
    HWND hActionBtns[8] = {}; // 操作按钮（朗读/暂停/继续/停止/读剪贴板/追加/清空/关闭）
    int  actionBtnCount = 0;
    HWND hVolumeSlider = nullptr;
    HWND hVolumeLabel = nullptr;
    HWND hStatusLabel = nullptr;
    HWND hHelpLabel = nullptr;
};
LayoutState g_layout;

// 布局常量（相对客户区坐标，buildWindow 时记录原始位置，relayout 时根据窗口大小调整）
// 原始布局（窗口宽 720, 高 640）
constexpr int kMargin       = 14;
constexpr int kHelpH        = 22;
constexpr int kToolbarH     = 32;
constexpr int kEditH        = 300;  // 文本框原始高度（窗口最大化时拉伸）
constexpr int kBtnRow1H     = 36;
constexpr int kBtnRow2H     = 36;
constexpr int kRateRowH     = 28;
constexpr int kVolRowH      = 30;

int getEditBottom(HWND hwnd) {
    // 文本框底部 y = 工具栏 + 编辑框高度（toolbar 紧跟在编辑框下）
    RECT rc; GetClientRect(hwnd, &rc);
    int clientH = rc.bottom - rc.top;
    // 总高度 = 顶部说明 + 编辑框 + 工具栏 + 按钮行1 + 按钮行2 + 语速 + 音量
    int usedBottom = kMargin + kHelpH + 6
                   + kEditH + 8 + kToolbarH
                   + 8 + kBtnRow1H + 8 + kBtnRow2H + 12 + kRateRowH + 8 + kVolRowH;
    if (clientH > usedBottom) {
        // 剩余空间匀给编辑框
        return kEditH + (clientH - usedBottom);
    }
    return kEditH;
}

// ============== SAPI 初始化 ==============

void initVoice() {
    if (g_voice) return;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
        logMsg(L"[TTS] CoInitializeEx FAILED");
    }
    HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&g_voice);
    if (FAILED(hr)) {
        logMsg(L"[TTS] CoCreateInstance(ISpVoice) FAILED");
        g_voice = nullptr;
        return;
    }
    AppConfig& cfg = AppConfig::instance();
    g_voice->SetRate(cfg.ttsRate());
    USHORT vol = (USHORT)((cfg.ttsVolume() * 65535) / 100);
    g_voice->SetVolume(vol);
}

void releaseVoice() {
    if (g_voice) {
        g_voice->Release();
        g_voice = nullptr;
    }
}

// GDI+ 启动
void initGdiPlus() {
    if (g_gdiplusToken) return;
    GdiplusStartupInput si;
    GdiplusStartup(&g_gdiplusToken, &si, nullptr);
}
void shutdownGdiPlus() {
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

std::wstring getClipboardText() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) out = p;
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

void setStatusText(HWND hwnd, const wchar_t* text) {
    if (g_layout.hStatusLabel) SetWindowTextW(g_layout.hStatusLabel, text);
    logMsg(text);
}

// ============== 朗读控制 ==============

void doSpeak(HWND hwnd, const std::wstring& text) {
    if (text.empty()) { setStatusText(hwnd, L"⚠ 文本为空"); return; }
    initVoice();
    if (!g_voice) {
        MessageBoxW(hwnd, L"无法初始化 SAPI 语音引擎，请确认系统已安装语音组件。",
                    L"TTS 错误", MB_ICONERROR);
        return;
    }
    AppConfig& cfg = AppConfig::instance();
    g_voice->SetRate(cfg.ttsRate());
    USHORT vol = (USHORT)((cfg.ttsVolume() * 65535) / 100);
    g_voice->SetVolume(vol);
    HRESULT hr = g_voice->Speak(text.c_str(), SPF_ASYNC | SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, nullptr);
    if (FAILED(hr)) setStatusText(hwnd, L"❌ Speak FAILED");
    else            setStatusText(hwnd, L"🔊 正在朗读...");
}

void doPause()  { if (g_voice) g_voice->Pause(); }
void doResume() { if (g_voice) g_voice->Resume(); }
void doStop() {
    if (!g_voice) return;
    g_voice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
}

// ============== 自绘按钮（防闪烁） ==============

struct BtnColors {
    Color normalStart, normalEnd;
    Color hoverStart,  hoverEnd;
    Color pressedStart, pressedEnd;
    Color text;
    Color border;
};

BtnColors getColorsByClass(int cls) {
    switch (cls) {
    case kBtnClassPrimary:
        return {
            Color(255, 33, 150, 243), Color(255, 25, 118, 210),
            Color(255, 66, 165, 245), Color(255, 33, 150, 243),
            Color(255, 25, 118, 210), Color(255, 13, 71, 161),
            Color(255, 255, 255, 255), Color(255, 10, 58, 131)
        };
    case kBtnClassSuccess:
        return {
            Color(255, 76, 175, 80), Color(255, 56, 142, 60),
            Color(255, 102, 187, 106), Color(255, 76, 175, 80),
            Color(255, 56, 142, 60), Color(255, 46, 125, 50),
            Color(255, 255, 255, 255), Color(255, 27, 94, 32)
        };
    case kBtnClassWarn:
        return {
            Color(255, 255, 152, 0), Color(255, 245, 124, 0),
            Color(255, 255, 167, 38), Color(255, 255, 152, 0),
            Color(255, 245, 124, 0), Color(255, 230, 81, 0),
            Color(255, 255, 255, 255), Color(255, 191, 54, 12)
        };
    case kBtnClassDanger:
        return {
            Color(255, 244, 67, 54), Color(255, 211, 47, 47),
            Color(255, 246, 112, 96), Color(255, 244, 67, 54),
            Color(255, 211, 47, 47), Color(255, 198, 40, 40),
            Color(255, 255, 255, 255), Color(255, 183, 28, 28)
        };
    case kBtnClassTool:  // 蓝紫色（工具栏）
        return {
            Color(255, 124, 77, 184),  Color(255, 96, 53, 156),
            Color(255, 149, 117, 205), Color(255, 124, 77, 184),
            Color(255, 96, 53, 156),   Color(255, 74, 35, 130),
            Color(255, 255, 255, 255), Color(255, 56, 28, 112)
        };
    case kBtnClassPlain:
    default:
        return {
            Color(255, 245, 245, 245), Color(255, 225, 225, 225),
            Color(255, 230, 230, 230), Color(255, 200, 200, 200),
            Color(255, 215, 215, 215), Color(255, 190, 190, 190),
            Color(255, 50, 50, 50),    Color(255, 180, 180, 180)
        };
    }
}

// 绘制圆角矩形按钮（cls 普通按钮分类）
// 重要：必须用 BeginPaint/EndPaint，而不是 GetDC/ReleaseDC！
// 因为 owner-draw 按钮的 WM_PAINT 必须通过 BeginPaint 通知系统
// "已绘制完毕"。如果只 GetDC，RDW_INVALIDATE 标志不会被清掉，
// 系统会立刻再次发 WM_PAINT → 死循环 → 闪动。
void paintCustomButton(HWND hBtn, int cls, bool hovered, bool pressed) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hBtn, &ps);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    RECT rc;
    GetClientRect(hBtn, &rc);
    REAL w = (REAL)(rc.right - rc.left);
    REAL h = (REAL)(rc.bottom - rc.top);

    BtnColors c = getColorsByClass(cls);
    Color start, end;
    if (pressed)      { start = c.pressedStart; end = c.pressedEnd; }
    else if (hovered) { start = c.hoverStart;   end = c.hoverEnd;   }
    else              { start = c.normalStart;   end = c.normalEnd;   }

    GraphicsPath path;
    REAL r = 8.0f;
    path.AddArc(0.0f, 0.0f, r*2, r*2, 180, 90);
    path.AddArc(w - r*2, 0.0f, r*2, r*2, 270, 90);
    path.AddArc(w - r*2, h - r*2, r*2, r*2, 0.0f, 90);
    path.AddArc(0.0f, h - r*2, r*2, r*2, 90, 90);
    path.CloseAllFigures();

    RectF rect(0.0f, 0.0f, w, h);
    LinearGradientBrush brush(rect, start, end, LinearGradientModeVertical);
    g.FillPath(&brush, &path);

    Pen pen(c.border, 1.2f);
    g.DrawPath(&pen, &path);

    wchar_t text[64];
    GetWindowTextW(hBtn, text, 64);
    FontFamily ff(L"Microsoft YaHei UI");
    Font font(&ff, (cls == kBtnClassPlain ? 11.0f : 12.0f),
              (cls == kBtnClassPlain ? FontStyleRegular : FontStyleBold),
              UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    SolidBrush tb(c.text);
    g.DrawString(text, -1, &font, rect, &fmt, &tb);

    EndPaint(hBtn, &ps);
}

// 绘制语速按钮（区分选中）
// 必须 BeginPaint/EndPaint，理由同 paintCustomButton
void paintRateButton(HWND hBtn, bool selected, bool hovered, bool pressed) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hBtn, &ps);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    RECT rc;
    GetClientRect(hBtn, &rc);
    REAL w = (REAL)(rc.right - rc.left);
    REAL h = (REAL)(rc.bottom - rc.top);

    GraphicsPath path;
    REAL r = 6.0f;
    path.AddArc(0.0f, 0.0f, r*2, r*2, 180, 90);
    path.AddArc(w - r*2, 0.0f, r*2, r*2, 270, 90);
    path.AddArc(w - r*2, h - r*2, r*2, r*2, 0.0f, 90);
    path.AddArc(0.0f, h - r*2, r*2, r*2, 90, 90);
    path.CloseAllFigures();

    RectF rect(0.0f, 0.0f, w, h);
    Color start, end, border, textColor;
    if (selected) {
        start = Color(255, 33, 150, 243);
        end   = Color(255, 25, 118, 210);
        border = Color(255, 13, 71, 161);
        textColor = Color(255, 255, 255, 255);
    } else if (pressed) {
        start = Color(255, 25, 118, 210);
        end   = Color(255, 13, 71, 161);
        border = Color(255, 10, 58, 131);
        textColor = Color(255, 255, 255, 255);
    } else if (hovered) {
        start = Color(255, 235, 245, 255);
        end   = Color(255, 200, 220, 250);
        border = Color(255, 100, 180, 240);
        textColor = Color(255, 33, 100, 180);
    } else {
        start = Color(255, 245, 247, 250);
        end   = Color(255, 232, 236, 240);
        border = Color(255, 200, 215, 230);
        textColor = Color(255, 70, 70, 70);
    }

    LinearGradientBrush brush(rect, start, end, LinearGradientModeVertical);
    g.FillPath(&brush, &path);
    Pen pen(border, 1.0f);
    g.DrawPath(&pen, &path);

    wchar_t text[64];
    GetWindowTextW(hBtn, text, 64);
    FontFamily ff(L"Microsoft YaHei UI");
    Font font(&ff, 11.0f, selected ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    SolidBrush tb(textColor);
    g.DrawString(text, -1, &font, rect, &fmt, &tb);

    EndPaint(hBtn, &ps);
}

}  // namespace

// ============== 公共接口 ==============

bool TtsDialog::isSpeaking() {
    if (!g_voice) return false;
    SPVOICESTATUS s;
    return SUCCEEDED(g_voice->GetStatus(&s, nullptr)) && s.dwRunningState == 1;
}
bool TtsDialog::isPaused() {
    if (!g_voice) return false;
    SPVOICESTATUS s;
    return SUCCEEDED(g_voice->GetStatus(&s, nullptr)) && s.dwRunningState == 2;
}
void TtsDialog::speakText(const wchar_t* text) {
    if (!text) return;
    initVoice();
    if (!g_voice) return;
    g_voice->Speak(text, SPF_ASYNC | SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, nullptr);
}
void TtsDialog::pauseSpeak()  { if (g_voice) g_voice->Pause(); }
void TtsDialog::resumeSpeak() { if (g_voice) g_voice->Resume(); }
void TtsDialog::stopSpeak() {
    if (!g_voice) return;
    g_voice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
}

void TtsDialog::closeExisting() {
    if (hwnd_ && IsWindow(hwnd_)) PostMessage(hwnd_, WM_CLOSE, 0, 0);
}

void TtsDialog::show(HINSTANCE hInst) {
    std::wstring txt = getClipboardText();
    showWithText(hInst, txt.c_str());
}

void TtsDialog::showWithText(HINSTANCE hInst, const wchar_t* text) {
    HWND existing = FindWindowW(kClassName, L"TTS 朗读 - 文本转语音");
    if (existing) {
        SetForegroundWindow(existing);
        if (text && text[0]) {
            HWND hEdit = GetDlgItem(existing, kEditTextId);
            if (hEdit) {
                int len = GetWindowTextLengthW(hEdit);
                SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
                std::wstring prefix = (len > 0) ? L"\n" : L"";
                SendMessageW(hEdit, EM_REPLACESEL, (WPARAM)TRUE,
                             (LPARAM)(prefix + std::wstring(text)).c_str());
            }
        }
        return;
    }

    struct ThreadParam { HINSTANCE hInst; std::wstring text; };
    auto* p = new ThreadParam;
    p->hInst = hInst;
    p->text  = text ? std::wstring(text) : std::wstring();
    HANDLE hThread = CreateThread(nullptr, 0, threadProc, (LPVOID)p, 0, nullptr);
    if (hThread) CloseHandle(hThread);
    else delete p;
}

// ============== 子类化（带状态机、防闪烁） ==============
//
// 关键：用 GWLP_USERDATA 存三个状态值（位图）：
//   bit 0 = hovered
//   bit 1 = pressed
//   bit 2 = mouse-tracked（已 TME_LEAVE 注册）
//   bit 3 = selected（仅语速按钮用）
// 在 WM_PAINT 时根据当前鼠标真实状态计算期望状态，仅当与上次绘制状态不同时才真正绘制。
// 这样按钮"静止"时不会被系统或鼠标小幅移动反复触发重绘 → 不闪烁。

static void updateBtnState(HWND hBtn, bool hovered, bool pressed) {
    LONG_PTR old = GetWindowLongPtr(hBtn, GWLP_USERDATA);
    LONG_PTR newSt = (old & ~0x3) | (hovered ? 1 : 0) | (pressed ? 2 : 0);
    if (old != newSt) {
        SetWindowLongPtr(hBtn, GWLP_USERDATA, newSt);
        InvalidateRect(hBtn, nullptr, FALSE);  // 状态变了 → 重绘
    }
}

LRESULT CALLBACK TtsDialog::btnSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass; (void)dwRefData;
    LONG_PTR cur = GetWindowLongPtr(hwnd, GWLP_USERDATA);
    bool wasHovered = (cur & 1) != 0;
    bool wasPressed = (cur & 2) != 0;
    bool tracked    = (cur & 4) != 0;
    int cls         = (int)(cur >> 8);  // 高字节存按钮分类

    switch (msg) {
    case WM_MOUSEMOVE: {
        // 注册一次性的 mouse-leave 跟踪
        if (!tracked) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, cur | 4);
        }
        // 仅在悬停状态变化时更新（避免持续 MOUSEMOVE 引发反复重绘）
        if (!wasHovered || wasPressed) {
            updateBtnState(hwnd, true, false);
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, cur & ~0x7);  // 清 tracked+hover+pressed
        updateBtnState(hwnd, false, false);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        updateBtnState(hwnd, true, true);
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        updateBtnState(hwnd, true, false);
        HWND parent = GetParent(hwnd);
        if (parent) {
            int id = GetDlgCtrlID(hwnd);
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)hwnd);
        }
        return 0;
    }
    case WM_PAINT: {
        paintCustomButton(hwnd, cls, wasHovered, wasPressed);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, btnSubclassProc, uIdSubclass);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK TtsDialog::rateBtnSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass; (void)dwRefData;
    LONG_PTR cur = GetWindowLongPtr(hwnd, GWLP_USERDATA);
    bool wasHovered = (cur & 1) != 0;
    bool wasPressed = (cur & 2) != 0;
    bool tracked    = (cur & 4) != 0;
    bool selected   = (cur & 8) != 0;

    switch (msg) {
    case WM_MOUSEMOVE:
        if (!tracked) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, cur | 4);
        }
        if (!wasHovered || wasPressed) {
            LONG_PTR newSt = (cur & ~0x3) | 1;
            if (newSt != cur) {
                SetWindowLongPtr(hwnd, GWLP_USERDATA, newSt);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, cur & ~0x7);
        if (wasHovered || wasPressed) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        {
            LONG_PTR newSt = cur | 3;  // hover+pressed
            if (newSt != cur) {
                SetWindowLongPtr(hwnd, GWLP_USERDATA, newSt);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        {
            LONG_PTR newSt = (cur & ~2) | 1;  // pressed=0, hover=1
            if (newSt != cur) {
                SetWindowLongPtr(hwnd, GWLP_USERDATA, newSt);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        {
            HWND parent = GetParent(hwnd);
            if (parent) {
                int id = GetDlgCtrlID(hwnd);
                SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)hwnd);
            }
        }
        return 0;
    case WM_PAINT:
        paintRateButton(hwnd, selected, wasHovered, wasPressed);
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, rateBtnSubclassProc, uIdSubclass);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ============== 创建带子类化的按钮 ==============

// 把分类 cls 编码到 GWLP_USERDATA 高字节（不与状态位冲突）
static LONG_PTR encodeClass(int cls) {
    return ((LONG_PTR)cls) << 8;
}

HWND makeCustomBtn(HINSTANCE hInst, HWND parent, int id, LPCWSTR text, int cls,
                   int x, int y, int w, int h, HWND* outStore) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h,
        parent, (HMENU)(INT_PTR)id, hInst, nullptr);
    LONG_PTR style = GetWindowLongPtr(btn, GWL_STYLE);
    style &= ~(LONG_PTR)BS_PUSHBUTTON;
    style |= (LONG_PTR)BS_OWNERDRAW;
    SetWindowLongPtr(btn, GWL_STYLE, style);
    // 初始状态：未悬停、未按下、未跟踪；高字节存 cls
    SetWindowLongPtr(btn, GWLP_USERDATA, encodeClass(cls));
    SetWindowSubclass(btn, TtsDialog::btnSubclassProc, 0, 0);
    if (outStore) *outStore = btn;
    return btn;
}

HWND makeRateBtn(HINSTANCE hInst, HWND parent, int id, LPCWSTR text,
                 int x, int y, int w, int h, HWND* outStore) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h,
        parent, (HMENU)(INT_PTR)id, hInst, nullptr);
    LONG_PTR style = GetWindowLongPtr(btn, GWL_STYLE);
    style &= ~(LONG_PTR)BS_PUSHBUTTON;
    style |= (LONG_PTR)BS_OWNERDRAW;
    SetWindowLongPtr(btn, GWL_STYLE, style);
    SetWindowLongPtr(btn, GWLP_USERDATA, 0);  // selected 标志后续由 setRateFromButton 设置
    SetWindowSubclass(btn, TtsDialog::rateBtnSubclassProc, 0, 0);
    if (outStore) *outStore = btn;
    return btn;
}

// ============== 构建窗口 ==============

void TtsDialog::buildWindow(HWND hwnd, HINSTANCE hInst) {
    // 清空旧的 layout 状态（窗口可能被复用，本实现里只有一次）
    ZeroMemory(&g_layout, sizeof(g_layout));

    int x = kMargin;
    int clientW = 760 - 2 * kMargin;  // 内部宽度

    // 顶部说明
    g_layout.hHelpLabel = CreateWindowExW(0, L"STATIC",
        L"💡 编辑文本后点「朗读」；可随时「暂停」后「继续」。",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, 10, clientW, kHelpH, hwnd, nullptr, hInst, nullptr);

    int editY = 10 + kHelpH + 6;

    // 文本编辑框
    g_layout.hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        ES_LEFT | ES_MULTILINE | ES_WANTRETURN |
        ES_AUTOVSCROLL | ES_NOHIDESEL,
        x, editY, clientW, kEditH, hwnd, (HMENU)kEditTextId, hInst, nullptr);

    int toolbarY = editY + kEditH + 8;
    int bw = 110, bh = kToolbarH, gap = 8;

    // 工具栏（3 个按钮：字体 / 翻译 / LLM 设置）
// 注意：必须使用 makeCustomBtn 而不是裸 CreateWindowExW + BS_PUSHBUTTON，
// 否则系统会画一次按钮 + 子类化又画一次 → 闪动。
    g_layout.toolbarCount = 3;
    {
        int tx = x;
        makeCustomBtn(hInst, hwnd, kBtnFontId,        L"🔤 字体设置",
                      kBtnClassTool, tx, toolbarY, bw, bh, &g_layout.hToolbar[0]);
        tx += bw + gap;
        makeCustomBtn(hInst, hwnd, kBtnTranslateId,   L"🌐 LLM 翻译",
                      kBtnClassTool, tx, toolbarY, bw, bh, &g_layout.hToolbar[1]);
        tx += bw + gap;
        makeCustomBtn(hInst, hwnd, kBtnLlmSettingsId, L"⚙ LLM 设置",
                      kBtnClassPlain, tx, toolbarY, bw, bh, &g_layout.hToolbar[2]);
    }

    // 操作按钮行 1
    int row1Y = toolbarY + bh + 8;
    int bwAct = 100;
    {
        int col = x;
        auto addAct = [&](int id, LPCWSTR text, int cls, HWND* outStore) {
            makeCustomBtn(hInst, hwnd, id, text, cls, col, row1Y, bwAct, kBtnRow1H, outStore);
            col += bwAct + gap;
        };
        addAct(kBtnSpeakId,     L"▶ 朗读",       kBtnClassPrimary, &g_layout.hActionBtns[0]);
        addAct(kBtnPauseId,     L"⏸ 暂停",       kBtnClassWarn,    &g_layout.hActionBtns[1]);
        addAct(kBtnResumeId,    L"▶▶ 继续",      kBtnClassSuccess, &g_layout.hActionBtns[2]);
        addAct(kBtnStopId,      L"⏹ 停止",       kBtnClassDanger,  &g_layout.hActionBtns[3]);
        addAct(kBtnClipboardId, L"📋 读剪贴板",   kBtnClassPlain,   &g_layout.hActionBtns[4]);
        addAct(kBtnPasteId,     L"📌 追加剪贴板", kBtnClassPlain,   &g_layout.hActionBtns[5]);
        g_layout.actionBtnCount = 6;
    }

    // 操作按钮行 2
    int row2Y = row1Y + kBtnRow1H + gap;
    {
        int col = x;
        makeCustomBtn(hInst, hwnd, kBtnClearId, L"🗑 清空文本", kBtnClassPlain,
                      col, row2Y, bwAct, kBtnRow2H, &g_layout.hActionBtns[6]);
        g_layout.actionBtnCount = 7;
        col += bwAct + gap;

        // 状态文字（占据中间空间）
        g_layout.hStatusLabel = CreateWindowExW(0, L"STATIC", L"⏸ 就绪",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            col + 8, row2Y + 6, 380, 22, hwnd, (HMENU)kStaticStatusId, hInst, nullptr);

        // 关闭按钮贴右
        int closeX = x + 7 * bwAct + 6 * gap - bwAct;
        makeCustomBtn(hInst, hwnd, kBtnCloseId, L"✕ 关闭", kBtnClassPlain,
                      closeX, row2Y, bwAct, kBtnRow2H, &g_layout.hActionBtns[7]);
        g_layout.actionBtnCount = 8;
    }

    // 语速按钮组
    int rateY = row2Y + kBtnRow2H + 12;
    CreateWindowExW(0, L"STATIC", L"🐢 语速：",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, rateY, 64, kRateRowH, hwnd, nullptr, hInst, nullptr);

    int rateBtnW = 90, rateBtnH = kRateRowH, rateGap = 4;
    int rateBtnX = 78;
    for (int i = 0; i < kRateBtnCount; ++i) {
        HWND rb;
        makeRateBtn(hInst, hwnd, kRateBtnBase + i, kRateLabels[i],
                    rateBtnX + i * (rateBtnW + rateGap), rateY,
                    rateBtnW, rateBtnH, &rb);
        g_layout.hRateBtns[i] = rb;
    }

    // 音量行
    int volY = rateY + kRateRowH + 8;
    CreateWindowExW(0, L"STATIC", L"🔊 音量：",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, volY, 64, kVolRowH, hwnd, nullptr, hInst, nullptr);
    g_layout.hVolumeSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
        78, volY, 380, kVolRowH - 4, hwnd, (HMENU)kSliderVolumeId, hInst, nullptr);
    SendMessageW(g_layout.hVolumeSlider, TBM_SETRANGE, FALSE, MAKELONG(0, 100));

    g_layout.hVolumeLabel = CreateWindowExW(0, L"STATIC", L"100",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        466, volY, 50, kVolRowH, hwnd, (HMENU)kStaticVolumeId, hInst, nullptr);

    CreateWindowExW(0, L"STATIC",
        L"💡 自动朗读请到设置中开启",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        520, volY, 200, kVolRowH, hwnd, nullptr, hInst, nullptr);

    SetFocus(g_layout.hEdit);
}

// ============== 重新布局（最大化/拉伸） ==============

void TtsDialog::relayout(HWND hwnd) {
    if (!g_layout.hEdit) return;  // 还没构建好

    RECT rc; GetClientRect(hwnd, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;

    int x = kMargin;
    int editW = clientW - 2 * kMargin;
    int editH = getEditBottom(hwnd);  // 让编辑框占用所有剩余高度

    // 重设各控件的位置 / 大小
    int helpY = 10;
    MoveWindow(g_layout.hHelpLabel, x, helpY, editW, kHelpH, TRUE);
    int editY = helpY + kHelpH + 6;
    MoveWindow(g_layout.hEdit, x, editY, editW, editH, TRUE);

    int toolbarY = editY + editH + 8;
    int bw = 110, bh = kToolbarH, gap = 8;
    int tx = x;
    for (int i = 0; i < g_layout.toolbarCount; ++i) {
        if (g_layout.hToolbar[i]) {
            MoveWindow(g_layout.hToolbar[i], tx, toolbarY, bw, bh, TRUE);
            tx += bw + gap;
        }
    }

    int row1Y = toolbarY + bh + 8;
    int bwAct = 100;
    int col = x;
    for (int i = 0; i < g_layout.actionBtnCount; ++i) {
        if (g_layout.hActionBtns[i]) {
            MoveWindow(g_layout.hActionBtns[i], col, row1Y, bwAct, kBtnRow1H, TRUE);
            col += bwAct + gap;
        }
    }
    int row2Y = row1Y + kBtnRow1H + gap;
    // 第 7 个（清空文本）
    if (g_layout.hActionBtns[6]) {
        MoveWindow(g_layout.hActionBtns[6], x, row2Y, bwAct, kBtnRow2H, TRUE);
    }
    if (g_layout.hStatusLabel) {
        MoveWindow(g_layout.hStatusLabel, x + bwAct + gap + 8, row2Y + 6, 380, 22, TRUE);
    }
    if (g_layout.hActionBtns[7]) {
        int closeX = x + 7 * bwAct + 6 * gap - bwAct;
        MoveWindow(g_layout.hActionBtns[7], closeX, row2Y, bwAct, kBtnRow2H, TRUE);
    }

    int rateY = row2Y + kBtnRow2H + 12;
    int rateBtnW = 90, rateBtnH = kRateRowH, rateGap = 4;
    for (int i = 0; i < kRateBtnCount; ++i) {
        if (g_layout.hRateBtns[i]) {
            MoveWindow(g_layout.hRateBtns[i],
                       78 + i * (rateBtnW + rateGap), rateY,
                       rateBtnW, rateBtnH, TRUE);
        }
    }
    int volY = rateY + kRateRowH + 8;
    if (g_layout.hVolumeSlider) MoveWindow(g_layout.hVolumeSlider, 78, volY, 380, kVolRowH - 4, TRUE);
    if (g_layout.hVolumeLabel)  MoveWindow(g_layout.hVolumeLabel, 466, volY, 50, kVolRowH, TRUE);
}

// ============== 配置应用 / 字体 ==============

int findRateBtnIndex(int rate) {
    int best = 2;
    int bestDist = INT_MAX;
    for (int i = 0; i < kRateBtnCount; ++i) {
        int d = std::abs(kRateValues[i] - rate);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void TtsDialog::setRateFromButton(HWND hwnd, int idx) {
    int rate = kRateValues[idx];
    AppConfig::instance().setTtsRate(rate);
    if (g_voice) g_voice->SetRate(rate);
    for (int i = 0; i < kRateBtnCount; ++i) {
        if (g_layout.hRateBtns[i]) {
            LONG_PTR cur = GetWindowLongPtr(g_layout.hRateBtns[i], GWLP_USERDATA);
            LONG_PTR newSt = (cur & ~8) | ((i == idx) ? 8 : 0);
            if (newSt != cur) {
                SetWindowLongPtr(g_layout.hRateBtns[i], GWLP_USERDATA, newSt);
                InvalidateRect(g_layout.hRateBtns[i], nullptr, FALSE);
            }
        }
    }
    wchar_t buf[64];
    swprintf_s(buf, L"语速已切换：%s (SAPI=%d)", kRateLabels[idx], rate);
    setStatusText(hwnd, buf);
}

void TtsDialog::applyConfigToUI(HWND hwnd) {
    AppConfig& cfg = AppConfig::instance();

    // 语速按钮
    int rate = cfg.ttsRate();
    int idx = findRateBtnIndex(rate);
    for (int i = 0; i < kRateBtnCount; ++i) {
        if (g_layout.hRateBtns[i]) {
            LONG_PTR cur = GetWindowLongPtr(g_layout.hRateBtns[i], GWLP_USERDATA);
            LONG_PTR newSt = (cur & ~8) | ((i == idx) ? 8 : 0);
            SetWindowLongPtr(g_layout.hRateBtns[i], GWLP_USERDATA, newSt);
            InvalidateRect(g_layout.hRateBtns[i], nullptr, FALSE);
        }
    }

    // 音量
    if (g_layout.hVolumeSlider) {
        SendMessageW(g_layout.hVolumeSlider, TBM_SETPOS, TRUE, cfg.ttsVolume());
    }
    if (g_layout.hVolumeLabel) {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", cfg.ttsVolume());
        SetWindowTextW(g_layout.hVolumeLabel, buf);
    }

    // 字体应用到编辑框
    if (g_layout.hEdit) {
        HFONT hf = CreateFontW(
            -MulDiv(cfg.textFontSize(), GetDeviceCaps(GetDC(hwnd), LOGPIXELSY), 72),
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            cfg.textFontName().c_str());
        if (hf) {
            // 用 FALSE 让编辑框不立即重绘，避免触发 WM_PAINT 链
            SendMessageW(g_layout.hEdit, WM_SETFONT, (WPARAM)hf, FALSE);
        }
    }
}

void TtsDialog::saveConfigFromUI(HWND hwnd) {
    if (g_layout.hVolumeSlider) {
        int vol = (int)SendMessageW(g_layout.hVolumeSlider, TBM_GETPOS, 0, 0);
        AppConfig::instance().setTtsVolume(vol);
    }
    AppConfig::instance().save();
}

// ============== 工具栏按钮回调 ==============

void TtsDialog::onFontButton(HWND hwnd) {
    // 使用 ChooseFont 让用户选字体
    CHOOSEFONTW cf{};
    LOGFONTW lf{};
    AppConfig& cfg = AppConfig::instance();
    // 把当前字体填入 LOGFONT
    wcscpy_s(lf.lfFaceName, cfg.textFontName().c_str());
    lf.lfHeight = -MulDiv(cfg.textFontSize(), GetDeviceCaps(GetDC(hwnd), LOGPIXELSY), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
    cf.nFontType = SCREEN_FONTTYPE;

    if (ChooseFontW(&cf)) {
        std::wstring face = lf.lfFaceName;
        // lfHeight 是负数（向上），取其绝对值再换算回 pt
        int pt = (int)std::abs(MulDiv(lf.lfHeight, 72, GetDeviceCaps(GetDC(hwnd), LOGPIXELSY)));
        if (pt < 8) pt = 8; if (pt > 48) pt = 48;
        cfg.setTextFontName(face);
        cfg.setTextFontSize(pt);
        cfg.save();
        applyConfigToUI(hwnd);
        wchar_t msg[128];
        swprintf_s(msg, L"✅ 字体已切换：%s (%dpt)", face.c_str(), pt);
        setStatusText(hwnd, msg);
    } else {
        setStatusText(hwnd, L"字体选择已取消");
    }
}

void TtsDialog::onTranslateButton(HWND hwnd) {
    AppConfig& cfg = AppConfig::instance();

    if (cfg.llmApiKey().empty()) {
        MessageBoxW(hwnd,
            L"未配置 LLM API Key。\n请先点击「⚙ LLM 设置」填写。",
            L"提示", MB_ICONINFORMATION);
        return;
    }

    if (!g_layout.hEdit) return;
    int len = GetWindowTextLengthW(g_layout.hEdit);
    if (len <= 0) {
        setStatusText(hwnd, L"⚠ 编辑框为空");
        return;
    }
    std::wstring text;
    text.resize(len + 1);
    GetWindowTextW(g_layout.hEdit, &text[0], len + 1);
    text.resize(len);

    setStatusText(hwnd, L"🌐 正在翻译...");
    if (g_layout.hStatusLabel) UpdateWindow(g_layout.hStatusLabel);

    std::wstring target = cfg.translateTargetLang();
    std::wstring sysPrompt =
        L"你是一名专业翻译。请把用户提供的文本翻译成 " + target +
        L"。只输出翻译结果，不要任何解释、注释或前后缀。如果原文已经是目标语言，则原样返回。";

    std::wstring err;
    std::wstring resp = LlmClient::chatCompletion(
        cfg.llmBaseUrl(), cfg.llmApiKey(), cfg.llmModel(),
        sysPrompt, text, &err);

    if (!err.empty()) {
        wchar_t msg[512];
        swprintf_s(msg, L"❌ 翻译失败：\n%s", err.c_str());
        MessageBoxW(hwnd, msg, L"翻译错误", MB_ICONERROR);
        setStatusText(hwnd, L"❌ 翻译失败");
    } else if (resp.empty()) {
        setStatusText(hwnd, L"⚠ 翻译结果为空");
    } else {
        // 用翻译结果替换编辑框内容
        SetWindowTextW(g_layout.hEdit, resp.c_str());
        wchar_t buf[128];
        swprintf_s(buf, L"✅ 翻译完成：%zu 字符", resp.size());
        setStatusText(hwnd, buf);
    }
}

void TtsDialog::onLlmSettingsButton(HWND hwnd) {
    LlmSettingsDialog::show((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
}

// ============== 窗口过程 ==============

DWORD WINAPI TtsDialog::threadProc(LPVOID param) {
    struct ThreadParam { HINSTANCE hInst; std::wstring text; };
    auto* p = (ThreadParam*)param;
    HINSTANCE hInst = p->hInst;
    std::wstring presetText = p->text;
    delete p;

    initGdiPlus();
    initVoice();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        wchar_t buf[128];
        swprintf_s(buf, L"[TTS] RegisterClass FAILED, err=%u", err);
        logMsg(buf);
        return 1;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int w = 760, h = 640;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    // ========== 菜单栏 ==========
    // 菜单 ID 与 WM_COMMAND 中的按钮 ID 对齐，让所有逻辑复用同一份 case 分支
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, kBtnClipboardId, L"读取剪贴板 (&C)\tCtrl+L");
    AppendMenuW(hFileMenu, MF_STRING, kBtnPasteId,     L"追加剪贴板 (&A)");
    AppendMenuW(hFileMenu, MF_STRING, kBtnClearId,     L"清空文本 (&X)");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, kBtnCloseId,     L"退出 (&Q)\tAlt+F4");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"文件 (&F)");

    HMENU hSpeakMenu = CreatePopupMenu();
    AppendMenuW(hSpeakMenu, MF_STRING, kBtnSpeakId,  L"▶ 朗读 (&R)\tSpace");
    AppendMenuW(hSpeakMenu, MF_STRING, kBtnPauseId,  L"⏸ 暂停 (&P)");
    AppendMenuW(hSpeakMenu, MF_STRING, kBtnResumeId, L"▶▶ 继续 (&G)");
    AppendMenuW(hSpeakMenu, MF_STRING, kBtnStopId,   L"⏹ 停止 (&S)\tEsc");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hSpeakMenu, L"朗读 (&S)");

    HMENU hToolMenu = CreatePopupMenu();
    AppendMenuW(hToolMenu, MF_STRING, kBtnFontId,        L"字体设置... (&F)");
    AppendMenuW(hToolMenu, MF_STRING, kBtnTranslateId,   L"LLM 翻译当前文本... (&T)");
    AppendMenuW(hToolMenu, MF_STRING, kBtnLlmSettingsId, L"LLM 设置... (&L)");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hToolMenu, L"工具 (&T)");

    HWND hwnd = CreateWindowExW(
        0,
        kClassName, L"TTS 朗读 - 文本转语音",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
        x, y, w, h,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) {
        logMsg(L"[TTS] CreateWindow FAILED");
        DestroyMenu(hMenuBar);
        shutdownGdiPlus();
        return 1;
    }
    SetMenu(hwnd, hMenuBar);
    hwnd_ = hwnd;

    HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
    if (hSysMenu) DeleteMenu(hSysMenu, SC_SIZE, MF_BYCOMMAND);

    if (!presetText.empty() && g_layout.hEdit) {
        SetWindowTextW(g_layout.hEdit, presetText.c_str());
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!IsWindow(hwnd)) break;
    }

    hwnd_ = nullptr;
    UnregisterClassW(kClassName, hInst);
    releaseVoice();
    shutdownGdiPlus();
    return 0;
}

LRESULT CALLBACK TtsDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        buildWindow(hwnd, hInst);
        applyConfigToUI(hwnd);
        return 0;
    }
    case WM_SIZE: {
        // 窗口尺寸变化（最大化 / 拉伸 / 还原）→ 重设控件位置
        relayout(hwnd);
        return 0;
    }
    case WM_HSCROLL: {
        HWND hCtrl = (HWND)lParam;
        if (GetWindowLongPtr(hCtrl, GWLP_ID) == kSliderVolumeId) {
            int pos = (int)SendMessageW(hCtrl, TBM_GETPOS, 0, 0);
            if (g_layout.hVolumeLabel) {
                wchar_t buf[16];
                swprintf_s(buf, L"%d", pos);
                SetWindowTextW(g_layout.hVolumeLabel, buf);
            }
            AppConfig::instance().setTtsVolume(pos);
            if (g_voice) g_voice->SetVolume((USHORT)((pos * 65535) / 100));
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case kBtnSpeakId: {
            if (!g_layout.hEdit) break;
            int len = GetWindowTextLengthW(g_layout.hEdit);
            if (len <= 0) { setStatusText(hwnd, L"⚠ 文本为空"); break; }
            std::wstring text;
            text.resize(len + 1);
            GetWindowTextW(g_layout.hEdit, &text[0], len + 1);
            text.resize(len);
            doSpeak(hwnd, text);
            break;
        }
        case kBtnPauseId:  doPause();  setStatusText(hwnd, L"⏸ 已暂停");  break;
        case kBtnResumeId: doResume(); setStatusText(hwnd, L"▶ 继续朗读"); break;
        case kBtnStopId:   doStop();   setStatusText(hwnd, L"⏹ 已停止");  break;
        case kBtnClipboardId: {
            std::wstring txt = getClipboardText();
            if (txt.empty()) {
                setStatusText(hwnd, L"⚠ 剪贴板为空或不是文本");
                MessageBoxW(hwnd, L"剪贴板为空或不是文本内容。\n请先复制一些文字再试。",
                            L"提示", MB_ICONINFORMATION);
            } else if (g_layout.hEdit) {
                SetWindowTextW(g_layout.hEdit, txt.c_str());
                doSpeak(hwnd, txt);
            }
            break;
        }
        case kBtnPasteId: {
            std::wstring txt = getClipboardText();
            if (txt.empty()) {
                setStatusText(hwnd, L"⚠ 剪贴板为空或不是文本");
            } else if (g_layout.hEdit) {
                int len = GetWindowTextLengthW(g_layout.hEdit);
                SendMessageW(g_layout.hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
                std::wstring prefix = (len > 0) ? L"\n" : L"";
                SendMessageW(g_layout.hEdit, EM_REPLACESEL, (WPARAM)TRUE,
                             (LPARAM)(prefix + txt).c_str());
                setStatusText(hwnd, L"✅ 已追加剪贴板内容");
            }
            break;
        }
        case kBtnClearId:
            if (g_layout.hEdit) SetWindowTextW(g_layout.hEdit, L"");
            doStop();
            setStatusText(hwnd, L"🗑 已清空并停止");
            break;
        case kBtnCloseId:
            DestroyWindow(hwnd);
            break;

        // 工具栏
        case kBtnFontId:        onFontButton(hwnd);        break;
        case kBtnTranslateId:   onTranslateButton(hwnd);   break;
        case kBtnLlmSettingsId: onLlmSettingsButton(hwnd); break;

        default:
            // 语速按钮
            if (id >= kRateBtnBase && id < kRateBtnBase + kRateBtnCount) {
                setRateFromButton(hwnd, id - kRateBtnBase);
            }
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        doStop();
        saveConfigFromUI(hwnd);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}