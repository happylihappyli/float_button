// phrase_mgr.h - 常用语管理器
#pragma once

#include <string>
#include <vector>
#include <mutex>

/**
 * 常用语管理器
 *  - 使用 JSON 格式（nlohmann/json 或手写）持久化到 %APPDATA%/float_button/phrases.json
 *  - 线程安全（使用 mutex 保护）
 *  - 支持增删改查、加载、保存
 */
class PhraseManager {
public:
    static PhraseManager& instance();

    // 加载（从文件）
    void load();
    // 保存到文件
    void save();
    // 获取所有常用语
    std::vector<std::wstring> getAll() const;
    // 添加
    bool add(const std::wstring& phrase);
    // 修改
    bool update(size_t index, const std::wstring& newPhrase);
    // 删除
    bool remove(size_t index);
    // 上移
    bool moveUp(size_t index);
    // 下移
    bool moveDown(size_t index);
    // 重置为默认
    void resetToDefault();
    // 数量
    size_t count() const;
    // 备份当前 phrases.json 到 bak/ 目录
    void backup(const wchar_t* reason);
    // 打开备份目录
    void openBackupDir();
    // 获取备份目录路径
    std::wstring getBackupDir() const;

private:
    PhraseManager() = default;
    std::vector<std::wstring> phrases_;
    mutable std::mutex mutex_;
    std::wstring getDataPath() const;
};
