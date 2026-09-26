// config.cpp
#include "config.h"
#include "window_helper.h"
#include <shlobj.h>
#include <sstream>

AppConfig& AppConfig::instance() {
    static AppConfig c;
    return c;
}

std::wstring AppConfig::getConfigPath() const {
    wchar_t appdata[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    fs::path dir = fs::path(appdata) / L"float_button";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return (dir / L"config.json").wstring();
}

// 解析整数（支持负数）
static int parseInt(const std::string& content, const std::string& key, int defaultVal) {
    size_t pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) return defaultVal;
    pos = content.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    pos++;
    while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
    int val = 0;
    bool neg = false;
    if (pos < content.size() && content[pos] == '-') { neg = true; pos++; }
    while (pos < content.size() && content[pos] >= '0' && content[pos] <= '9') {
        val = val * 10 + (content[pos] - '0');
        pos++;
    }
    return neg ? -val : val;
}

// 解析布尔（0/false -> false; 1/true -> true）
static bool parseBool(const std::string& content, const std::string& key, bool defaultVal) {
    int v = parseInt(content, key, defaultVal ? 1 : 0);
    return v != 0;
}

// 解析字符串（双引号包裹，UTF-8 内容）
static std::string parseStringRaw(const std::string& content, const std::string& key, const std::string& defaultVal) {
    size_t pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) return defaultVal;
    pos = content.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    pos++;
    while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
    if (pos >= content.size() || content[pos] != '"') return defaultVal;
    pos++;  // 跳过起始 "
    std::string out;
    while (pos < content.size() && content[pos] != '"') {
        // 不解析 \" 等转义（配置项里不需要）
        out.push_back(content[pos]);
        pos++;
    }
    return out;
}

static std::wstring parseString(const std::string& content, const std::string& key, const std::wstring& defaultVal) {
    std::string v = parseStringRaw(content, key, "");
    if (v.empty() && !defaultVal.empty()) return defaultVal;
    if (v.empty()) return L"";
    return WinHelper::utf8ToUtf16(v);
}

void AppConfig::load() {
    std::wstring path = getConfigPath();
    std::ifstream in(path);
    if (!in.is_open()) {
        save();
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    hotkeyModifiers_  = (UINT)parseInt(content, "hkMods", (int)(MOD_WIN | MOD_SHIFT | MOD_NOREPEAT));
    hotkeyVk_         = (UINT)parseInt(content, "hkVk", (int)'F');
    floatButtonAlpha_ = parseInt(content, "alpha", 220);
    autoStart_        = parseBool(content, "autoStart", false);
    autoSpeakClipboard_ = parseBool(content, "autoSpeakClipboard", false);
    ttsRate_          = parseInt(content, "ttsRate", 0);
    ttsVolume_        = parseInt(content, "ttsVolume", 100);

    textFontName_     = parseString(content, "textFontName", L"Microsoft YaHei UI");
    textFontSize_     = parseInt(content, "textFontSize", 14);
    llmBaseUrl_       = parseString(content, "llmBaseUrl", L"https://api.openai.com/v1");
    llmApiKey_        = parseString(content, "llmApiKey", L"");
    llmModel_         = parseString(content, "llmModel", L"gpt-4o-mini");
    translateTargetLang_ = parseString(content, "translateTargetLang", L"中文");
}

void AppConfig::save() {
    std::wstring path = getConfigPath();
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return;
    out << "\xEF\xBB\xBF";  // UTF-8 BOM
    out << "{\n";
    out << "  \"hkMods\": " << (int)hotkeyModifiers_ << ",\n";
    out << "  \"hkVk\": " << (int)hotkeyVk_ << ",\n";
    out << "  \"alpha\": " << floatButtonAlpha_ << ",\n";
    out << "  \"autoStart\": " << (autoStart_ ? 1 : 0) << ",\n";
    out << "  \"autoSpeakClipboard\": " << (autoSpeakClipboard_ ? 1 : 0) << ",\n";
    out << "  \"ttsRate\": " << ttsRate_ << ",\n";
    out << "  \"ttsVolume\": " << ttsVolume_ << ",\n";
    out << "  \"textFontName\": \"" << WinHelper::utf16ToUtf8(textFontName_) << "\",\n";
    out << "  \"textFontSize\": " << textFontSize_ << ",\n";
    out << "  \"llmBaseUrl\": \"" << WinHelper::utf16ToUtf8(llmBaseUrl_) << "\",\n";
    out << "  \"llmApiKey\": \"" << WinHelper::utf16ToUtf8(llmApiKey_) << "\",\n";
    out << "  \"llmModel\": \"" << WinHelper::utf16ToUtf8(llmModel_) << "\",\n";
    out << "  \"translateTargetLang\": \"" << WinHelper::utf16ToUtf8(translateTargetLang_) << "\"\n";
    out << "}\n";
}

std::wstring AppConfig::hotkeyDescription() const {
    std::wstring desc;
    UINT mods = hotkeyModifiers_ & 0x000F;  // 去掉 NOREPEAT 等
    if (mods & MOD_CONTROL) desc += L"Ctrl+";
    if (mods & MOD_ALT) desc += L"Alt+";
    if (mods & MOD_SHIFT) desc += L"Shift+";
    if (mods & MOD_WIN) desc += L"Win+";

    // 虚拟键码转字符
    wchar_t keyName[64] = {0};
    if ((hotkeyVk_ >= '0' && hotkeyVk_ <= '9') ||
        (hotkeyVk_ >= 'A' && hotkeyVk_ <= 'Z')) {
        desc += (wchar_t)hotkeyVk_;
    } else {
        UINT scan = MapVirtualKeyW(hotkeyVk_, MAPVK_VK_TO_CHAR);
        if (scan) {
            desc += (wchar_t)scan;
        } else {
            desc += L"[VK " + std::to_wstring(hotkeyVk_) + L"]";
        }
    }
    return desc;
}