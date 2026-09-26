// autostart.h - 开机自动启动辅助（操作 HKCU Run 注册表项）
#pragma once

#include <windows.h>
#include <string>

namespace AutoStart {

// 注册表路径（当前用户的"启动"项）
constexpr LPCWSTR kRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr LPCWSTR kValueName = L"FloatButton";

// 启用自动启动：将 exe 路径写入 HKCU\...\Run
// 返回 true 表示设置成功，false 表示失败
bool enable();

// 禁用自动启动：从注册表删除对应值（不存在也返回 true）
bool disable();

// 查询当前是否已启用（注册表值存在且匹配当前 exe）
// 如果注册表有值但路径不匹配（用户可能换了位置），仍返回 false 以提示重新启用
bool isEnabled();

// 获取注册表里写入的路径（仅用于诊断；为空表示未启用）
std::wstring getRegisteredPath();

}  // namespace AutoStart