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

    // 简单 JSON 解析（仅支持 key:number 格式）
    auto findNumber = [&](const std::string& key, int defaultVal) -> int {
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
    };

    hotkeyModifiers_ = (UINT)findNumber("hkMods", (int)(MOD_WIN | MOD_SHIFT | MOD_NOREPEAT));
    hotkeyVk_ = (UINT)findNumber("hkVk", (int)'F');
    floatButtonAlpha_ = findNumber("alpha", 220);
}

void AppConfig::save() {
    std::wstring path = getConfigPath();
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return;
    out << "\xEF\xBB\xBF";  // UTF-8 BOM
    out << "{\n";
    out << "  \"hkMods\": " << (int)hotkeyModifiers_ << ",\n";
    out << "  \"hkVk\": " << (int)hotkeyVk_ << ",\n";
    out << "  \"alpha\": " << floatButtonAlpha_ << "\n";
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
