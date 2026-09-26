// autostart.cpp - 开机自动启动辅助实现
#include "autostart.h"
#include <vector>

namespace AutoStart {

// 获取当前 exe 的完整路径
static std::wstring getExePath() {
    wchar_t path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return std::wstring(path, n);
}

// 用双引号包裹路径（用于注册表 Run 值，支持空格路径）
static std::wstring quotedPath(const std::wstring& p) {
    if (p.empty()) return L"";
    // 如果已经带引号就返回原文
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') return p;
    return L"\"" + p + L"\"";
}

bool enable() {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) {
        // 尝试创建键
        rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                             nullptr, &hKey, nullptr);
        if (rc != ERROR_SUCCESS) return false;
    }

    std::wstring quoted = quotedPath(getExePath());
    if (quoted.empty()) {
        if (hKey) RegCloseKey(hKey);
        return false;
    }

    rc = RegSetValueExW(hKey, kValueName, 0, REG_SZ,
                        (const BYTE*)quoted.c_str(),
                        (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

bool disable() {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) {
        // 键不存在 / 打开失败 = 已经没启用，返回 true
        return true;
    }
    rc = RegDeleteValueW(hKey, kValueName);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

std::wstring getRegisteredPath() {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) return L"";

    std::vector<wchar_t> buf(1024);
    DWORD bufSize = (DWORD)buf.size();
    DWORD type = 0;
    rc = RegQueryValueExW(hKey, kValueName, nullptr, &type,
                          (LPBYTE)buf.data(), &bufSize);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";
    // 去掉尾部 \0
    size_t len = bufSize / sizeof(wchar_t);
    if (len > 0 && buf[len - 1] == L'\0') len--;
    return std::wstring(buf.data(), len);
}

bool isEnabled() {
    std::wstring reg = getRegisteredPath();
    if (reg.empty()) return false;
    // 去掉引号再比较
    std::wstring cur = getExePath();
    std::wstring curQuoted = quotedPath(cur);

    auto unquote = [](const std::wstring& s) -> std::wstring {
        if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    };

    // 大小写不敏感比较（Windows 路径）
    auto ieq = [](const std::wstring& a, const std::wstring& b) -> bool {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            wchar_t ca = a[i]; if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
            wchar_t cb = b[i]; if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
            if (ca != cb) return false;
        }
        return true;
    };

    return ieq(unquote(reg), cur) || ieq(reg, curQuoted);
}

}  // namespace AutoStart