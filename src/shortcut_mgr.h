// shortcut_mgr.h - 快捷程序管理器
// 类似 phrase_mgr.h，管理用户配置的"快捷程序"列表
// 数据文件: %APPDATA%\float_button\shortcuts.json
#pragma once

#include <string>
#include <vector>
#include <mutex>

/**
 * 一个用户配置的快捷方式
 *  - icon: emoji 字符（如 "📝"），用于面板按钮显示
 *  - name: 显示名（如 "记事本"）
 *  - path: 程序路径（支持环境变量如 %ProgramFiles%\...、支持 ms-settings: 等 URI）
 *  - args: 启动参数（空格分隔）
 */
struct Shortcut {
    std::wstring icon;
    std::wstring name;
    std::wstring path;
    std::wstring args;
};

/**
 * 快捷程序管理器（单例）
 *  - 线程安全（用 mutex 保护）
 *  - 增删改查 + 排序
 *  - 手动写入 JSON 数组格式（和 phrase_mgr 风格一致）
 */
class ShortcutManager {
public:
    static ShortcutManager& instance();

    // 从磁盘加载
    void load();
    // 保存到磁盘
    void save();
    // 获取所有快捷方式
    std::vector<Shortcut> getAll() const;
    // 数量
    size_t count() const;
    // 添加（name 或 path 为空则拒绝）
    bool add(const Shortcut& sc);
    // 修改
    bool update(size_t index, const Shortcut& sc);
    // 删除
    bool remove(size_t index);
    // 上移
    bool moveUp(size_t index);
    // 下移
    bool moveDown(size_t index);
    // 整体替换（编辑窗口保存时用）
    void setAll(const std::vector<Shortcut>& list);
    // 启动一个快捷方式（异步，不阻塞）
    // 返回 true = 已启动（即使启动失败返回 false 也可以用 errorMsg 看具体原因）
    bool launch(const Shortcut& sc, std::wstring* errorMsg = nullptr);

private:
    ShortcutManager() = default;
    std::wstring getDataPath() const;

    mutable std::mutex mutex_;
    std::vector<Shortcut> shortcuts_;
};
