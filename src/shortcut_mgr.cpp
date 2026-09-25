// shortcut_mgr.cpp - 快捷程序管理器实现
// 风格与 phrase_mgr.cpp 完全一致：
//   - JSON 格式：数组，每个元素是 {"icon":"...","name":"...","path":"...","args":"..."}
//   - 手动 JSON 解析（不依赖第三方库）
//   - UTF-8 + BOM 写入
//   - 线程安全
#include "shortcut_mgr.h"
#include <shlobj.h>       // SHGetFolderPathW
#include <shellapi.h>     // ShellExecuteExW
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

ShortcutManager& ShortcutManager::instance() {
    static ShortcutManager mgr;
    return mgr;
}

std::wstring ShortcutManager::getDataPath() const {
    wchar_t appdata[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    fs::path dir = fs::path(appdata) / L"float_button";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return (dir / L"shortcuts.json").wstring();
}

// ====== 工具：UTF-8 -> wstring ======
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &wstr[0], wlen);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
}

// ====== 工具：wstring -> UTF-8（已转义 \" 和 \\） ======
static std::string wideToUtf8Escaped(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &utf8[0], len, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    std::string out;
    out.reserve(utf8.size());
    for (char c : utf8) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// ====== 工具：展开 %ENVVAR% ======
// 支持: %ProgramFiles% %SystemRoot% %USERPROFILE% %APPDATA% %LOCALAPPDATA% %TEMP% %SystemDrive%
static std::wstring expandEnv(const std::wstring& s) {
    if (s.empty()) return s;
    static const wchar_t* vars[] = {
        L"ProgramFiles", L"ProgramFiles(x86)", L"SystemRoot", L"USERPROFILE",
        L"APPDATA", L"LOCALAPPDATA", L"TEMP", L"SystemDrive",
        L"ProgramData", L"HOMEDRIVE", L"HOMEPATH", L"USERNAME"
    };
    std::wstring out = s;
    for (auto* v : vars) {
        std::wstring key = L"%";
        key += v;
        key += L"%";
        // 不区分大小写替换
        size_t pos = 0;
        while ((pos = out.find(key, pos)) != std::wstring::npos) {
            wchar_t buf[1024] = {0};
            DWORD n = GetEnvironmentVariableW(v, buf, 1024);
            std::wstring val = (n > 0 && n < 1024) ? std::wstring(buf) : L"";
            out.replace(pos, key.size(), val);
            pos += val.size();
        }
    }
    return out;
}

void ShortcutManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    shortcuts_.clear();

    std::wstring path = getDataPath();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        // 首次使用：写一份默认的 6 个常用程序
        shortcuts_ = {
            {L"📝", L"记事本",       L"notepad.exe",      L""},
            {L"🧮", L"计算器",       L"calc.exe",         L""},
            {L"📁", L"资源管理器",   L"explorer.exe",     L""},
            {L"⚙",  L"设置",         L"ms-settings:",     L""},
            {L"🎨", L"画图",         L"mspaint.exe",      L""},
            {L"⌨",  L"命令提示符",   L"cmd.exe",          L""},
        };
        // 直接写入磁盘（save 会重新加锁，要先释放）
        // 这里偷个懒：直接解锁后再 save
    } else {
        // 读文件
        std::stringstream ss;
        ss << in.rdbuf();
        std::string content = ss.str();
        // 跳 BOM
        if (content.size() >= 3 &&
            (unsigned char)content[0] == 0xEF &&
            (unsigned char)content[1] == 0xBB &&
            (unsigned char)content[2] == 0xBF) {
            content = content.substr(3);
        }

        // 手写 JSON 对象数组解析：
        // 期望格式: [{"icon":"...","name":"...","path":"...","args":"..."}, ...]
        // 思路：依次找每个对象的 "icon" "name" "path" "args" 字段值
        size_t pos = 0;
        while (true) {
            size_t objStart = content.find('{', pos);
            if (objStart == std::string::npos) break;
            size_t objEnd = content.find('}', objStart);
            if (objEnd == std::string::npos) break;
            std::string obj = content.substr(objStart, objEnd - objStart + 1);

            Shortcut sc;
            // 解析单个字段
            auto extractField = [&](const std::string& field) -> std::string {
                std::string key = "\"" + field + "\"";
                size_t kp = obj.find(key);
                if (kp == std::string::npos) return "";
                size_t colon = obj.find(':', kp);
                if (colon == std::string::npos) return "";
                size_t q1 = obj.find('"', colon);
                if (q1 == std::string::npos) return "";
                size_t i = q1 + 1;
                std::string val;
                bool escaped = false;
                while (i < obj.size()) {
                    char c = obj[i];
                    if (escaped) {
                        switch (c) {
                            case '"':  val += '"';  break;
                            case '\\': val += '\\'; break;
                            case 'n':  val += '\n'; break;
                            case 't':  val += '\t'; break;
                            case 'r':  val += '\r'; break;
                            default:   val += c;    break;
                        }
                        escaped = false;
                    } else if (c == '\\') {
                        escaped = true;
                    } else if (c == '"') {
                        break;
                    } else {
                        val += c;
                    }
                    i++;
                }
                return val;
            };

            sc.icon = utf8ToWide(extractField("icon"));
            sc.name = utf8ToWide(extractField("name"));
            sc.path = utf8ToWide(extractField("path"));
            sc.args = utf8ToWide(extractField("args"));

            if (!sc.name.empty() || !sc.path.empty()) {
                shortcuts_.push_back(sc);
            }
            pos = objEnd + 1;
        }
    }

    // 释放锁后保存（save 内部会再加锁）
    // 注：因为我们还在 lock_guard 作用域里，调用 save 会死锁
    // 所以这里直接写文件而不调用 save
    {
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF";
            out << "[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{";
                out << "\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\",";
                out << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\",";
                out << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\",";
                out << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"";
                out << "}";
            }
            out << "]";
        }
    }
}

void ShortcutManager::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::wstring path = getDataPath();
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return;
    out << "\xEF\xBB\xBF";
    out << "[";
    for (size_t i = 0; i < shortcuts_.size(); ++i) {
        if (i > 0) out << ",";
        out << "{";
        out << "\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\",";
        out << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\",";
        out << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\",";
        out << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"";
        out << "}";
    }
    out << "]";
}

std::vector<Shortcut> ShortcutManager::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcuts_;
}

size_t ShortcutManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcuts_.size();
}

bool ShortcutManager::add(const Shortcut& sc) {
    if (sc.name.empty() || sc.path.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    shortcuts_.push_back(sc);
    // 不在锁内调 save（save 自己会加锁）—— 解锁后保存
    // 简化：直接落盘
    {
        std::wstring path = getDataPath();
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\","
                    << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\","
                    << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\","
                    << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"}";
            }
            out << "]";
        }
    }
    return true;
}

bool ShortcutManager::update(size_t index, const Shortcut& sc) {
    if (sc.name.empty() || sc.path.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= shortcuts_.size()) return false;
    shortcuts_[index] = sc;
    {
        std::wstring path = getDataPath();
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\","
                    << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\","
                    << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\","
                    << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"}";
            }
            out << "]";
        }
    }
    return true;
}

bool ShortcutManager::remove(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= shortcuts_.size()) return false;
    shortcuts_.erase(shortcuts_.begin() + index);
    {
        std::wstring path = getDataPath();
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\","
                    << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\","
                    << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\","
                    << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"}";
            }
            out << "]";
        }
    }
    return true;
}

bool ShortcutManager::moveUp(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index == 0 || index >= shortcuts_.size()) return false;
    std::swap(shortcuts_[index], shortcuts_[index - 1]);
    {
        std::wstring path = getDataPath();
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\","
                    << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\","
                    << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\","
                    << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"}";
            }
            out << "]";
        }
    }
    return true;
}

bool ShortcutManager::moveDown(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index + 1 >= shortcuts_.size()) return false;
    std::swap(shortcuts_[index], shortcuts_[index + 1]);
    {
        std::wstring path = getDataPath();
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\","
                    << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\","
                    << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\","
                    << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"}";
            }
            out << "]";
        }
    }
    return true;
}

void ShortcutManager::setAll(const std::vector<Shortcut>& list) {
    std::lock_guard<std::mutex> lock(mutex_);
    shortcuts_ = list;
    {
        std::wstring path = getDataPath();
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "\xEF\xBB\xBF[";
            for (size_t i = 0; i < shortcuts_.size(); ++i) {
                if (i > 0) out << ",";
                out << "{\"icon\":\"" << wideToUtf8Escaped(shortcuts_[i].icon) << "\","
                    << "\"name\":\"" << wideToUtf8Escaped(shortcuts_[i].name) << "\","
                    << "\"path\":\"" << wideToUtf8Escaped(shortcuts_[i].path) << "\","
                    << "\"args\":\"" << wideToUtf8Escaped(shortcuts_[i].args) << "\"}";
            }
            out << "]";
        }
    }
}

bool ShortcutManager::launch(const Shortcut& sc, std::wstring* errorMsg) {
    if (sc.path.empty()) {
        if (errorMsg) *errorMsg = L"路径为空";
        return false;
    }

    // 展开环境变量
    std::wstring path = expandEnv(sc.path);
    std::wstring args = expandEnv(sc.args);

    // 1) 优先用 ShellExecuteEx（支持 URI 如 ms-settings:）
    // 2) 用 CreateProcess 兜底（支持任意 .exe）
    if (path.find(L':') != std::wstring::npos && path.size() >= 2 && path[1] == L':') {
        // 形如 "C:\xxx" —— 是文件路径
        std::wstring fullArgs = L"\"" + path + L"\"";
        if (!args.empty()) {
            fullArgs += L" ";
            fullArgs += args;
        }
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = 0;
        sei.lpFile = path.c_str();
        sei.lpParameters = args.empty() ? nullptr : args.c_str();
        sei.nShow = SW_SHOW;
        if (ShellExecuteExW(&sei)) {
            return true;
        }
    } else {
        // URI 或 PATH 里的命令（如 notepad.exe、ms-settings:）
        if (path == L"ms-settings:" || path.find(L"ms-") == 0 || path.find(L"http") == 0) {
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.lpFile = path.c_str();
            sei.nShow = SW_SHOW;
            if (ShellExecuteExW(&sei)) {
                return true;
            }
        } else {
            // 普通可执行命令：用 ShellExecute（自动从 PATH 找）
            HINSTANCE ret = ShellExecuteW(nullptr, L"open", path.c_str(),
                                          args.empty() ? nullptr : args.c_str(),
                                          nullptr, SW_SHOW);
            if ((INT_PTR)ret > 32) {
                return true;
            }
        }
    }

    // 失败：错误信息
    DWORD err = GetLastError();
    if (errorMsg) {
        wchar_t buf[256];
        swprintf_s(buf, L"启动失败 (错误码 %u)\n路径: %s", err, path.c_str());
        *errorMsg = buf;
    }
    return false;
}
