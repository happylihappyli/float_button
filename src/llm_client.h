// llm_client.h - 简易 LLM HTTP 客户端（OpenAI 兼容协议）
#pragma once

#include <windows.h>
#include <string>

namespace LlmClient {

// 一次性调用 LLM（OpenAI 兼容 chat/completions 接口）
//   baseUrl: 如 https://api.openai.com/v1
//   apiKey : Bearer Token
//   model  : 模型名
//   prompt : 用户提示词
//   systemPrompt: 系统提示词（可空）
//   errMsg : 失败时填充错误信息
// 返回值：成功返回 LLM 回复的文本（UTF-8 已转 UTF-16）；失败返回空串
//   阻塞调用，可能耗时数秒（在调用方线程里调用）
std::wstring chatCompletion(
    const std::wstring& baseUrl,
    const std::wstring& apiKey,
    const std::wstring& model,
    const std::wstring& systemPrompt,
    const std::wstring& userPrompt,
    std::wstring* errMsg
);

// JSON 字符串转义（" -> \"，\ -> \\，换行 -> \n）
std::wstring jsonEscape(const std::wstring& s);

}  // namespace LlmClient