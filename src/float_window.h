// float_window.h - 悬浮窗类
#pragma once

#include <windows.h>
#include <string>
#include <vector>

/**
 * 悬浮窗管理器
 *  - 创建圆形悬浮按钮（置顶、透明背景、可拖动）
 *  - 创建常用语面板（点击按钮展开/收起）
 *  - 点击常用语复制到剪贴板
 *  - 拖动到屏幕边缘自动贴边
 *  - 双击打开编辑窗口
 */
class FloatWindow {
public:
    static FloatWindow& instance();

    // 创建悬浮窗和面板
    bool create();
    // 销毁
    void destroy();
    // 显示/隐藏
    void show();
    void hide();
    void toggle();

    // 切换面板
    void togglePanel();
    void showPanel();
    void hidePanel();

    // 重新加载常用语（编辑后调用）
    void refreshPhrases();

    // 重新加载快捷程序（编辑后调用）
    void refreshShortcuts();

    // 窗口过程（静态）
    static LRESULT CALLBACK btnWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK panelWndProc(HWND, UINT, WPARAM, LPARAM);

    // HWND
    HWND buttonHwnd() const { return hBtn_; }
    HWND panelHwnd() const { return hPanel_; }

private:
    FloatWindow() = default;
    HWND hBtn_ = nullptr;
    HWND hPanel_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    bool dragging_ = false;
    POINT dragStart_{};
    POINT windowStart_{};
    bool isHiding_ = false;  // 贴边隐藏中
    int hoveredRow_ = -1;     // 当前悬停的行索引（-1表示无）
    bool hovered_ = false;    // 鼠标是否悬停在按钮上
    bool trackedMouse_ = false;// 已注册 TrackMouseEvent，避免重复调用

    // 面板布局状态（每次 showPanel 时刷新）
    int m_phraseCount = 0;     // 实际渲染的常用语条数
    int m_shortcutCount = 0;   // 实际渲染的快捷程序条数
    int m_shortcutY0 = 0;      // 快捷程序区起始 y（相对面板客户区）
    int m_shortcutHeaderY0 = 0;// "快捷程序"小标题起始 y
    int m_toolbarY0 = 0;       // 底部工具栏起始 y（相对面板客户区）
    bool toolbarHovered_ = false;  // 鼠标是否悬停在 TTS 工具栏按钮上

    // 内部辅助
    void registerClasses();
    void unregisterClasses();
    void drawPanel(HDC hdc);
    void showEditDialog();
    void startEdgeHideTimer();
    void stopEdgeHideTimer();
};
