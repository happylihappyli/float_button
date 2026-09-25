package com.example.floatbutton

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.WindowManager

/**
 * 1像素保活 Activity
 * 原理：在屏幕角落显示一个 1x1 像素的透明窗口
 * 系统认为 App 处于"用户可见"状态，不会被 ColorOS HansManager 杀掉
 * 适用：OPPO / OnePlus / Realme (ColorOS 14)
 */
class KeepAliveActivity : Activity() {

    companion object {
        private const val TAG = "KeepAliveActivity"
        const val ACTION_START = "com.example.floatbutton.START_KEEP_ALIVE"
        const val ACTION_STOP = "com.example.floatbutton.STOP_KEEP_ALIVE"

        /** 启动保活（从 Service 调用） */
        fun start(context: android.content.Context) {
            val intent = Intent(context, KeepAliveActivity::class.java).apply {
                action = ACTION_START
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
            }
            try {
                context.startActivity(intent)
            } catch (e: Exception) {
                Log.e(TAG, "启动保活失败", e)
            }
        }

        /** 停止保活 */
        fun stop(context: android.content.Context) {
            val intent = Intent(context, KeepAliveActivity::class.java).apply {
                action = ACTION_STOP
            }
            try {
                context.startActivity(intent)
            } catch (e: Exception) {
                Log.e(TAG, "停止保活失败", e)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        when (intent?.action) {
            ACTION_STOP -> {
                // 收到停止信号，结束自己
                Log.d(TAG, "收到停止信号，关闭保活")
                finish()
                return
            }
            else -> {
                // 默认行为：启动 1 像素窗口
                Log.d(TAG, "启动 1 像素保活窗口")
                showOnePxWindow()
            }
        }
    }

    /**
     * 显示 1 像素窗口
     * 关键点：
     * 1. 窗口位置在屏幕角落（0, 0）
     * 2. 窗口大小 1x1 像素
     * 3. 透明度 0.001（完全透明但系统认为可见）
     * 4. 不接受任何触摸事件
     * 5. 不在最近任务列表显示
     */
    private fun showOnePxWindow() {
        val windowParams = WindowManager.LayoutParams(
            1,  // 宽度 1 像素
            1,  // 高度 1 像素
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O)
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            else
                WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                    WindowManager.LayoutParams.FLAG_LAYOUT_INSET_DECOR or
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            android.graphics.PixelFormat.TRANSPARENT
        ).apply {
            gravity = Gravity.START or Gravity.TOP
            x = 0
            y = 0
            // 透明度几乎为 0
            alpha = 0.01f
        }

        try {
            windowManager.addView(createOnePxView(), windowParams)
        } catch (e: Exception) {
            Log.e(TAG, "添加 1 像素窗口失败", e)
        }

        // 把自己也 finish 掉（1 像素窗口是独立的 WindowManager 窗口，不需要 Activity 持续存活）
        // 但保留在最近任务里（系统级保活）
    }

    /** 创建 1 像素的 View（透明 + 不接受事件） */
    private fun createOnePxView(): android.view.View {
        return android.view.View(this).apply {
            setBackgroundColor(0x00000000) // 完全透明
            isClickable = false
            isFocusable = false
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d(TAG, "保活 Activity 已销毁")
    }
}
