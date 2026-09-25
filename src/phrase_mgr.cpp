// phrase_mgr.cpp - 常用语管理器实现
#include "phrase_mgr.h"
#include <shlobj.h>      // SHGetFolderPathW
#include <shellapi.h>    // ShellExecuteW
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

namespace {
// 默认常用语
const std::vector<std::wstring> kDefaultPhrases = {
    L"好的，收到！",
    L"没问题，我来处理。",
    L"稍等一下，马上好。",
    L"这个我需要确认一下。",
    L"已完成，请查收。",
    L"在吗？",
    L"谢谢！",
    L"辛苦了！",
    L"请问这个怎么处理？",
    L"发我一份，谢谢。",
    L"正在处理中...",
    L"明天再跟进一下。",
    L"收到，我看看。",
    L"好的，没问题～",
    L"这个需要讨论一下。",
};
}  // namespace

PhraseManager& PhraseManager::instance() {
    static PhraseManager mgr;
    return mgr;
}

std::wstring PhraseManager::getDataPath() const {
    wchar_t appdata[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    fs::path dir = fs::path(appdata) / L"float_button";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    return (dir / L"phrases.json").wstring();
}

std::wstring PhraseManager::getBackupDir() const {
    wchar_t appdata[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    fs::path dir = fs::path(appdata) / L"float_button" / L"bak";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    return dir.wstring();
}

// 备份当前 phrases.json 到 bak/phrases_YYYYMMDD_HHMMSS_<reason>.json
// 在每次修改（add/update/remove/move）前调用
void PhraseManager::backup(const wchar_t* reason) {
    std::wstring srcPath = getDataPath();
    std::ifstream in(srcPath, std::ios::binary);
    if (!in.is_open()) return;  // 没有文件就不备份

    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    if (ss.str().empty()) return;

    // 构造时间戳
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[64];
    swprintf_s(ts, L"%04d%02d%02d_%02d%02d%02d_%03d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::wstring backupName = std::wstring(L"phrases_") + ts + L"_" + reason + L".json";
    std::wstring backupPath = getBackupDir() + L"\\" + backupName;

    std::ofstream out(backupPath, std::ios::binary);
    if (!out.is_open()) return;
    out << ss.str();
    out.close();

    // 保留最近 50 个备份，避免无限增长
    namespace fs = std::filesystem;
    std::vector<fs::path> backups;
    for (const auto& entry : fs::directory_iterator(getBackupDir())) {
        if (entry.is_regular_file() && entry.path().filename().wstring().find(L"phrases_") == 0) {
            backups.push_back(entry.path());
        }
    }
    if (backups.size() > 50) {
        // 按修改时间排序，删掉最旧的
        std::sort(backups.begin(), backups.end(), [](const fs::path& a, const fs::path& b) {
            return fs::last_write_time(a) < fs::last_write_time(b);
        });
        for (size_t i = 0; i + 50 < backups.size(); ++i) {
            fs::remove(backups[i]);
        }
    }
}

void PhraseManager::openBackupDir() {
    std::wstring dir = getBackupDir();
    // 用资源管理器打开目录
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOW);
}

void PhraseManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    phrases_.clear();

    std::wstring path = getDataPath();
    std::ifstream in(path);
    if (!in.is_open()) {
        // 首次使用，写入默认
        phrases_ = kDefaultPhrases;
        save();
        return;
    }

    // 读取整个文件
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    // 跳过 UTF-8 BOM（如果有）
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);
    }

    // 手写 JSON 字符串数组解析（正确处理转义字符）
    // 格式: ["phrase1","phrase2",...]
    // 关键：必须反转义 \" -> ", \\ -> \, \n -> 换行 等
    // 否则反复 save/load 会导致反斜杠不断累积
    size_t i = 0;
    size_t n = content.size();
    while (i < n) {
        // 找到字符串开始的引号
        size_t start = content.find('"', i);
        if (start == std::string::npos) break;
        ++start;  // 跳过开始的引号

        // 逐字符解析，直到遇到未转义的结束引号
        std::string utf8;
        bool escaped = false;
        size_t j = start;
        for (; j < n; ++j) {
            char c = content[j];
            if (escaped) {
                // 反转义常见 JSON 转义序列
                switch (c) {
                    case '"':  utf8 += '"';  break;
                    case '\\': utf8 += '\\'; break;
                    case '/':  utf8 += '/';  break;
                    case 'n':  utf8 += '\n'; break;
                    case 't':  utf8 += '\t'; break;
                    case 'r':  utf8 += '\r'; break;
                    case 'b':  utf8 += '\b'; break;
                    case 'f':  utf8 += '\f'; break;
                    default:   utf8 += c;    break;  // 未知转义，保留字符本身
                }
                escaped = false;
            } else if (c == '\\') {
                escaped = true;  // 遇到反斜杠，下一个字符需要反转义
            } else if (c == '"') {
                break;  // 字符串结束
            } else {
                utf8 += c;
            }
        }
        i = j + 1;  // 跳过结束的引号

        // UTF-8 -> UTF-16
        if (!utf8.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                std::wstring wstr(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);
                if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
                phrases_.push_back(wstr);
            }
        }
    }

    if (phrases_.empty()) {
        phrases_ = kDefaultPhrases;
        save();
    }
}

void PhraseManager::save() {
    std::wstring path = getDataPath();
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return;

    // 写 UTF-8 BOM
    out << "\xEF\xBB\xBF";
    out << "[";
    for (size_t i = 0; i < phrases_.size(); ++i) {
        if (i > 0) out << ",";
        out << "\"";
        // 转义引号和反斜杠
        std::wstring wstr = phrases_[i];
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], len, nullptr, nullptr);
            if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
            for (char c : utf8) {
                if (c == '"' || c == '\\') out << '\\';
                out << c;
            }
        }
        out << "\"";
    }
    out << "]";
}

std::vector<std::wstring> PhraseManager::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phrases_;
}

bool PhraseManager::add(const std::wstring& phrase) {
    if (phrase.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : phrases_) {
        if (p == phrase) return false;  // 重复
    }
    backup(L"add");
    phrases_.push_back(phrase);
    save();
    return true;
}

bool PhraseManager::update(size_t index, const std::wstring& newPhrase) {
    if (newPhrase.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= phrases_.size()) return false;
    backup(L"update");
    phrases_[index] = newPhrase;
    save();
    return true;
}

bool PhraseManager::remove(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= phrases_.size()) return false;
    backup(L"remove");
    phrases_.erase(phrases_.begin() + index);
    save();
    return true;
}

bool PhraseManager::moveUp(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index == 0 || index >= phrases_.size()) return false;
    backup(L"move");
    std::swap(phrases_[index], phrases_[index - 1]);
    save();
    return true;
}

bool PhraseManager::moveDown(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index + 1 >= phrases_.size()) return false;
    backup(L"move");
    std::swap(phrases_[index], phrases_[index + 1]);
    save();
    return true;
}

void PhraseManager::resetToDefault() {
    std::lock_guard<std::mutex> lock(mutex_);
    phrases_ = kDefaultPhrases;
    save();
}

size_t PhraseManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phrases_.size();
}
