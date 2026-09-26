// llm_settings_dialog.cpp - LLM 设置对话框实现
#include "llm_settings_dialog.h"
#include "config.h"
#include "llm_client.h"
#include "window_helper.h"
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")

LPCWSTR LlmSettingsDialog::kClassName = L"LlmSettingsDialogClass";

extern void logMsg(const wchar_t* msg);

namespace {
constexpr int kEditBaseUrlId   = 1001;
constexpr int kEditApiKeyId    = 1002;
constexpr int kEditModelId     = 1003;
constexpr int kEditTargetLangId= 1004;
constexpr int kBtnSaveId       = 1010;
constexpr int kBtnCancelId     = 1011;
constexpr int kBtnTestId       = 1012;
constexpr int kBtnShowKeyId    = 1013;  // 显示/隐藏 API key
constexpr int kStaticStatusId  = 1020;

// 辅助：更新状态栏文字
void setStatus(HWND hwnd, const wchar_t* text) {
    HWND hStatic = GetDlgItem(hwnd, kStaticStatusId);
    if (hStatic) SetWindowTextW(hStatic, text);
}
}

void LlmSettingsDialog::show(HINSTANCE hInst) {
    HWND existing = FindWindowW(kClassName, L"LLM 设置");
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }
    HANDLE hThread = CreateThread(nullptr, 0, threadProc, (LPVOID)hInst, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

DWORD WINAPI LlmSettingsDialog::threadProc(LPVOID param) {
    HINSTANCE hInst = (HINSTANCE)param;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int w = 640, h = 420;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    // ========== 菜单栏（复用按钮 ID） ==========
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, kBtnTestId,   L"测试连接 (&T)\tF5");
    AppendMenuW(hFileMenu, MF_STRING, kBtnSaveId,   L"保存 (&S)\tCtrl+S");
    AppendMenuW(hFileMenu, MF_STRING, kBtnCancelId, L"取消 (&Q)\tEsc");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"操作 (&A)");

    HMENU hViewMenu = CreatePopupMenu();
    AppendMenuW(hViewMenu, MF_STRING, kBtnShowKeyId, L"显示/隐藏 API Key (&K)");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hViewMenu, L"视图 (&V)");

    HWND hwnd = CreateWindowExW(
        0,
        kClassName, L"LLM 设置",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) {
        DestroyMenu(hMenuBar);
        return 1;
    }
    SetMenu(hwnd, hMenuBar);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!IsWindow(hwnd)) break;
    }
    UnregisterClassW(kClassName, hInst);
    return 0;
}

void LlmSettingsDialog::loadToUI(HWND hwnd) {
    AppConfig& cfg = AppConfig::instance();
    SetWindowTextW(GetDlgItem(hwnd, kEditBaseUrlId),    cfg.llmBaseUrl().c_str());
    SetWindowTextW(GetDlgItem(hwnd, kEditApiKeyId),     cfg.llmApiKey().c_str());
    SetWindowTextW(GetDlgItem(hwnd, kEditModelId),      cfg.llmModel().c_str());
    SetWindowTextW(GetDlgItem(hwnd, kEditTargetLangId), cfg.translateTargetLang().c_str());
}

void LlmSettingsDialog::saveFromUI(HWND hwnd) {
    wchar_t buf[2048];
    AppConfig& cfg = AppConfig::instance();

    GetWindowTextW(GetDlgItem(hwnd, kEditBaseUrlId), buf, 2048);
    cfg.setLlmBaseUrl(buf);
    GetWindowTextW(GetDlgItem(hwnd, kEditApiKeyId), buf, 2048);
    cfg.setLlmApiKey(buf);
    GetWindowTextW(GetDlgItem(hwnd, kEditModelId), buf, 2048);
    cfg.setLlmModel(buf);
    GetWindowTextW(GetDlgItem(hwnd, kEditTargetLangId), buf, 2048);
    cfg.setTranslateTargetLang(buf);

    cfg.save();
}

// 测试连接（在调用线程里同步执行，会卡住几秒；调用前已切到后台）
void LlmSettingsDialog::doTestConnection(HWND hwnd) {
    AppConfig& cfg = AppConfig::instance();

    // 先把界面上的值保存下来再测试（也可以不保存直接读界面）
    saveFromUI(hwnd);

    setStatus(hwnd, L"正在测试连接...");
    HWND hStatic = GetDlgItem(hwnd, kStaticStatusId);
    if (hStatic) UpdateWindow(hStatic);

    std::wstring err;
    std::wstring resp = LlmClient::chatCompletion(
        cfg.llmBaseUrl(),
        cfg.llmApiKey(),
        cfg.llmModel(),
        L"You are a ping bot.",
        L"reply with one word: pong",
        &err);

    if (!err.empty()) {
        wchar_t msg[512];
        swprintf_s(msg, L"❌ 测试失败：\n%s", err.c_str());
        MessageBoxW(hwnd, msg, L"LLM 测试", MB_ICONERROR);
        setStatus(hwnd, L"❌ 测试失败");
    } else if (resp.empty()) {
        MessageBoxW(hwnd, L"⚠ 响应为空", L"LLM 测试", MB_ICONWARNING);
        setStatus(hwnd, L"⚠ 响应为空");
    } else {
        std::wstring preview = resp;
        if (preview.size() > 200) preview = preview.substr(0, 200) + L"...";
        wchar_t msg[512];
        swprintf_s(msg, L"✅ 连接成功！\n\n响应：\n%s", preview.c_str());
        MessageBoxW(hwnd, msg, L"LLM 测试", MB_ICONINFORMATION);
        setStatus(hwnd, L"✅ 连接成功");
    }
}

LRESULT CALLBACK LlmSettingsDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        int x = 14;
        int labelW = 90;
        int editX = x + labelW + 8;
        int editW = 540 - editX - 14;

        // 顶部说明
        CreateWindowExW(0, L"STATIC",
            L"配置 OpenAI 兼容的 LLM 接口（适用于大多数 GPT/Qwen/DeepSeek 等服务）",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, 12, 532, 20, hwnd, nullptr, hInst, nullptr);

        int rowY = 44;
        int rowH = 30;

        auto mkLabel = [&](LPCWSTR text, int y) {
            CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                x, y + 4, labelW, 22, hwnd, nullptr, hInst, nullptr);
        };
        auto mkEdit = [&](int id, int y, bool password) {
            DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
            if (password) style |= ES_PASSWORD;
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
                editX, y, editW, 24, hwnd, (HMENU)id, hInst, nullptr);
        };

        mkLabel(L"Base URL:", rowY); mkEdit(kEditBaseUrlId, rowY, false); rowY += rowH;
        mkLabel(L"API Key:",  rowY);
        mkEdit(kEditApiKeyId, rowY, true);
        // 显示/隐藏 API Key 按钮（贴右侧）
        int btnW = 60;
        CreateWindowExW(0, L"BUTTON", L"显示", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            editX + editW - btnW, rowY, btnW, 24, hwnd, (HMENU)kBtnShowKeyId, hInst, nullptr);
        rowY += rowH;
        mkLabel(L"模型名:",   rowY); mkEdit(kEditModelId, rowY, false); rowY += rowH;
        mkLabel(L"目标语言:", rowY); mkEdit(kEditTargetLangId, rowY, false); rowY += rowH;

        // 状态栏
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, rowY + 4, 532, 20, hwnd, (HMENU)kStaticStatusId, hInst, nullptr);
        rowY += 28;

        // 测试连接按钮
        CreateWindowExW(0, L"BUTTON", L"测试连接", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, rowY, 100, 30, hwnd, (HMENU)kBtnTestId, hInst, nullptr);

        // 保存 / 取消
        int bx = 540 - 14 - 90;
        CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            bx - 100 - 8, rowY, 90, 30, hwnd, (HMENU)kBtnSaveId, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            bx, rowY, 90, 30, hwnd, (HMENU)kBtnCancelId, hInst, nullptr);

        loadToUI(hwnd);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == kBtnSaveId) {
            saveFromUI(hwnd);
            DestroyWindow(hwnd);
        } else if (id == kBtnCancelId) {
            DestroyWindow(hwnd);
        } else if (id == kBtnTestId) {
            doTestConnection(hwnd);
        } else if (id == kBtnShowKeyId) {
            // 切换 API Key 编辑框的密码遮罩
            HWND hKey = GetDlgItem(hwnd, kEditApiKeyId);
            if (hKey) {
                LONG_PTR style = GetWindowLongPtr(hKey, GWL_STYLE);
                if (style & ES_PASSWORD) {
                    style &= ~ES_PASSWORD;
                    SetWindowTextW(GetDlgItem(hwnd, kBtnShowKeyId), L"隐藏");
                } else {
                    style |= ES_PASSWORD;
                    SetWindowTextW(GetDlgItem(hwnd, kBtnShowKeyId), L"显示");
                }
                SetWindowLongPtr(hKey, GWL_STYLE, style);
                InvalidateRect(hKey, nullptr, TRUE);
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