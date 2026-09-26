// llm_client.cpp - LLM HTTP 客户端实现（基于 WinInet）
#include "llm_client.h"
#include "window_helper.h"
#include <wininet.h>
#include <sstream>
#include <string>

#pragma comment(lib, "wininet.lib")

namespace LlmClient {

// 简易 JSON 字符串字段转义
std::wstring jsonEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (wchar_t c : s) {
        switch (c) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        case L'\t': out += L"\\t";  break;
        case L'\b': out += L"\\b";  break;
        case L'\f': out += L"\\f";  break;
        default:
            if (c < 0x20) {
                wchar_t buf[8];
                swprintf_s(buf, L"\\u%04x", (unsigned)c);
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    return out;
}

// 从 JSON 文本中提取 "content" 字段的值（粗略提取，能应对标准 OpenAI 响应）
// 找 "content":"...\" 包围的内容（不含嵌套转义外的引号）
static std::wstring extractContent(const std::wstring& json) {
    const std::wstring key = L"\"content\"";
    size_t pos = json.find(key);
    if (pos == std::wstring::npos) return L"";
    pos = json.find(L':', pos);
    if (pos == std::wstring::npos) return L"";
    pos++;
    // 跳过空白
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t' || json[pos] == L'\n' || json[pos] == L'\r')) pos++;
    if (pos >= json.size() || json[pos] != L'"') return L"";
    pos++;  // 跳过起始 "
    std::wstring out;
    while (pos < json.size()) {
        wchar_t c = json[pos];
        if (c == L'"') break;  // 结束
        if (c == L'\\' && pos + 1 < json.size()) {
            wchar_t n = json[pos + 1];
            switch (n) {
            case L'n': out.push_back(L'\n'); break;
            case L'r': out.push_back(L'\r'); break;
            case L't': out.push_back(L'\t'); break;
            case L'"': out.push_back(L'"');  break;
            case L'\\':out.push_back(L'\\'); break;
            case L'u':
                // \uXXXX（仅处理 BMP 简单字符）
                if (pos + 5 < json.size()) {
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        wchar_t h = json[pos + 2 + k];
                        code <<= 4;
                        if (h >= L'0' && h <= L'9') code |= (h - L'0');
                        else if (h >= L'a' && h <= L'f') code |= (h - L'a' + 10);
                        else if (h >= L'A' && h <= L'F') code |= (h - L'A' + 10);
                    }
                    out.push_back((wchar_t)code);
                    pos += 4;
                }
                break;
            default: out.push_back(n); break;
            }
            pos += 2;
            continue;
        }
        out.push_back(c);
        pos++;
    }
    return out;
}

std::wstring chatCompletion(
    const std::wstring& baseUrlIn,
    const std::wstring& apiKey,
    const std::wstring& model,
    const std::wstring& systemPrompt,
    const std::wstring& userPrompt,
    std::wstring* errMsg)
{
    if (errMsg) errMsg->clear();

    if (baseUrlIn.empty()) {
        if (errMsg) *errMsg = L"Base URL 不能为空";
        return L"";
    }
    if (apiKey.empty()) {
        if (errMsg) *errMsg = L"API Key 不能为空（请到 TTS 对话框工具栏 → LLM 设置 中填写）";
        return L"";
    }

    // 解析 baseUrl，分离 host 和 path
    // 形如 https://api.openai.com/v1 或 https://api.openai.com/v1/
    std::wstring baseUrl = baseUrlIn;
    while (!baseUrl.empty() && baseUrl.back() == L'/') baseUrl.pop_back();

    std::wstring hostPart;
    std::wstring pathPart = L"/chat/completions";
    bool isHttps = false;

    if (baseUrl.substr(0, 8) == L"https://") {
        isHttps = true;
        hostPart = baseUrl.substr(8);
    } else if (baseUrl.substr(0, 7) == L"http://") {
        hostPart = baseUrl.substr(7);
    } else {
        if (errMsg) *errMsg = L"Base URL 必须以 http:// 或 https:// 开头";
        return L"";
    }
    size_t slashPos = hostPart.find(L'/');
    if (slashPos != std::wstring::npos) {
        pathPart = hostPart.substr(slashPos) + L"/chat/completions";
        hostPart = hostPart.substr(0, slashPos);
    }

    // 构建请求体 JSON（用 UTF-8 写到 char*）
    std::wstring wjson;
    wjson += L"{\"model\":\"" + jsonEscape(model) + L"\",\"stream\":false";
    if (!systemPrompt.empty()) {
        wjson += L",\"messages\":[";
        wjson += L"{\"role\":\"system\",\"content\":\"" + jsonEscape(systemPrompt) + L"\"},";
        wjson += L"{\"role\":\"user\",\"content\":\"" + jsonEscape(userPrompt) + L"\"}";
        wjson += L"]";
    } else {
        wjson += L",\"messages\":[{\"role\":\"user\",\"content\":\"" + jsonEscape(userPrompt) + L"\"}]";
    }
    wjson += L"}";

    std::string bodyUtf8 = WinHelper::utf16ToUtf8(wjson);

    // WinInet 调用
    HINTERNET hInet = InternetOpenW(L"FloatButton-LLM/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        if (errMsg) *errMsg = L"InternetOpenW 失败";
        return L"";
    }

    // 默认 30 秒超时
    DWORD timeout = 30000;
    InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOption(hInet, INTERNET_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));
    InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // 端口：https 默认 443，http 默认 80
    INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET hConn = InternetConnectW(hInet, hostPart.c_str(), port,
        nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) {
        if (errMsg) {
            wchar_t buf[64];
            swprintf_s(buf, L"InternetConnectW 失败 (host=%s)", hostPart.c_str());
            *errMsg = buf;
        }
        InternetCloseHandle(hInet);
        return L"";
    }

    DWORD flags = isHttps ? INTERNET_FLAG_SECURE : 0;
    HINTERNET hReq = HttpOpenRequestW(hConn, L"POST", pathPart.c_str(),
        nullptr, nullptr, nullptr, flags, 0);
    if (!hReq) {
        if (errMsg) *errMsg = L"HttpOpenRequestW 失败";
        InternetCloseHandle(hConn);
        InternetCloseHandle(hInet);
        return L"";
    }

    // 设置 Header：Authorization + Content-Type
    std::wstring authHeader = L"Authorization: Bearer " + apiKey;
    HttpAddRequestHeadersW(hReq, authHeader.c_str(), (DWORD)authHeader.size(),
        HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    const wchar_t* ctHeader = L"Content-Type: application/json";
    HttpAddRequestHeadersW(hReq, ctHeader, (DWORD)wcslen(ctHeader),
        HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);

    // 发送
    BOOL ok = HttpSendRequestW(hReq, nullptr, 0,
        (LPVOID)bodyUtf8.data(), (DWORD)bodyUtf8.size());
    if (!ok) {
        DWORD err = GetLastError();
        if (errMsg) {
            wchar_t buf[128];
            swprintf_s(buf, L"HttpSendRequestW 失败 (err=%u)", err);
            *errMsg = buf;
        }
        InternetCloseHandle(hReq);
        InternetCloseHandle(hConn);
        InternetCloseHandle(hInet);
        return L"";
    }

    // 读取响应
    std::string respBytes;
    char buf[4096];
    DWORD read = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf), &read) && read > 0) {
        respBytes.append(buf, read);
    }

    // 查 HTTP 状态码
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
        &statusCode, &statusSize, nullptr);

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);

    if (statusCode != 200) {
        if (errMsg) {
            wchar_t wbuf[256];
            // 截取响应前 200 字符作为错误信息
            std::string preview = respBytes.substr(0, std::min<size_t>(respBytes.size(), 200));
            std::wstring wpreview = WinHelper::utf8ToUtf16(preview);
            swprintf_s(wbuf, L"HTTP %u: %s", statusCode, wpreview.c_str());
            *errMsg = wbuf;
        }
        return L"";
    }

    std::wstring respW = WinHelper::utf8ToUtf16(respBytes);
    std::wstring content = extractContent(respW);
    if (content.empty()) {
        if (errMsg) *errMsg = L"无法解析 LLM 响应（响应中没有 content 字段）";
    }
    return content;
}

}  // namespace LlmClient