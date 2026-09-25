package com.example.floatbutton

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.NotificationManagerCompat

/**
 * 主界面：引导用户开启悬浮窗权限并启动服务
 */
class MainActivity : AppCompatActivity() {

    private lateinit var btnToggle: Button
    private lateinit var btnEditPhrases: Button
    private lateinit var tvStatus: TextView
    private lateinit var tvTip: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        btnToggle = findViewById(R.id.btn_toggle_service)
        btnEditPhrases = findViewById(R.id.btn_edit_phrases)
        tvStatus = findViewById(R.id.tv_status)
        tvTip = findViewById(R.id.tv_tip)

        updateUI()

        btnToggle.setOnClickListener {
            if (hasOverlayPermission()) {
                toggleService()
            } else {
                requestOverlayPermission()
            }
        }

        // 编辑常用语入口
        btnEditPhrases.setOnClickListener {
            startActivity(Intent(this, PhraseEditActivity::class.java))
        }

        // App 启动时，如果已授权悬浮窗且服务未运行，自动启动服务
        // 这样 adb 启动 App 也能触发悬浮窗（解决用户点不到按钮的问题）
        if (hasOverlayPermission() && !FloatButtonService.isRunning) {
            android.os.Handler(mainLooper).postDelayed({
                if (!FloatButtonService.isRunning) {
                    android.widget.Toast.makeText(
                        this,
                        "正在启动悬浮窗...",
                        android.widget.Toast.LENGTH_SHORT
                    ).show()
                    toggleService()
                }
            }, 1500)
        }
    }

    override fun onResume() {
        super.onResume()
        updateUI()
    }
    /** 更新界面状态 */
    private fun updateUI() {
        if (hasOverlayPermission()) {
            if (FloatButtonService.isRunning) {
                btnToggle.text = "关闭悬浮窗"
                btnToggle.backgroundTintList = android.content.res.ColorStateList.valueOf(0xFFF44336.toInt())
                tvStatus.text = "● 悬浮窗已开启"
                tvStatus.setTextColor(0xFF4CAF50.toInt())
                tvTip.text = "拖拽悬浮按钮移动，点击展开常用语"
            } else {
                btnToggle.text = "开启悬浮窗"
                btnToggle.backgroundTintList = android.content.res.ColorStateList.valueOf(0xFF2196F3.toInt())
                tvStatus.text = "○ 悬浮窗已关闭"
                tvStatus.setTextColor(0xFF999999.toInt())
                tvTip.text = "点击按钮启动悬浮窗"
            }
        } else {
            btnToggle.text = "申请悬浮窗权限"
            btnToggle.backgroundTintList = android.content.res.ColorStateList.valueOf(0xFFFF9800.toInt())
            tvStatus.text = "⚠ 需要悬浮窗权限"
            tvStatus.setTextColor(0xFFFF9800.toInt())
            tvTip.text = "请先授予悬浮窗权限"
        }
    }

    /** 切换服务开关 */
    private fun toggleService() {
        val intent = Intent(this, FloatButtonService::class.java)
        if (FloatButtonService.isRunning) {
            stopService(intent)
        } else {
            // 启动前引导用户开启 ColorOS 自启动和电池白名单
            // 这是 ColorOS 上避免被杀进程的关键
            if (Build.MANUFACTURER.lowercase().contains("oppo") ||
                Build.MANUFACTURER.lowercase().contains("oneplus") ||
                Build.MANUFACTURER.lowercase().contains("realme")) {
                // 提示用户开启自启动
                showAutoStartGuide()
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent)
            } else {
                startService(intent)
            }
        }
        // 延迟刷新，等服务完成启动/停止
        btnToggle.postDelayed({ updateUI() }, 300)
    }

    /**
     * 引导用户开启 ColorOS 自启动
     * ColorOS 14 上不开自启动 → 前台服务会被 HansManager 立即杀掉
     */
    private fun showAutoStartGuide() {
        val tips = StringBuilder()
        tips.append("【一加/OPPO/Realme 必看】\n\n")
        tips.append("本机厂商会杀死后台服务。开启悬浮窗后请按以下设置：\n\n")

        if (Build.MANUFACTURER.lowercase().contains("oppo") ||
            Build.MANUFACTURER.lowercase().contains("oneplus") ||
            Build.MANUFACTURER.lowercase().contains("realme")) {

            tips.append("1. 设置 → 电池 → 更多电池设置\n")
            tips.append("   → 关闭「睡眠待机优化」\n\n")

            tips.append("2. 设置 → 应用 → 浮动按钮\n")
            tips.append("   → 电池 → 允许后台运行\n")
            tips.append("   → 自启动 → 开启\n\n")

            tips.append("3. 任务管理器中锁定本 App\n")
            tips.append("   （下拉卡片找到「浮动按钮」→ 锁定）\n\n")
        }

        tips.append("不设置会导致按钮被系统自动关闭")

        android.app.AlertDialog.Builder(this)
            .setTitle("保活设置")
            .setMessage(tips.toString())
            .setPositiveButton("去设置") { _, _ ->
                // 尝试跳转到自启动设置（ColorOS 特有）
                try {
                    val intent = Intent()
                    intent.component = android.content.ComponentName(
                        "com.coloros.safecenter",
                        "com.coloros.safecenter.permission.startup.StartupAppListActivity"
                    )
                    startActivity(intent)
                } catch (e: Exception) {
                    try {
                        val intent = Intent()
                        intent.component = android.content.ComponentName(
                            "com.coloros.safecenter",
                            "com.coloros.safecenter.startupapp.StartupAppListActivity"
                        )
                        startActivity(intent)
                    } catch (e2: Exception) {
                        // 兜底：跳到应用详情
                        val intent = Intent(
                            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                            Uri.parse("package:$packageName")
                        )
                        startActivity(intent)
                    }
                }
            }
            .setNegativeButton("我知道了", null)
            .show()
    }

    /** 检查悬浮窗权限 */
    private fun hasOverlayPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Settings.canDrawOverlays(this)
        } else {
            true
        }
    }

    /** 请求悬浮窗权限 */
    private fun requestOverlayPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val intent = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:$packageName")
            )
            startActivity(intent)
        }
    }
}