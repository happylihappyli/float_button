// shortcut_edit_dialog.cpp - 快捷程序编辑窗口实现
// 风格与 phrase_edit_dialog.cpp 类似，但用系统标准 ListView + BUTTON，不画自定义 GDI+ 按钮
// 让代码更短、更易维护
#include "shortcut_edit_dialog.h"
#include "shortcut_mgr.h"
#include "window_helper.h"
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <sstream>

#pragma comment(lib, "comctl32.lib")

LPCWSTR ShortcutEditDialog::kClassName = L"ShortcutEditDialogClass";

namespace {
constexpr int kListId         = 1001;  // ListView 控件
constexpr int kEditIconId     = 1010;  // 图标输入框
constexpr int kEditNameId     = 1011;  // 名称输入框
constexpr int kEditPathId     = 1012;  // 路径输入框
constexpr int kEditArgsId     = 1013;  // 参数输入框
constexpr int kBtnAddId       = 1020;  // 新增
constexpr int kBtnUpdateId    = 1021;  // 修改
constexpr int kBtnDeleteId    = 1022;  // 删除
constexpr int kBtnUpId        = 1023;  // 上移
constexpr int kBtnDownId      = 1024;  // 下移
constexpr int kBtnBrowseId    = 1025;  // 浏览文件
constexpr int kBtnTestId      = 1026;  // 测试启动
constexpr int kBtnCloseId     = 1027;  // 关闭
constexpr int kBtnSaveAllId   = 1028;  // 保存（将所有修改一次性提交）

// 状态：编辑窗内未保存的修改
struct EditState {
    std::vector<Shortcut> working;  // 编辑中的副本（每次启动窗口时复制）
    int selectedIndex = -1;         // 当前选中的行
};
EditState g_state;

// 工具：宽字符串 -> UTF-8 给 ListView 用
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &wstr[0], wlen);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
}
}  // namespace

void ShortcutEditDialog::show(HINSTANCE hInst) {
    // 防止重复打开
    HWND existing = FindWindowW(kClassName, L"Edit Shortcuts");
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }
    HANDLE hThread = CreateThread(nullptr, 0, threadProc, (LPVOID)hInst, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

DWORD WINAPI ShortcutEditDialog::threadProc(LPVOID param) {
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
    int w = 720, h = 480;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    // 复制当前配置到 working
    g_state.working = ShortcutManager::instance().getAll();
    g_state.selectedIndex = -1;

    HWND hwnd = CreateWindowExW(
        0, kClassName, L"Edit Shortcuts",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX,
        x, y, w, h,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 1;

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

void ShortcutEditDialog::refreshList(HWND hList) {
    ListView_DeleteAllItems(hList);
    for (size_t i = 0; i < g_state.working.size(); ++i) {
        const auto& sc = g_state.working[i];
        LVITEMW li{};
        li.mask = LVIF_TEXT;
        li.iItem = (int)i;
        li.iSubItem = 0;
        li.pszText = (LPWSTR)sc.icon.c_str();
        int idx = ListView_InsertItem(hList, &li);

        ListView_SetItemText(hList, idx, 1, (LPWSTR)sc.name.c_str());
        ListView_SetItemText(hList, idx, 2, (LPWSTR)sc.path.c_str());
        ListView_SetItemText(hList, idx, 3, (LPWSTR)sc.args.c_str());
    }
    if (g_state.selectedIndex >= 0 && g_state.selectedIndex < (int)g_state.working.size()) {
        ListView_SetItemState(hList, g_state.selectedIndex,
            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void ShortcutEditDialog::loadSelectionToForm(HWND hwnd, int selIndex) {
    if (selIndex < 0 || selIndex >= (int)g_state.working.size()) {
        clearForm(hwnd);
        return;
    }
    const auto& sc = g_state.working[selIndex];
    SetWindowTextW(GetDlgItem(hwnd, kEditIconId), sc.icon.c_str());
    SetWindowTextW(GetDlgItem(hwnd, kEditNameId), sc.name.c_str());
    SetWindowTextW(GetDlgItem(hwnd, kEditPathId), sc.path.c_str());
    SetWindowTextW(GetDlgItem(hwnd, kEditArgsId), sc.args.c_str());
}

void ShortcutEditDialog::clearForm(HWND hwnd) {
    SetWindowTextW(GetDlgItem(hwnd, kEditIconId), L"");
    SetWindowTextW(GetDlgItem(hwnd, kEditNameId), L"");
    SetWindowTextW(GetDlgItem(hwnd, kEditPathId), L"");
    SetWindowTextW(GetDlgItem(hwnd, kEditArgsId), L"");
}

void ShortcutEditDialog::applyFormToManager(HWND hwnd, int selIndex) {
    if (selIndex < 0 || selIndex >= (int)g_state.working.size()) return;
    wchar_t buf[1024];
    Shortcut sc;
    GetWindowTextW(GetDlgItem(hwnd, kEditIconId), buf, 1024); sc.icon = buf;
    GetWindowTextW(GetDlgItem(hwnd, kEditNameId), buf, 1024); sc.name = buf;
    GetWindowTextW(GetDlgItem(hwnd, kEditPathId), buf, 1024); sc.path = buf;
    GetWindowTextW(GetDlgItem(hwnd, kEditArgsId), buf, 1024); sc.args = buf;
    g_state.working[selIndex] = sc;
}

LRESULT CALLBACK ShortcutEditDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        // 标题
        CreateWindowExW(0, L"STATIC", L"快捷程序 - 配置后点击面板对应按钮即可启动",
            WS_CHILD | WS_VISIBLE,
            12, 10, 680, 20, hwnd, nullptr, hInst, nullptr);

        // ====== 左侧：ListView ======
        HWND hList = CreateWindowExW(0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            12, 36, 320, 380, hwnd, (HMENU)kListId, hInst, nullptr);
        ListView_SetExtendedListViewStyle(hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)L"图标"; col.cx = 50; col.iSubItem = 0;
        ListView_InsertColumn(hList, 0, &col);
        col.pszText = (LPWSTR)L"名称"; col.cx = 110; col.iSubItem = 1;
        ListView_InsertColumn(hList, 1, &col);
        col.pszText = (LPWSTR)L"路径"; col.cx = 130; col.iSubItem = 2;
        ListView_InsertColumn(hList, 2, &col);
        col.pszText = (LPWSTR)L"参数"; col.cx = 25; col.iSubItem = 3;
        ListView_InsertColumn(hList, 3, &col);

        refreshList(hList);

        // ====== 右侧：编辑表单 ======
        int fx = 348;
        CreateWindowExW(0, L"STATIC", L"图标 (emoji):", WS_CHILD | WS_VISIBLE,
            fx, 36, 200, 18, hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            fx, 56, 340, 24, hwnd, (HMENU)kEditIconId, hInst, nullptr);

        CreateWindowExW(0, L"STATIC", L"显示名:", WS_CHILD | WS_VISIBLE,
            fx, 88, 200, 18, hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            fx, 108, 340, 24, hwnd, (HMENU)kEditNameId, hInst, nullptr);

        CreateWindowExW(0, L"STATIC", L"程序路径 (支持 %ProgramFiles% 等环境变量):", WS_CHILD | WS_VISIBLE,
            fx, 140, 340, 18, hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            fx, 160, 270, 24, hwnd, (HMENU)kEditPathId, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"浏览...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            fx + 278, 160, 62, 24, hwnd, (HMENU)kBtnBrowseId, hInst, nullptr);

        CreateWindowExW(0, L"STATIC", L"启动参数 (空格分隔):", WS_CHILD | WS_VISIBLE,
            fx, 192, 200, 18, hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            fx, 212, 340, 24, hwnd, (HMENU)kEditArgsId, hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"测试启动",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            fx, 248, 100, 28, hwnd, (HMENU)kBtnTestId, hInst, nullptr);

        // ====== 底部按钮行 ======
        int by = 430;
        int bw = 90, bh = 28, gap = 8;
        int bx = 12;

        auto makeBtn = [&](int id, LPCWSTR text, int x) {
            CreateWindowExW(0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, by, bw, bh, hwnd, (HMENU)id, hInst, nullptr);
            return x + bw;
        };
        bx = makeBtn(kBtnAddId,    L"+ 新增",   bx); bx += gap;
        bx = makeBtn(kBtnUpdateId, L"应用修改",  bx); bx += gap;
        bx = makeBtn(kBtnDeleteId, L"- 删除",   bx); bx += gap;
        bx = makeBtn(kBtnUpId,     L"↑ 上移",   bx); bx += gap;
        bx = makeBtn(kBtnDownId,   L"↓ 下移",   bx); bx += gap;

        // 右下：保存 + 关闭
        CreateWindowExW(0, L"BUTTON", L"保存到磁盘",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            720 - 12 - 2*bw - gap, by, bw, bh, hwnd, (HMENU)kBtnSaveAllId, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            720 - 12 - bw, by, bw, bh, hwnd, (HMENU)kBtnCloseId, hInst, nullptr);
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR p = (LPNMHDR)lParam;
        if (p->idFrom == kListId && p->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW pLv = (LPNMLISTVIEW)lParam;
            if (pLv->uNewState & LVIS_SELECTED) {
                int sel = pLv->iItem;
                g_state.selectedIndex = sel;
                loadSelectionToForm(hwnd, sel);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        HWND hList = GetDlgItem(hwnd, kListId);

        if (id == kBtnBrowseId) {
            // 弹出文件选择对话框
            OPENFILENAMEW ofn{};
            wchar_t file[MAX_PATH] = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"可执行文件 (*.exe;*.bat;*.cmd;*.lnk)\0*.exe;*.bat;*.cmd;*.lnk\0所有文件\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(GetDlgItem(hwnd, kEditPathId), file);
                // 自动填名称
                wchar_t name[256] = {0};
                if (GetWindowTextW(GetDlgItem(hwnd, kEditNameId), name, 256) == 0) {
                    // 取文件名（去后缀）
                    std::wstring f = file;
                    size_t slash = f.find_last_of(L"\\/");
                    if (slash != std::wstring::npos) f = f.substr(slash + 1);
                    size_t dot = f.find_last_of(L'.');
                    if (dot != std::wstring::npos) f = f.substr(0, dot);
                    SetWindowTextW(GetDlgItem(hwnd, kEditNameId), f.c_str());
                }
            }
        } else if (id == kBtnTestId) {
            // 测试启动当前表单
            wchar_t buf[1024];
            Shortcut sc;
            GetWindowTextW(GetDlgItem(hwnd, kEditIconId), buf, 1024); sc.icon = buf;
            GetWindowTextW(GetDlgItem(hwnd, kEditNameId), buf, 1024); sc.name = buf;
            GetWindowTextW(GetDlgItem(hwnd, kEditPathId), buf, 1024); sc.path = buf;
            GetWindowTextW(GetDlgItem(hwnd, kEditArgsId), buf, 1024); sc.args = buf;
            if (sc.path.empty()) {
                MessageBoxW(hwnd, L"请先填写路径", L"提示", MB_ICONINFORMATION);
            } else {
                std::wstring err;
                if (!ShortcutManager::instance().launch(sc, &err)) {
                    MessageBoxW(hwnd, err.c_str(), L"启动失败", MB_ICONERROR);
                } else {
                    MessageBoxW(hwnd, L"已提交启动请求", L"成功", MB_ICONINFORMATION);
                }
            }
        } else if (id == kBtnAddId) {
            // 新增：加到 working 末尾并选中新行
            Shortcut newSc = {L"📌", L"新程序", L"", L""};
            g_state.working.push_back(newSc);
            refreshList(hList);
            g_state.selectedIndex = (int)g_state.working.size() - 1;
            ListView_SetItemState(hList, g_state.selectedIndex,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            // 焦点到名称输入框
            SetFocus(GetDlgItem(hwnd, kEditNameId));
        } else if (id == kBtnUpdateId) {
            // 应用修改：把表单内容写回选中的行
            if (g_state.selectedIndex >= 0) {
                applyFormToManager(hwnd, g_state.selectedIndex);
                refreshList(hList);
            } else {
                MessageBoxW(hwnd, L"请先在列表中选中一行", L"提示", MB_ICONINFORMATION);
            }
        } else if (id == kBtnDeleteId) {
            if (g_state.selectedIndex < 0) {
                MessageBoxW(hwnd, L"请先选中要删除的行", L"提示", MB_ICONINFORMATION);
            } else {
                if (MessageBoxW(hwnd, L"确定要删除选中的快捷程序吗？",
                                L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    g_state.working.erase(g_state.working.begin() + g_state.selectedIndex);
                    g_state.selectedIndex = -1;
                    refreshList(hList);
                    clearForm(hwnd);
                }
            }
        } else if (id == kBtnUpId) {
            if (g_state.selectedIndex > 0) {
                std::swap(g_state.working[g_state.selectedIndex],
                          g_state.working[g_state.selectedIndex - 1]);
                g_state.selectedIndex--;
                refreshList(hList);
            }
        } else if (id == kBtnDownId) {
            if (g_state.selectedIndex >= 0 && g_state.selectedIndex + 1 < (int)g_state.working.size()) {
                std::swap(g_state.working[g_state.selectedIndex],
                          g_state.working[g_state.selectedIndex + 1]);
                g_state.selectedIndex++;
                refreshList(hList);
            }
        } else if (id == kBtnSaveAllId) {
            // 把 working 整体写入磁盘
            ShortcutManager::instance().setAll(g_state.working);
            MessageBoxW(hwnd, L"已保存到 %APPDATA%\\float_button\\shortcuts.json",
                        L"保存成功", MB_ICONINFORMATION);
        } else if (id == kBtnCloseId) {
            DestroyWindow(hwnd);
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
