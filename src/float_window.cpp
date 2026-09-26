// float_window.cpp - 悬浮窗实现
#include "float_window.h"
#include "phrase_mgr.h"
#include "shortcut_mgr.h"
#include "window_helper.h"
#include "phrase_edit_dialog.h"
#include "shortcut_edit_dialog.h"
#include "settings_dialog.h"
#include "tts_dialog.h"
#include "config.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <string>
#include <sstream>
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
using namespace Gdiplus;

// 日志函数（与 main.cpp 中的 logMsg 兼容）
extern void logMsg(const wchar_t* msg);

static void logFmt(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(buf, fmt, ap);
    va_end(ap);
    logMsg(buf);
}

// WM_PAINT 入口 - 直接用 GDI+ 绘制到内存 DC
// Per-Pixel Alpha 模式：圆内 RGBA=不透明，圆外保持 DIB 初始的 alpha=0（透明）
static void paintButton(HDC hdc, int winSize, bool hovered) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    // 圆形渐变背景（填满整个窗口）
    RectF rect(0.0f, 0.0f, (REAL)winSize, (REAL)winSize);
    LinearGradientBrush brush(
        rect,
        hovered ? Color(255, 100, 181, 246) : Color(255, 33, 150, 243),
        hovered ? Color(255, 25, 118, 210)  : Color(255, 13, 71, 161),
        LinearGradientModeVertical
    );
    g.FillEllipse(&brush, rect);

    // 边框（贴在圆形最外侧，仅用于强化轮廓）
    Pen pen(Color(220, 255, 255, 255), 1.0f);
    g.DrawEllipse(&pen, rect);

    // 中心文字
    FontFamily fontFamily(L"Segoe UI Symbol");
    Font font(&fontFamily, 16, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 255, 255, 255));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"✚", -1, &font, rect, &fmt, &textBrush);
}

// Per-Pixel Alpha 渲染：32 位 DIB + AC_SRC_ALPHA + ULW_ALPHA
// 圆内不透明，圆外透明（无颜色键二义性，零闪烁）
static void paintButtonPPA(HWND hwnd, int size, bool hovered) {
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) { ReleaseDC(nullptr, hdcScreen); return; }

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = size;
    bi.biHeight = -size;  // top-down DIB（行从顶到底递增）
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap || !pBits) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // 关键：先把 DIB 全部清成 alpha=0（全透明）。GDI+ 只覆盖圆内像素，圆外仍透明
    int rowBytes = size * 4;
    for (int y = 0; y < size; y++) {
        memset((BYTE*)pBits + y * rowBytes, 0, rowBytes);
    }

    // GDI+ 画圆 + 文字到 DIB（圆内像素被 FillEllipse 覆盖为不透明）
    paintButton(hdcMem, size, hovered);

    POINT ptSrc = {0, 0};
    POINT ptWin; RECT rcWin;
    GetWindowRect(hwnd, &rcWin);
    ptWin.x = rcWin.left; ptWin.y = rcWin.top;
    SIZE szWin = {size, size};

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;  // 关键：逐像素 alpha 合成，无颜色键二义性

    UpdateLayeredWindow(hwnd, hdcScreen, &ptWin, &szWin, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

namespace {
constexpr int kBtnSize = 36;           // 圆形按钮直径（缩小为 36px）
constexpr int kBtnWindowSize = 36;     // 窗口大小 = 按钮大小（避免圆形外留下非透明边）
constexpr int kPanelWidth = 300;       // 面板宽度
constexpr int kPanelItemHeight = 56;   // 每条高度（支持多行文本换行）
constexpr int kPanelMaxItems = 12;     // 最多显示 12 条（实际根据屏幕高度自适应）
constexpr int kSectionGap = 8;         // 常用语区与快捷程序区之间的间距
constexpr int kShortcutsHeaderH = 24;  // "快捷程序" 标题高度
constexpr int kShortcutItemH = 40;     // 快捷程序每条高度
constexpr int kShortcutMaxItems = 6;   // 最多显示 6 个快捷程序
constexpr int kPanelPadding = 8;       // 底边距
constexpr int kToolbarH = 30;          // 面板底部工具栏高度（TTS 按钮）
constexpr UINT kTimerEdgeHide = 1;     // 贴边隐藏定时器
constexpr UINT kTimerEdgeShow = 2;     // 贴边显示定时器
constexpr LPCWSTR kBtnClass = L"FloatButtonClass";
constexpr LPCWSTR kPanelClass = L"FloatPanelClass";

// 根据屏幕工作区高度计算最多可显示的行数（避免面板超出屏幕）
// 布局常量与 drawPanel / showPanel 完全一致
int calcMaxItemsForScreen(int shortcutsCount) {
    constexpr int kOuter = 6;
    constexpr int kSmallTitleH = 24;
    constexpr int kItemH = 38;
    constexpr int kSepGap = 10;
    constexpr int kSecTitleH = 18;
    constexpr int kScItemH = 32;
    constexpr int kPadBottom = 8;

    RECT work;
    WinHelper::getWorkArea(work);
    int availHeight = (work.bottom - work.top) - (kOuter + kSmallTitleH + kToolbarH + kPadBottom + kOuter);
    int shortcutsBlockH = (shortcutsCount > 0)
        ? (kSepGap + kSecTitleH + (int)shortcutsCount * kScItemH)
        : 0;
    int maxByScreen = (availHeight - shortcutsBlockH) / kItemH;
    // 限制在 4~8 之间（避免面板过高不美观）
    if (maxByScreen < 4) maxByScreen = 4;
    if (maxByScreen > 8)  maxByScreen = 8;
    return maxByScreen;
}
}  // namespace

// GDI+ 全局 token
namespace {
ULONG_PTR g_gdiplusToken = 0;
}  // namespace

void initGdiPlus() {
    if (g_gdiplusToken == 0) {
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
    }
}

void shutdownGdiPlus() {
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

FloatWindow& FloatWindow::instance() {
    static FloatWindow w;
    return w;
}

bool FloatWindow::create() {
    initGdiPlus();
    hInst_ = GetModuleHandle(nullptr);
    registerClasses();

    // 创建悬浮按钮 - 默认在屏幕中央偏上 (y=120) 避开所有窗口覆盖
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int btnX = screenW / 2 - kBtnSize / 2;     // 屏幕水平居中
    int btnY = 120;                              // 顶部偏下 120px（避开任务栏/标题栏）
    hBtn_ = CreateWindowEx(
        // WS_EX_LAYERED + Per-Pixel Alpha：圆外像素 alpha=0，真正透明无颜色键闪烁
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kBtnClass, L"",
        WS_POPUP | WS_VISIBLE,
        btnX, btnY, kBtnWindowSize, kBtnWindowSize,
        nullptr, nullptr, hInst_, this
    );
    if (!hBtn_) return false;

    // 强制置顶 + 初次绘制（Per-Pixel Alpha 上传 DIB）
    SetWindowPos(hBtn_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    paintButtonPPA(hBtn_, kBtnWindowSize, false);
    return true;
}

void FloatWindow::destroy() {
    if (hPanel_) DestroyWindow(hPanel_);
    if (hBtn_) DestroyWindow(hBtn_);
    unregisterClasses();
    shutdownGdiPlus();
}

void FloatWindow::show() {
    ShowWindow(hBtn_, SW_SHOW);
}

void FloatWindow::hide() {
    hidePanel();
    ShowWindow(hBtn_, SW_HIDE);
}

void FloatWindow::toggle() {
    if (IsWindowVisible(hBtn_)) hide();
    else show();
}

void FloatWindow::registerClasses() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    // 按钮类
    wc.lpfnWndProc = btnWndProc;
    wc.lpszClassName = kBtnClass;
    // NULL_BRUSH：WM_PAINT 中 GDI+ 直接画圆形，避免先擦灰底再画圆造成的中间帧闪烁
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    // 面板类
    WNDCLASSEXW wc2{};
    wc2.cbSize = sizeof(wc2);
    wc2.hInstance = hInst_;
    wc2.lpfnWndProc = panelWndProc;
    wc2.lpszClassName = kPanelClass;
    // NULL_BRUSH：WM_PAINT 中 GDI+ 直接画整面板内容（圆角背景 + 列表），不擦灰底
    // 原 nullptr(=BLACK_BRUSH) 会在每次 BeginPaint 前用黑刷子擦背景，造成白色面板"灰闪"中间帧
    wc2.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // CS_DBLCLKS: 支持双击消息
    // CS_SAVEBITS: 保存被面板遮挡的屏幕位图，移除面板时直接恢复，减少下面窗口重绘闪烁
    wc2.style = CS_DBLCLKS | CS_SAVEBITS;
    RegisterClassExW(&wc2);
}

void FloatWindow::unregisterClasses() {
    UnregisterClassW(kBtnClass, hInst_);
    UnregisterClassW(kPanelClass, hInst_);
}

void FloatWindow::togglePanel() {
    if (IsWindowVisible(hPanel_)) hidePanel();
    else showPanel();
}

void FloatWindow::showPanel() {
    // 取一次数据（保证常用语数 / 快捷程序数一致）
    auto phrases = PhraseManager::instance().getAll();
    auto shortcuts = ShortcutManager::instance().getAll();

    int shortcutsToShow = (int)shortcuts.size();
    if (shortcutsToShow > kShortcutMaxItems) shortcutsToShow = kShortcutMaxItems;

    int maxItems = calcMaxItemsForScreen(shortcutsToShow);
    int phraseCount = std::min((int)phrases.size(), maxItems);

    // 客户区高度 = 外边距 + 小标题 + 常用语 + 大间距 + 快捷程序小标题 + 快捷程序 + 底内边距
    // 与 drawPanel 布局常量保持一致（避免命中区错位）
    constexpr int kOuter = 6;
    constexpr int kSmallTitleH = 24;     // 系统标题栏下方的小标题
    constexpr int kItemH = 38;          // 常用语每条（压缩）
    constexpr int kSepGap = 10;
    constexpr int kSecTitleH = 18;
    constexpr int kScItemH = 32;        // 快捷程序每条（压缩）
    constexpr int kPadBottom = 8;
    const int listY = kOuter + kSmallTitleH;
    const int sectionY = listY + phraseCount * kItemH + kSepGap;
    int shortcutsBlockH = (shortcutsToShow > 0)
        ? (kSepGap + kSecTitleH + shortcutsToShow * kScItemH)
        : 0;
    int contentHeight = kOuter + kSmallTitleH + phraseCount * kItemH + shortcutsBlockH
                        + kToolbarH + kPadBottom + kOuter;

    // 总窗口高度 = 客户区 + 系统标题栏 + 边框（让 CreateWindow 用 OVERLAPPED 风格）
    int cyCaption = GetSystemMetrics(SM_CYCAPTION);
    int cyFrame  = GetSystemMetrics(SM_CYFIXEDFRAME);
    int totalHeight = contentHeight + cyCaption + cyFrame * 2;

    // 记录布局状态（给 drawPanel / 鼠标处理用）
    m_phraseCount = phraseCount;
    m_shortcutCount = shortcutsToShow;
    m_shortcutHeaderY0 = sectionY;
    m_shortcutY0 = sectionY + kSecTitleH;
    m_toolbarY0 = contentHeight - kPadBottom - kOuter - kToolbarH;
    hoveredRow_ = -1;
    toolbarHovered_ = false;

    RECT rc;
    GetWindowRect(hBtn_, &rc);
    int x = rc.left - kPanelWidth - 8;
    int y = rc.top;

    // 限制不超出屏幕工作区
    RECT work;
    WinHelper::getWorkArea(work);
    if (y + totalHeight > work.bottom) y = work.bottom - totalHeight;
    if (y < work.top) y = work.top;
    if (x < work.left) x = rc.right + 8;
    if (x + kPanelWidth > work.right) x = work.right - kPanelWidth;

    if (!hPanel_) {
        // 关键：先创建隐藏窗口，画好内容后再显示
        // WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU：带系统标准标题栏，
        // 用户可通过标题栏拖动面板、点 X 关闭
        hPanel_ = CreateWindowEx(
            WS_EX_TOPMOST,                  // 不要 WS_EX_TOOLWINDOW（要任务栏可见的"普通窗口"）
            kPanelClass, L"快捷面板 - Float Button",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU
                | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            x, y, kPanelWidth, totalHeight,
            nullptr, nullptr, hInst_, this
        );
        if (!hPanel_) return;

        // 关键：禁用系统菜单中的"最大化/最小化"（仅留"关闭"和"移动"）
        // 因为面板没有意义最大化
        HMENU hSysMenu = GetSystemMenu(hPanel_, FALSE);
        if (hSysMenu) {
            DeleteMenu(hSysMenu, SC_MAXIMIZE, MF_BYCOMMAND);
            DeleteMenu(hSysMenu, SC_MINIMIZE, MF_BYCOMMAND);
            DeleteMenu(hSysMenu, SC_SIZE,     MF_BYCOMMAND);
        }

        InvalidateRect(hPanel_, nullptr, FALSE);
        UpdateWindow(hPanel_);
        SetWindowPos(hPanel_, HWND_TOPMOST, x, y, kPanelWidth, totalHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        // 已存在：更新位置/大小/标题，显示
        SetWindowTextW(hPanel_, L"快捷面板 - Float Button");
        SetWindowPos(hPanel_, HWND_TOPMOST, x, y, kPanelWidth, totalHeight,
                     SWP_NOACTIVATE);
        InvalidateRect(hPanel_, nullptr, FALSE);
        UpdateWindow(hPanel_);
        ShowWindow(hPanel_, SW_SHOWNA);
    }
}

void FloatWindow::hidePanel() {
    if (hPanel_ && IsWindowVisible(hPanel_)) {
        ShowWindow(hPanel_, SW_HIDE);
    }
}

void FloatWindow::refreshPhrases() {
    // 重新计算面板高度
    if (hPanel_) {
        DestroyWindow(hPanel_);
        hPanel_ = nullptr;
        if (IsWindowVisible(hBtn_)) {
            showPanel();
        }
    }
}

void FloatWindow::refreshShortcuts() {
    // 与 refreshPhrases 逻辑相同（面板里有快捷程序区，必须重建才能反映编辑结果）
    refreshPhrases();
}

void FloatWindow::showEditDialog() {
    hidePanel();
    // 独立窗口，不会禁用按钮
    PhraseEditDialog::show(GetModuleHandle(nullptr));
    refreshPhrases();
}

void FloatWindow::drawPanel(HDC hdc) {
    auto phrases = PhraseManager::instance().getAll();
    auto shortcuts = ShortcutManager::instance().getAll();
    int maxItems = calcMaxItemsForScreen((int)std::min<size_t>(shortcuts.size(), kShortcutMaxItems));
    int itemCount = std::min((int)phrases.size(), maxItems);
    int shortcutsToShow = (int)std::min<size_t>(shortcuts.size(), kShortcutMaxItems);

    // 布局常量（与 showPanel 一致）
    constexpr int kOuter = 6;
    constexpr int kPadX = 10;
    constexpr int kSmallTitleH = 24;    // 系统标题栏下方的小标题
    constexpr int kItemH = 38;          // 常用语每条（压缩）
    constexpr int kSepGap = 10;
    constexpr int kSecTitleH = 18;
    constexpr int kScItemH = 32;        // 快捷程序每条（压缩）
    constexpr int kRadius = 6;          // 圆角半径（普通窗口风格）
    constexpr int kPadBottom = 8;
    constexpr int kShortcutIndent = 28;

    int shortcutsBlockH = (shortcutsToShow > 0)
        ? (kSepGap + kSecTitleH + shortcutsToShow * kScItemH)
        : 0;
    int height = kOuter + kSmallTitleH + itemCount * kItemH + shortcutsBlockH
                 + kToolbarH + kPadBottom + kOuter;
    int width = kPanelWidth;
    REAL rWidth = (REAL)width;
    REAL rHeight = (REAL)height;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    FontFamily fontFamily(L"Microsoft YaHei UI");

    // 辅助 lambda：画圆角矩形路径
    auto addRoundRect = [&](GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r) {
        REAL d = r * 2.0f;
        path.AddArc(x, y, d, d, 180, 90);
        path.AddArc(x + w - d, y, d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        path.AddArc(x, y + h - d, d, d, 90, 90);
        path.CloseFigure();
    };

    // 1) 圆角背景（白色）- 普通窗口风格，不画外阴影（系统已有边框）
    GraphicsPath bgPath;
    addRoundRect(bgPath, 0, 0, rWidth, rHeight, (REAL)kRadius);
    SolidBrush bgBrush(Color(255, 252, 252, 252));
    g.FillPath(&bgBrush, &bgPath);

    // 2) 顶部小标题（图标 + 标题），不画整条蓝色背景
    Font titleIconFont(&fontFamily, 13, FontStyleBold, UnitPixel);
    Font titleFont(&fontFamily, 12, FontStyleBold, UnitPixel);
    SolidBrush titleTextBrush(Color(255, 33, 150, 243));
    StringFormat titleFmt;
    titleFmt.SetLineAlignment(StringAlignmentCenter);
    titleFmt.SetAlignment(StringAlignmentNear);
    REAL titleY = (REAL)kOuter;
    REAL titleH = (REAL)kSmallTitleH;
    RectF titleIconRect((REAL)kPadX, titleY, 20.0f, titleH);
    g.DrawString(L"💬", -1, &titleIconFont, titleIconRect, &titleFmt, &titleTextBrush);
    RectF titleRect((REAL)kPadX + 22.0f, titleY, rWidth - (REAL)kPadX * 2 - 22.0f, titleH);
    g.DrawString(L"快捷面板", -1, &titleFont, titleRect, &titleFmt, &titleTextBrush);

    // 标题下方细分隔线
    Pen titleSepPen(Color(255, 230, 235, 240), 1.0f);
    REAL sepY = (REAL)(kOuter + kSmallTitleH);
    g.DrawLine(&titleSepPen, (REAL)kPadX, sepY, rWidth - (REAL)kPadX, sepY);

    // ===== 常用语列表 =====
    int listY = kOuter + kSmallTitleH;
    Font itemFont(&fontFamily, 12, FontStyleRegular, UnitPixel);
    Font idxFont(&fontFamily, 9, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 50, 50, 50));
    SolidBrush idxBrush(Color(255, 33, 150, 243));

    for (int i = 0; i < itemCount; ++i) {
        REAL y = (REAL)(listY + i * kItemH);
        // 悬停高亮（圆角矩形）
        if (hoveredRow_ == i) {
            GraphicsPath hp;
            addRoundRect(hp, 4.0f, y + 2.0f, rWidth - 8.0f, (REAL)kItemH - 4.0f, 6.0f);
            SolidBrush hb(Color(255, 235, 245, 255));
            g.FillPath(&hb, &hp);
        }
        // 序号徽章
        wchar_t num[8];
        swprintf_s(num, L"%d", i + 1);
        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        centerFmt.SetLineAlignment(StringAlignmentCenter);
        RectF badgeRect((REAL)kPadX, y + (REAL)(kItemH - 22) / 2, 22.0f, 22.0f);
        GraphicsPath bp;
        addRoundRect(bp, badgeRect.X, badgeRect.Y, badgeRect.Width, badgeRect.Height, 5.0f);
        if (hoveredRow_ == i) {
            SolidBrush bb(Color(255, 33, 150, 243));
            g.FillPath(&bb, &bp);
        } else {
            SolidBrush bb(Color(255, 220, 240, 255));
            g.FillPath(&bb, &bp);
        }
        SolidBrush numBrush(Color(255, hoveredRow_ == i ? 255 : 33, hoveredRow_ == i ? 255 : 150, hoveredRow_ == i ? 255 : 243));
        g.DrawString(num, -1, &idxFont, badgeRect, &centerFmt, &numBrush);

        // 文本（支持多行）
        RectF textRect((REAL)(kPadX + 30), y + 4.0f, rWidth - (REAL)(kPadX + 30 + kPadX), (REAL)kItemH - 8.0f);
        StringFormat textFmt;
        textFmt.SetAlignment(StringAlignmentNear);
        textFmt.SetLineAlignment(StringAlignmentNear);
        textFmt.SetTrimming(StringTrimmingEllipsisCharacter);
        textFmt.SetFormatFlags(StringFormatFlagsNoClip);
        g.DrawString(phrases[i].c_str(), -1, &itemFont, textRect, &textFmt, &textBrush);
    }

    // ===== 快捷程序区 =====
    if (shortcutsToShow > 0) {
        int sectionY = listY + itemCount * kItemH + kSepGap;
        // 顶部细线
        Pen sepPen(Color(255, 220, 225, 230), 1.0f);
        g.DrawLine(&sepPen, (REAL)kPadX, (REAL)sectionY, rWidth - (REAL)kPadX, (REAL)sectionY);

        // 区标题
        Font secIconFont(&fontFamily, 12, FontStyleBold, UnitPixel);
        Font secTitleFont(&fontFamily, 11, FontStyleBold, UnitPixel);
        Font tipFont(&fontFamily, 9, FontStyleRegular, UnitPixel);
        SolidBrush secTitleBrush(Color(255, 80, 80, 80));
        SolidBrush tipBrush(Color(255, 160, 160, 160));
        StringFormat stf;
        stf.SetLineAlignment(StringAlignmentCenter);
        stf.SetAlignment(StringAlignmentNear);
        RectF secIconRect((REAL)kPadX, (REAL)(sectionY + 4), 18.0f, (REAL)kSecTitleH);
        g.DrawString(L"⚡", -1, &secIconFont, secIconRect, &stf, &secTitleBrush);
        RectF secTitleRect((REAL)(kPadX + 20), (REAL)(sectionY + 4), rWidth - (REAL)(kPadX + 24), (REAL)kSecTitleH);
        g.DrawString(L"快捷程序", -1, &secTitleFont, secTitleRect, &stf, &secTitleBrush);
        // 右侧提示
        StringFormat rt;
        rt.SetLineAlignment(StringAlignmentCenter);
        rt.SetAlignment(StringAlignmentFar);
        RectF tipRect((REAL)kPadX, (REAL)(sectionY + 4), rWidth - (REAL)(kPadX * 2), (REAL)kSecTitleH);
        g.DrawString(L"点击启动 · 右键编辑", -1, &tipFont, tipRect, &rt, &tipBrush);

        // 列表
        Font scIconFont(&fontFamily, 16, FontStyleRegular, UnitPixel);
        Font scNameFont(&fontFamily, 12, FontStyleBold, UnitPixel);
        SolidBrush scNameBrush(Color(255, 50, 50, 50));

        int scY0 = sectionY + kSecTitleH;
        for (int i = 0; i < shortcutsToShow; ++i) {
            REAL y = (REAL)(scY0 + i * kScItemH);
            int scRowIdx = itemCount + i;

            // 悬停高亮（圆角矩形）
            if (hoveredRow_ == scRowIdx) {
                GraphicsPath hp;
                addRoundRect(hp, 4.0f, y + 2.0f, rWidth - 8.0f, (REAL)kScItemH - 4.0f, 6.0f);
                SolidBrush hb(Color(255, 235, 245, 255));
                g.FillPath(&hb, &hp);
            }

            const auto& sc = shortcuts[i];
            // emoji
            RectF iconRect((REAL)kPadX, y, (REAL)kShortcutIndent, (REAL)kScItemH);
            StringFormat centerFmt2;
            centerFmt2.SetAlignment(StringAlignmentCenter);
            centerFmt2.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(sc.icon.c_str(), -1, &scIconFont, iconRect, &centerFmt2, &scNameBrush);

            // 名称（单行显示，无路径副标题）
            RectF nameRect((REAL)(kPadX + kShortcutIndent), y, rWidth - (REAL)(kPadX + kShortcutIndent + kPadX), (REAL)kScItemH);
            StringFormat nf;
            nf.SetAlignment(StringAlignmentNear);
            nf.SetLineAlignment(StringAlignmentCenter);
            nf.SetTrimming(StringTrimmingEllipsisCharacter);
            g.DrawString(sc.name.c_str(), -1, &scNameFont, nameRect, &nf, &scNameBrush);
        }
    }

    // ===== 底部工具栏（TTS 朗读按钮） =====
    int toolbarY = height - kPadBottom - kOuter - kToolbarH;
    // 顶部分隔线
    Pen tbSepPen(Color(255, 230, 235, 240), 1.0f);
    g.DrawLine(&tbSepPen, (REAL)kPadX, (REAL)toolbarY, rWidth - (REAL)kPadX, (REAL)toolbarY);

    // TTS 朗读按钮（圆角矩形 + 图标 + 文字）
    GraphicsPath tbPath;
    addRoundRect(tbPath, (REAL)kPadX, (REAL)(toolbarY + 4),
                 rWidth - (REAL)(kPadX * 2), (REAL)(kToolbarH - 8), 5.0f);
    if (toolbarHovered_) {
        SolidBrush tbBg(Color(255, 235, 245, 255));
        g.FillPath(&tbBg, &tbPath);
    } else {
        SolidBrush tbBg(Color(255, 248, 249, 252));
        g.FillPath(&tbBg, &tbPath);
    }
    Pen tbBorder(Color(255, 220, 230, 245), 1.0f);
    g.DrawPath(&tbBorder, &tbPath);

    // 图标 + 文字
    Font tbIconFont(&fontFamily, 13, FontStyleBold, UnitPixel);
    Font tbTextFont(&fontFamily, 11, FontStyleBold, UnitPixel);
    SolidBrush tbIconBrush(Color(255, 33, 150, 243));
    SolidBrush tbTextBrush(Color(255, 60, 60, 60));
    StringFormat tbFmt;
    tbFmt.SetLineAlignment(StringAlignmentCenter);
    tbFmt.SetAlignment(StringAlignmentNear);
    RectF tbIconRect((REAL)(kPadX + 8), (REAL)(toolbarY + 4), 22.0f, (REAL)(kToolbarH - 8));
    g.DrawString(L"📢", -1, &tbIconFont, tbIconRect, &tbFmt, &tbIconBrush);
    RectF tbTextRect((REAL)(kPadX + 32), (REAL)(toolbarY + 4),
                     rWidth - (REAL)(kPadX + 32 + 8), (REAL)(kToolbarH - 8));
    g.DrawString(L"朗读剪贴板 (TTS)", -1, &tbTextFont, tbTextRect, &tbFmt, &tbTextBrush);

    // 右侧：自动朗读状态指示
    bool autoSpeak = AppConfig::instance().autoSpeakClipboard();
    Font tbStatusFont(&fontFamily, 9, FontStyleRegular, UnitPixel);
    SolidBrush tbStatusBrush(autoSpeak ? Color(255, 76, 175, 80) : Color(255, 160, 160, 160));
    StringFormat stFmt;
    stFmt.SetLineAlignment(StringAlignmentCenter);
    stFmt.SetAlignment(StringAlignmentFar);
    RectF statusRect((REAL)kPadX, (REAL)(toolbarY + 4),
                     rWidth - (REAL)(kPadX * 2), (REAL)(kToolbarH - 8));
    std::wstring statusText = autoSpeak ? L"自动朗读: 开" : L"自动朗读: 关";
    g.DrawString(statusText.c_str(), -1, &tbStatusFont, statusRect, &stFmt, &tbStatusBrush);
}

// ============== 窗口过程 ==============

LRESULT CALLBACK FloatWindow::btnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = (FloatWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (LPCREATESTRUCT)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_ERASEBKGND:
        // 背景擦除：LAYERED 窗口的擦除不影响显示，但仍 return 1 节省一次 GDI 操作
        return 1;
    case WM_PAINT: {
        // LAYERED 窗口的 WM_PAINT 不会显示给用户。
        // 我们在 create() / hover 变化 / 拖动结束 手动调 paintButtonPPA 上传 DIB。
        // 这里只做"声明无效区已处理"，避免系统持续发 WM_PAINT。
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        self->dragging_ = true;
        self->dragStart_.x = GET_X_LPARAM(lParam);
        self->dragStart_.y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetWindowRect(hwnd, &rc);
        self->windowStart_.x = rc.left;
        self->windowStart_.y = rc.top;
        // 关键：SetCapture + 拖动时禁用吸边动画（避免卡顿）
        SetCapture(hwnd);
        // 拖动时禁用定时器吸边逻辑（在贴边代码里检查）
        KillTimer(hwnd, 999);
        return 0;
    }
    case WM_MOUSEMOVE: {
        // 1) 拖动分支：保持原有逻辑（拖动期间不重传 DIB，只移动窗口）
        if (self->dragging_) {
            // 使用屏幕坐标的鼠标位置（避免客户区外 WM_MOUSEMOVE 不发的问题）
            POINT cur;
            GetCursorPos(&cur);
            // 屏幕坐标差值 = 拖动距离
            int newX = cur.x - self->dragStart_.x;
            int newY = cur.y - self->dragStart_.y;
            // 限制在屏幕范围内（避免拖到屏幕外后无法拉回）
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            if (newX < -kBtnSize/2) newX = -kBtnSize/2;
            if (newY < 0) newY = 0;
            if (newX > sw - kBtnSize/2) newX = sw - kBtnSize/2;
            if (newY > sh - kBtnSize/2) newY = sh - kBtnSize/2;
            // LAYERED 窗口拖动：只改位置，不重传 DIB（位置由 SetWindowPos 控制）
            SetWindowPos(hwnd, nullptr,
                newX, newY,
                0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        // 2) hover 状态：注册 TrackMouseEvent 接收 WM_MOUSELEAVE
        if (!self->trackedMouse_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            self->trackedMouse_ = true;
        }
        // 3) hover 状态变化时重传 DIB（PPA 合成，零闪烁）
        if (!self->hovered_) {
            self->hovered_ = true;
            paintButtonPPA(hwnd, kBtnWindowSize, true);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (self->dragging_) {
            ReleaseCapture();
            // 区分点击 vs 拖动（位移 < 5px 算点击）
            POINT cur; GetCursorPos(&cur);
            int moved = abs(cur.x - (self->windowStart_.x + self->dragStart_.x))
                      + abs(cur.y - (self->windowStart_.y + self->dragStart_.y));
            self->dragging_ = false;
            if (moved < 5) {
                self->togglePanel();
            } else {
                // 拖动结束才贴边（避免拖动过程中抖动）
                WinHelper::snapToEdge(hwnd);
            }
            // 拖动结束后重传一次 DIB（位置变了，确保显示正确）
            paintButtonPPA(hwnd, kBtnWindowSize, self->hovered_);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        // 右键：弹出菜单
        // 关键：动态字符串（快捷键描述）必须在 TrackPopupMenu 之前保持有效
        std::wstring hkLabel = L"快捷键: " + AppConfig::instance().hotkeyDescription() + L" (&K)...";
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 9001, L"编辑常用语(&E)...");
        AppendMenuW(hMenu, MF_STRING, 9008, L"编辑快捷程序(&S)...");
        AppendMenuW(hMenu, MF_STRING, 9002, hkLabel.c_str());
        AppendMenuW(hMenu, MF_STRING, 9007, L"设置(&G)...");
        AppendMenuW(hMenu, MF_STRING, 9003, L"重置位置(&R)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, 9006, L"锁定 Windows(&L)");
        AppendMenuW(hMenu, MF_STRING, 9004, L"关于(&A)...");
        AppendMenuW(hMenu, MF_STRING, 9005, L"退出(&X)");

        POINT pt;
        GetCursorPos(&pt);
        // 关键：SetForegroundWindow 让菜单能正确关闭
        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            pt.x, pt.y, 0, hwnd, nullptr);

        if (cmd == 9001) {
            // 编辑常用语
            self->showEditDialog();
        } else if (cmd == 9008) {
            // 编辑快捷程序
            self->hidePanel();
            ShortcutEditDialog::show(self->hInst_);
        } else if (cmd == 9002) {
            // 快捷键设置 - 打开设置窗口（包含快捷键录制）
            SettingsDialog::show(self->hInst_);
        } else if (cmd == 9007) {
            // 设置 - 同上（快捷键 + 透明度）
            SettingsDialog::show(self->hInst_);
        } else if (cmd == 9003) {
            // 重置位置到屏幕中央
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            SetWindowPos(hwnd, nullptr,
                screenW / 2 - kBtnSize / 2, 120,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
            // 重新计算贴边
            WinHelper::autoHideIfAtEdge(hwnd, pt);
        } else if (cmd == 9006) {
            // 锁定 Windows - 调用系统 API
            logMsg(L"User requested LockWorkStation");
            LockWorkStation();
        } else if (cmd == 9004) {
            // 关于
            MessageBoxW(hwnd,
                L"Float Button v1.0\n\n"
                L"功能：\n"
                L"- 点击展开常用语面板\n"
                L"- 拖动移动位置\n"
                L"- 右键弹出菜单\n"
                L"- 双击常用语复制\n"
                L"- 多行文本自动换行\n\n"
                L"GDI+ 渲染，普通窗口（无 LAYERED 闪烁）",
                L"关于", MB_ICONINFORMATION);
        } else if (cmd == 9005) {
            // 退出
            PostQuitMessage(0);
        }

        DestroyMenu(hMenu);
        return 0;
    }
    case WM_MOUSELEAVE: {
        // 离开按钮区域：清空 hover 状态 + 启动贴边隐藏定时器
        self->trackedMouse_ = false;
        if (self->hovered_) {
            self->hovered_ = false;
            // 重传 DIB（恢复非 hover 颜色）
            paintButtonPPA(hwnd, kBtnWindowSize, false);
        }
        if (!self->isHiding_) {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            if (WinHelper::isAtLeftEdge(rc) || WinHelper::isAtRightEdge(rc)) {
                self->startEdgeHideTimer();
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == kTimerEdgeHide) {
            POINT p; GetCursorPos(&p);
            WinHelper::autoHideIfAtEdge(hwnd, p);
            self->stopEdgeHideTimer();
        }
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FloatWindow::panelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = (FloatWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (LPCREATESTRUCT)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_ERASEBKGND:
        // 禁止背景擦除，避免面板闪烁
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->drawPanel(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int y = GET_Y_LPARAM(lParam);
        int newHover = -1;
        // 列表起点 y = kOuter + kSmallTitleH = 6 + 24 = 30
        const int listY = 30;
        const int kItemH = 38;
        const int kScItemH = 32;

        // 先判断是否在底部工具栏区域
        bool inToolbar = (self->m_toolbarY0 > 0 && y >= self->m_toolbarY0);
        if (inToolbar != self->toolbarHovered_) {
            self->toolbarHovered_ = inToolbar;
            // 重绘工具栏区域
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            RECT tr = {0, self->m_toolbarY0, rcClient.right,
                       self->m_toolbarY0 + kToolbarH + 8};
            InvalidateRect(hwnd, &tr, FALSE);
        }

        if (y >= listY) {
            if (y < self->m_shortcutY0) {
                // 常用语区
                int idx = (y - listY) / kItemH;
                if (idx >= 0 && idx < self->m_phraseCount) newHover = idx;
            } else {
                // 快捷程序区
                int idx = (y - self->m_shortcutY0) / kScItemH;
                if (idx >= 0 && idx < self->m_shortcutCount) newHover = self->m_phraseCount + idx;
            }
        }
        if (newHover != self->hoveredRow_) {
            int oldRow = self->hoveredRow_;
            self->hoveredRow_ = newHover;
            // 局部重绘：只重绘"旧行"和"新行"两个小矩形（避免整面板重绘导致闪烁）
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            auto invalidateRow = [&](int row) {
                if (row < 0) return;
                int ry = 0, rh = 0;
                if (row < self->m_phraseCount) {
                    ry = listY + row * kItemH;
                    rh = kItemH;
                } else {
                    int scRow = row - self->m_phraseCount;
                    ry = self->m_shortcutY0 + scRow * kScItemH;
                    rh = kScItemH;
                }
                RECT r = {0, ry, rcClient.right, ry + rh};
                InvalidateRect(hwnd, &r, FALSE);
            };
            invalidateRow(oldRow);
            invalidateRow(newHover);
            // 改变光标为手型，提示"可点击"（hover 之外的视觉反馈）
            SetCursor(LoadCursor(nullptr,
                (newHover >= 0 || self->toolbarHovered_) ? IDC_HAND : IDC_ARROW));
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (self->hoveredRow_ != -1) {
            int oldRow = self->hoveredRow_;
            self->hoveredRow_ = -1;
            // 局部重绘：只重绘最后高亮的那一行
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            const int listY = 30;
            const int kItemH = 38;
            const int kScItemH = 32;
            int ry = 0, rh = 0;
            if (oldRow < self->m_phraseCount) {
                ry = listY + oldRow * kItemH;
                rh = kItemH;
            } else {
                int scRow = oldRow - self->m_phraseCount;
                ry = self->m_shortcutY0 + scRow * kScItemH;
                rh = kScItemH;
            }
            RECT r = {0, ry, rcClient.right, ry + rh};
            InvalidateRect(hwnd, &r, FALSE);
        }
        // 清空 toolbar hover
        if (self->toolbarHovered_) {
            self->toolbarHovered_ = false;
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            if (self->m_toolbarY0 > 0) {
                RECT tr = {0, self->m_toolbarY0, rcClient.right,
                           self->m_toolbarY0 + kToolbarH + 8};
                InvalidateRect(hwnd, &tr, FALSE);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        // 列表起点 y = 30
        const int listY = 30;
        const int kItemH = 38;
        const int kScItemH = 32;
        if (y < listY) return 0;

        // 先判断是否点击底部工具栏（TTS 按钮）
        if (self->m_toolbarY0 > 0 && y >= self->m_toolbarY0) {
            // 打开 TTS 对话框，自动读取剪贴板
            logMsg(L"Clicked TTS toolbar button");
            TtsDialog::show(self->hInst_);
            // 点击后关闭面板，避免遮挡 TTS 窗口
            SetTimer(hwnd, 99, 200, nullptr);
            return 0;
        }

        if (y < self->m_shortcutY0) {
            // 常用语区：点击复制
            int idx = (y - listY) / kItemH;
            auto phrases = PhraseManager::instance().getAll();
            if (idx >= 0 && idx < (int)phrases.size()) {
                if (WinHelper::copyToClipboard(phrases[idx])) {
                    logMsg(L"Copied phrase to clipboard");
                } else {
                    logMsg(L"Copy to clipboard FAILED");
                }
                // 视觉反馈：只重绘被点击的那一行（避免整面板闪烁）
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                RECT r = {0, listY + idx * kItemH, rcClient.right, listY + (idx + 1) * kItemH};
                InvalidateRect(hwnd, &r, FALSE);
                // 800ms 后自动隐藏
                SetTimer(hwnd, 99, 800, nullptr);
            }
        } else {
            // 快捷程序区：点击启动
            int idx = (y - self->m_shortcutY0) / kScItemH;
            auto shortcuts = ShortcutManager::instance().getAll();
            if (idx >= 0 && idx < (int)shortcuts.size()) {
                std::wstring err;
                if (!ShortcutManager::instance().launch(shortcuts[idx], &err)) {
                    // 启动失败提示（用 log 简单记录，避免弹窗打扰）
                    logMsg((L"Launch shortcut FAILED: " + err).c_str());
                } else {
                    logMsg(L"Launched shortcut");
                }
                // 视觉反馈：只重绘被点击的那一行
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                RECT r = {0, self->m_shortcutY0 + idx * kScItemH,
                          rcClient.right, self->m_shortcutY0 + (idx + 1) * kScItemH};
                InvalidateRect(hwnd, &r, FALSE);
                // 600ms 后自动隐藏
                SetTimer(hwnd, 99, 600, nullptr);
            }
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        // 右键面板上的常用语 = 删除该条（需二次确认）
        int y = GET_Y_LPARAM(lParam);
        const int listY = 30;
        const int kItemH = 38;
        if (y < listY) return 0;

        if (y < self->m_shortcutY0) {
            // 常用语区：删除常用语
            int idx = (y - listY) / kItemH;
            if (idx < 0) return 0;

            // 取出要删除的常用语内容用于提示
            auto phrases = PhraseManager::instance().getAll();
            if (static_cast<size_t>(idx) >= phrases.size()) return 0;

            const std::wstring& phrase = phrases[idx];
            // 预览文本过长时截断，避免对话框过宽
            std::wstring preview = phrase;
            if (preview.size() > 60) preview = preview.substr(0, 60) + L"...";

            // 拼接确认提示文本
            std::wstring tip = L"确定要删除以下常用语吗？\n\n“" + preview + L"”\n\n删除后不可恢复，请谨慎操作。";

            // 弹出确认对话框（Yes/No）
            int ret = MessageBoxW(hwnd, tip.c_str(), L"删除确认",
                                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (ret != IDYES) return 0;

            // 用户确认后执行删除
            PhraseManager::instance().remove(idx);
            self->refreshPhrases();
        } else {
            // 快捷程序区：右键编辑快捷程序（打开编辑窗口）
            ShortcutEditDialog::show(self->hInst_);
            self->hidePanel();
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == 99) {
            KillTimer(hwnd, 99);
            self->hidePanel();
        }
        return 0;
    case WM_KILLFOCUS:
        // 失焦时关闭面板
        self->hidePanel();
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void FloatWindow::startEdgeHideTimer() {
    SetTimer(hBtn_, kTimerEdgeHide, 1000, nullptr);
}

void FloatWindow::stopEdgeHideTimer() {
    KillTimer(hBtn_, kTimerEdgeHide);
}
