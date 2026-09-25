// phrase_edit_dialog.cpp - 常用语编辑窗口
#include "phrase_edit_dialog.h"
#include "phrase_mgr.h"
#include "window_helper.h"
#include <string>
#include <commctrl.h>
#include <gdiplus.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

LPCWSTR PhraseEditDialog::kClassName = L"PhraseEditDialogClass";
HWND PhraseEditDialog::hList_ = nullptr;
HWND PhraseEditDialog::hEdit_ = nullptr;

namespace {
constexpr int kListId = 1001;
constexpr int kEditId = 1002;
constexpr int kBtnAdd = 1010;
constexpr int kBtnUpdate = 1011;
constexpr int kBtnDelete = 1012;
constexpr int kBtnUp = 1013;
constexpr int kBtnDown = 1014;
constexpr int kBtnReset = 1015;
constexpr int kBtnClose = 1016;
constexpr int kBtnCopy = 1017;  // 复制按钮
constexpr int kBtnOpenBak = 1018;  // 打开备份目录按钮

LPCWSTR kCustomBtnClass = L"CustomBtnClass";
ULONG_PTR g_gdiplusToken = 0;

void initBtnGdiPlus() {
    if (g_gdiplusToken == 0) {
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
    }
}

void shutdownBtnGdiPlus() {
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

// 自定义按钮颜色方案
struct BtnStyle {
    Color bgStart;
    Color bgEnd;
    Color hoverStart;
    Color hoverEnd;
    Color text;
    Color border;
};

BtnStyle getBtnStyle(int btnId) {
    switch (btnId) {
    case kBtnAdd:
        return {Color(255, 76, 175, 80), Color(255, 56, 142, 60),   // 绿色
                Color(255, 102, 187, 106), Color(255, 76, 175, 80),
                Color(255, 255, 255, 255), Color(255, 46, 125, 50)};
    case kBtnUpdate:
        return {Color(255, 33, 150, 243), Color(255, 13, 71, 161),   // 蓝色
                Color(255, 66, 165, 245), Color(255, 33, 150, 243),
                Color(255, 255, 255, 255), Color(255, 10, 58, 131)};
    case kBtnDelete:
        return {Color(255, 244, 67, 54), Color(255, 211, 47, 47),   // 红色
                Color(255, 246, 111, 92), Color(255, 244, 67, 54),
                Color(255, 255, 255, 255), Color(255, 198, 40, 40)};
    case kBtnCopy:
        return {Color(255, 255, 152, 0), Color(255, 255, 111, 0),    // 黄色
                Color(255, 255, 175, 51), Color(255, 255, 152, 0),
                Color(255, 33, 33, 33), Color(255, 255, 81, 0)};
    case kBtnClose:
        return {Color(255, 120, 120, 120), Color(255, 80, 80, 80),   // 灰色
                Color(255, 150, 150, 150), Color(255, 120, 120, 120),
                Color(255, 255, 255, 255), Color(255, 60, 60, 60)};
    default:
        return {Color(255, 100, 100, 100), Color(255, 60, 60, 60),    // 默认灰色
                Color(255, 130, 130, 130), Color(255, 100, 100, 100),
                Color(255, 255, 255, 255), Color(255, 50, 50, 50)};
    }
}

LRESULT CALLBACK customBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);

        RECT rc;
        GetClientRect(hwnd, &rc);
        REAL width = (REAL)(rc.right - rc.left);
        REAL height = (REAL)(rc.bottom - rc.top);

        bool hovered = false;
        POINT cur;
        GetCursorPos(&cur);
        ScreenToClient(hwnd, &cur);
        hovered = (cur.x >= 0 && cur.x < (int)width && cur.y >= 0 && cur.y < (int)height);

        bool pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        BtnStyle style = getBtnStyle((int)GetWindowLongPtr(hwnd, GWLP_ID));

        Color start = (hovered && pressed) ? style.hoverEnd : (hovered ? style.hoverStart : style.bgStart);
        Color end = (hovered && pressed) ? style.bgStart : (hovered ? style.hoverEnd : style.bgEnd);

        // 圆角矩形背景
        RectF rect(0.0f, 0.0f, width, height);
        float radius = 6.0f;
        GraphicsPath path;
        path.AddArc(0.0f, 0.0f, radius * 2, radius * 2, 180, 90);
        path.AddArc(width - radius * 2, 0.0f, radius * 2, radius * 2, 270, 90);
        path.AddArc(width - radius * 2, height - radius * 2, radius * 2, radius * 2, 0, 90);
        path.AddArc(0.0f, height - radius * 2, radius * 2, radius * 2, 90, 90);
        path.CloseAllFigures();

        LinearGradientBrush brush(rect, start, end, LinearGradientModeVertical);
        g.FillPath(&brush, &path);

        // 边框
        Pen pen(style.border, 1.5f);
        g.DrawPath(&pen, &path);

        // 按钮文字
        wchar_t text[64];
        GetWindowTextW(hwnd, text, 64);
        FontFamily fontFamily(L"Microsoft YaHei UI");
        Font font(&fontFamily, 14, FontStyleBold, UnitPixel);
        SolidBrush textBrush(style.text);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(text, -1, &font, rect, &fmt, &textBrush);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSELEAVE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        InvalidateRect(hwnd, nullptr, FALSE);
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        InvalidateRect(hwnd, nullptr, FALSE);
        ReleaseCapture();
        // 向父窗口发送 WM_COMMAND 消息（模拟标准按钮行为）
        HWND parent = GetParent(hwnd);
        if (parent) {
            SendMessageW(parent, WM_COMMAND,
                MAKELONG((UINT)GetWindowLongPtr(hwnd, GWLP_ID), BN_CLICKED),
                (LPARAM)hwnd);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void registerCustomBtnClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = customBtnProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCustomBtnClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
}

HWND createCustomButton(HINSTANCE hInst, HWND parent, int id, LPCWSTR text, int x, int y, int w, int h) {
    return CreateWindowExW(0, kCustomBtnClass, text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h,
        parent, (HMENU)(INT_PTR)id, hInst, nullptr);
}

}  // namespace

// 外部 logMsg（从 main.cpp 暴露）
extern void logMsg(const wchar_t* msg);

void PhraseEditDialog::show(HINSTANCE hInst) {
    // 防止重复打开
    HWND existing = FindWindowW(kClassName, L"编辑常用语");
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }

    // 在新线程中创建窗口（不阻塞调用方）
    HANDLE hThread = CreateThread(nullptr, 0, threadProc, (LPVOID)hInst, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    }
}

DWORD WINAPI PhraseEditDialog::threadProc(LPVOID param) {
    HINSTANCE hInst = (HINSTANCE)param;
    logMsg(L"[EditDialog] threadProc start");

    // 初始化 GDI+
    initBtnGdiPlus();

    // 注册自定义按钮类
    registerCustomBtnClass(hInst);

    // 注册窗口类（线程内）
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
        swprintf_s(buf, L"[EditDialog] RegisterClass FAILED, err=%u", err);
        logMsg(buf);
        return 1;
    }
    logMsg(L"[EditDialog] class registered");

    // 居中显示
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int w = 720, h = 720;  // 窗口 720px，按钮宽 160px 不会超出
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;
    if (y < 0) y = 0;  // 避免屏幕外

    HWND hwnd = CreateWindowExW(
        0,
        kClassName, L"编辑常用语",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        x, y, w, h,
        nullptr,  // 独立顶级窗口
        nullptr, hInst, nullptr
    );
    if (!hwnd) return 1;

    // 强制置顶（让用户在测试截图时能看到）
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);

    // 此线程独立的消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!IsWindow(hwnd)) break;
    }

    // 关闭窗口前反注册类
    UnregisterClassW(kClassName, hInst);
    UnregisterClassW(kCustomBtnClass, hInst);
    shutdownBtnGdiPlus();
    return 0;
}

LRESULT CALLBACK PhraseEditDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        // 客户区总高度：720 - 30(标题栏) - 8(边框) = ~682
        // 布局：
        //   y=10  : 标题提示            (h=20)
        //   y=35  : 列表                (h=480, 到 y=515, 显示 ~15 条)
        //   y=530 : "New / edit text"   (h=20)
        //   y=555 : 编辑框              (h=120, 到 y=675, 更大, 多行)
        //   下方  : 按钮

        // 顶部说明（中文）
        CreateWindowExW(0, L"STATIC",
            L"点击列表项编辑，或在下方输入新文本后点击「添加」（编辑框内按回车换行）：",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 8, 700, 20, hwnd, nullptr, hInst, nullptr);

        // 列表（左大半区，y=35~515，高=480，约可显示15条）
        hList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_GROUP |
            LBS_NOTIFY | LBS_HASSTRINGS | LBS_DISABLENOSCROLL,
            12, 32, 540, 480, hwnd, (HMENU)kListId, hInst, nullptr);

        // 编辑框标签（y=530，中文）
        CreateWindowExW(0, L"STATIC", L"新建/编辑文本（回车换行，Tab切换字段）：",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 530, 540, 20, hwnd, nullptr, hInst, nullptr);

        // 编辑框（y=555, h=120, w=540, 多行+自动换行+无水平滚动条）
        hEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            ES_LEFT | ES_MULTILINE | ES_WANTRETURN |
            ES_AUTOVSCROLL | ES_NOHIDESEL,
            12, 555, 540, 120, hwnd, (HMENU)kEditId, hInst, nullptr);

        // 二次确认样式（64位安全）
        LONG_PTR style = GetWindowLongPtr(hEdit_, GWL_STYLE);
        // 确保：多行 + Enter换行 + 自动垂直滚动 + 无水平滚动 + 自动换行
        style |= (ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL);
        style &= ~(LONG_PTR)ES_AUTOHSCROLL;  // 移除水平滚动（启用自动换行）
        style &= ~(LONG_PTR)WS_HSCROLL;       // 移除水平滚动条
        SetWindowLongPtr(hEdit_, GWL_STYLE, style);
        InvalidateRect(hEdit_, nullptr, TRUE);

        // 按钮 - 右侧一列（中文按钮）
        // 按钮区域：x=560, w=150 (窗口宽 720，按钮右边界 710，剩余 10px 边距)
        // 按钮 7 个：添加/更新/删除/复制/上移/下移/打开备份/关闭 = 8 个
        struct { int id; LPCWSTR text; int y; } buttons[] = {
            {kBtnAdd,    L"添加",     32},
            {kBtnUpdate, L"更新",     86},
            {kBtnDelete, L"删除",    140},
            {kBtnCopy,   L"复制",    194},
            {kBtnUp,     L"上移",    248},
            {kBtnDown,   L"下移",    302},
            {kBtnOpenBak,L"打开备份", 356},
            {kBtnClose,  L"关闭",    620},
        };
        for (auto& b : buttons) {
            createCustomButton(hInst, hwnd, b.id, b.text,
                560, b.y, 150, 46);
        }

        // 加载数据并刷新列表
        auto& mgr = PhraseManager::instance();
        mgr.load();
        refreshList(hList_);

        // 初始焦点设在编辑框
        SetFocus(hEdit_);
        logMsg(L"[EditDialog] WM_CREATE end OK");
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        auto& mgr = PhraseManager::instance();

        // 列表选中项变化 → 加载到编辑框
        if (id == kListId && code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                wchar_t buf[2048];
                SendMessageW(hList_, LB_GETTEXT, sel, (LPARAM)buf);
                SetWindowTextW(hEdit_, buf);
            }
            return 0;
        }
        // 列表双击 → 复制到剪贴板 + 关闭
        if (id == kListId && code == LBN_DBLCLK) {
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                wchar_t buf[2048];
                SendMessageW(hList_, LB_GETTEXT, sel, (LPARAM)buf);
                if (WinHelper::copyToClipboard(buf)) {
                    logMsg(L"Copied phrase via double-click");
                    MessageBoxW(hwnd, L"已复制到剪贴板！", L"成功", MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }

        switch (id) {
        case kBtnAdd: {
            wchar_t buf[2048];
            GetWindowTextW(hEdit_, buf, 2048);
            if (buf[0]) {
                mgr.add(buf);
                refreshList(hList_);
                SetWindowTextW(hEdit_, L"");
            } else {
                MessageBoxW(hwnd, L"请输入内容", L"提示", MB_OK);
            }
            return 0;
        }
        case kBtnUpdate: {
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            wchar_t buf[2048];
            GetWindowTextW(hEdit_, buf, 2048);
            if (sel >= 0 && buf[0]) {
                mgr.update(sel, buf);
                refreshList(hList_);
            } else {
                MessageBoxW(hwnd, L"请先选中列表项并输入内容", L"提示", MB_OK);
            }
            return 0;
        }
        case kBtnDelete: {
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                if (MessageBoxW(hwnd, L"确定删除？", L"确认",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    mgr.remove(sel);
                    refreshList(hList_);
                    SetWindowTextW(hEdit_, L"");
                }
            } else {
                MessageBoxW(hwnd, L"请先选中列表项", L"提示", MB_OK);
            }
            return 0;
        }
        case kBtnCopy: {
            // 优先级 1: 列表选中项
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            wchar_t buf[2048] = {};
            bool fromList = false;
            if (sel >= 0) {
                SendMessageW(hList_, LB_GETTEXT, sel, (LPARAM)buf);
                fromList = true;
            } else {
                // 优先级 2: 编辑框内容
                GetWindowTextW(hEdit_, buf, 2048);
            }
            if (buf[0]) {
                if (WinHelper::copyToClipboard(buf)) {
                    wchar_t msg[128];
                    swprintf_s(msg, L"已复制 (%s)", fromList ? L"列表选中项" : L"编辑框内容");
                    logMsg(msg);
                    MessageBoxW(hwnd, msg, L"成功", MB_OK | MB_ICONINFORMATION);
                } else {
                    logMsg(L"Copy to clipboard FAILED");
                    MessageBoxW(hwnd, L"复制失败", L"错误", MB_OK | MB_ICONERROR);
                }
            } else {
                MessageBoxW(hwnd, L"没有内容可复制\n请先选中列表项或在编辑框输入", L"提示", MB_OK);
            }
            return 0;
        }
        case kBtnUp: {
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            if (sel > 0) {
                mgr.moveUp(sel);
                refreshList(hList_);
                SendMessageW(hList_, LB_SETCURSEL, sel - 1, 0);
            }
            return 0;
        }
        case kBtnDown: {
            int sel = (int)SendMessageW(hList_, LB_GETCURSEL, 0, 0);
            int count = (int)SendMessageW(hList_, LB_GETCOUNT, 0, 0);
            if (sel >= 0 && sel < count - 1) {
                mgr.moveDown(sel);
                refreshList(hList_);
                SendMessageW(hList_, LB_SETCURSEL, sel + 1, 0);
            }
            return 0;
        }
        // kBtnReset 已移除（用户要求去掉恢复默认）
        case kBtnOpenBak: {
            // 打开备份目录（资源管理器）
            mgr.backup(L"manual");  // 手动备份一次当前内容
            mgr.openBackupDir();
            wchar_t msg[256];
            swprintf_s(msg, L"已打开备份目录\n\n每次修改都会自动备份到：\n%s\\bak",
                mgr.getBackupDir().c_str());
            MessageBoxW(hwnd, msg, L"备份目录", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case kBtnClose: {
            DestroyWindow(hwnd);
            return 0;
        }
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void PhraseEditDialog::refreshList(HWND hList) {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    auto phrases = PhraseManager::instance().getAll();
    for (const auto& p : phrases) {
        // 不加序号，直接显示原文
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)p.c_str());
    }
}
