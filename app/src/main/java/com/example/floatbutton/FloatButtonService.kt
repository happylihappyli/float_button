package com.example.floatbutton

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.graphics.PixelFormat
import android.os.Build
import android.os.IBinder
import android.util.Log
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.app.NotificationCompat

/**
 * 悬浮窗前台服务
 * 管理悬浮按钮的显示、拖拽、点击展开面板等功能
 */
class FloatButtonService : Service() {

    private lateinit var windowManager: WindowManager
    private var floatView: View? = null
    private var panelView: View? = null
    private var isPanelShowing = false

    // 静音动作与翻转静音管理器
    private val muteAction = MuteAction()
    private val flipToMuteManager by lazy { FlipToMuteManager(this) }

    // 拖拽状态
    private var isDragging = false
    private var hasMoved = false
    private var initialX = 0
    private var initialY = 0
    private var touchStartX = 0f
    private var touchStartY = 0f
    private val clickThreshold = 10

    // 悬浮按钮布局参数
    private var floatParams: WindowManager.LayoutParams? = null
    // 面板布局参数
    private var panelParams: WindowManager.LayoutParams? = null

    companion object {
        private const val TAG = "FloatButtonService"
        const val CHANNEL_ID = "float_button_channel"
        const val NOTIFICATION_ID = 1001

        /** 静态运行状态，供 MainActivity 读取（避免 getRunningServices 兼容性问题） */
        @Volatile var isRunning: Boolean = false
    }

    override fun onCreate() {
        super.onCreate()
        try {
            windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
            createNotificationChannel()
            // Android 14 严格要求：startForegroundService 后必须 5 秒内调用 startForeground
            // type 必须与 manifest 中 android:foregroundServiceType 一致
            val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
            } else 0
            startForeground(NOTIFICATION_ID, buildNotification(), type)
            Log.d(TAG, "onCreate: 服务已创建并提升为前台，type=$type")
        } catch (e: Exception) {
            Log.e(TAG, "onCreate: 异常", e)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        isRunning = true
        showFloatButton()

        // 默认显示功能面板（不藏起来）
        showPanel()

        // 启动 1 像素保活 Activity（ColorOS 14 杀进程的根治方案）
        try {
            KeepAliveActivity.start(this)
            Log.d(TAG, "1像素保活 Activity 已启动")
        } catch (e: Exception) {
            Log.e(TAG, "启动 1 像素保活失败", e)
        }

        // 提示用户加入电池白名单（ColorOS 杀进程严重，必须手动加白名单）
        Toast.makeText(
            this,
            "如遇按钮消失，请到 设置→电池→更多电池设置→不优化后台耗电 中允许本应用",
            Toast.LENGTH_LONG
        ).show()

        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        super.onDestroy()
        isRunning = false
        // 关闭 1 像素保活
        try {
            KeepAliveActivity.stop(this)
        } catch (e: Exception) {
            Log.e(TAG, "停止 1 像素保活失败", e)
        }
        // 关闭翻转静音监听
        try {
            flipToMuteManager.stop()
        } catch (e: Exception) {
            Log.e(TAG, "停止翻转静音失败", e)
        }
        hidePanel()
        removeFloatButton()
    }

    // ==================== 通知相关 ====================

    /** 创建通知渠道（Android 8+ 前台服务必须） */
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "浮动按钮服务",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "浮动按钮运行中"
            }
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }
    }

    /** 构建前台服务通知 */
    private fun buildNotification(): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("浮动按钮")
            .setContentText("正在运行中，点击管理")
            .setSmallIcon(android.R.drawable.ic_menu_add)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    // ==================== 悬浮按钮 ====================

    /** 显示悬浮按钮 */
    private fun showFloatButton() {
        if (floatView != null) return
        try {
            floatView = LayoutInflater.from(this).inflate(R.layout.float_button, null)
            floatParams = WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                else
                    WindowManager.LayoutParams.TYPE_PHONE,
                // 关键 flags 组合：
                // 1. FLAG_NOT_FOCUSABLE：让 View 不获取焦点，触摸事件不抢焦点
                //    → 其他 App 的输入框才能正常弹出输入法
                // 2. FLAG_NOT_TOUCH_MODAL：触摸事件在按钮外透传给下层窗口
                //    → 其他 App 的按钮才能被点击
                // 3. FLAG_WATCH_OUTSIDE_TOUCH：监听按钮外的触摸（用于关闭面板）
                // 4. 不用 FLAG_NOT_TOUCHABLE，否则 OnTouchListener 完全收不到事件
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                        WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                        WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH,
                PixelFormat.TRANSLUCENT
            ).apply {
                // 默认放屏幕右下角（不挡屏幕中央内容）
                gravity = Gravity.TOP or Gravity.START
                x = 50
                y = 600
            }

            // 设置拖拽和点击
            setupFloatButtonTouch()

            windowManager.addView(floatView, floatParams)
            Log.d(TAG, "showFloatButton: 悬浮按钮已添加")
        } catch (e: Exception) {
            Log.e(TAG, "showFloatButton: 显示悬浮按钮失败", e)
        }
    }

    /**
     * 设置悬浮按钮的触摸事件
     * 实现：
     *   - 单击（按下后快速抬手）→ 切换面板显隐
     *   - 长按（按下后移动超过阈值）→ 进入拖拽模式
     * 关键：只用 OnTouchListener 一个监听器处理所有情况，
     *       避免 OnClick/OnLongClick 与 OnTouch 之间的时序冲突
     */
    private fun setupFloatButtonTouch() {
        val btnFloat = floatView?.findViewById<ImageButton>(R.id.btn_float) ?: return

        btnFloat.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    // 按下时记录起始位置和触摸点
                    initialX = floatParams!!.x
                    initialY = floatParams!!.y
                    touchStartX = event.rawX
                    touchStartY = event.rawY
                    hasMoved = false
                    isDragging = false
                    // 不消费，让系统继续传递（便于后续长按检测等）
                    false
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - touchStartX
                    val dy = event.rawY - touchStartY
                    if (Math.abs(dx) > clickThreshold || Math.abs(dy) > clickThreshold) {
                        hasMoved = true
                        if (!isDragging) {
                            isDragging = true
                            Log.d(TAG, "进入拖拽模式")
                        }
                    }
                    if (isDragging) {
                        // 实时更新悬浮按钮位置
                        try {
                            floatParams!!.x = initialX + dx.toInt()
                            floatParams!!.y = initialY + dy.toInt()
                            windowManager.updateViewLayout(floatView, floatParams)
                        } catch (e: Exception) {
                            Log.e(TAG, "拖拽更新位置失败", e)
                        }
                    }
                    // 一旦开始移动，OnTouchListener 就消费事件
                    isDragging
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    val wasMoved = hasMoved
                    isDragging = false
                    hasMoved = false
                    if (wasMoved) {
                        // 拖动完成：吞掉事件（不触发 click）
                        true
                    } else {
                        // 未移动 → 单击：返回 false 让 OnClickListener 触发
                        false
                    }
                }
                else -> false
            }
        }

        // 单击事件：切换面板显隐
        btnFloat.setOnClickListener {
            Log.d(TAG, "onClick: 切换面板")
            try {
                togglePanel()
            } catch (e: Exception) {
                Log.e(TAG, "togglePanel 异常", e)
            }
        }
    }

    /** 移除悬浮按钮 */
    private fun removeFloatButton() {
        floatView?.let {
            windowManager.removeView(it)
        }
        floatView = null
    }

    // ==================== 功能面板 ====================

    /** 切换面板显示/隐藏 */
    private fun togglePanel() {
        if (isPanelShowing) {
            hidePanel()
        } else {
            showPanel()
        }
    }

    /** 显示功能面板 */
    private fun showPanel() {
        if (panelView != null) return
        try {
            panelView = LayoutInflater.from(this).inflate(R.layout.panel_function, null)
            panelParams = WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                else
                    WindowManager.LayoutParams.TYPE_PHONE,
                // 不加 FLAG_NOT_FOCUSABLE，让面板可接收点击
                // 加 FLAG_NOT_TOUCH_MODAL：面板外的触摸不拦截（透传给悬浮按钮/系统）
                // 不加 FLAG_WATCH_OUTSIDE_TOUCH 以避免外部触摸触发
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                        WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH,
                PixelFormat.TRANSLUCENT
            ).apply {
                gravity = Gravity.TOP or Gravity.START
                // 面板显示在按钮上方（确保不超出屏幕）
                val screenW = resources.displayMetrics.widthPixels
                val btnX = floatParams?.x ?: 100
                val panelW = 300 // 估算的面板宽度
                x = if (btnX + panelW + 60 > screenW) btnX - panelW - 20 else btnX + 60
                y = (floatParams?.y ?: 400) - 100
                if (x < 0) x = 20
            }

            buildPhraseList()
            setupFeatureButtons()
            setupCloseButton()

            // 点击面板外部时关闭面板
            panelView?.setOnTouchListener { _, event ->
                if (event.action == MotionEvent.ACTION_OUTSIDE) {
                    hidePanel()
                    true
                } else {
                    false
                }
            }

            windowManager.addView(panelView, panelParams)
            isPanelShowing = true
            Log.d(TAG, "showPanel: 面板已显示")
        } catch (e: Exception) {
            Log.e(TAG, "showPanel: 显示面板失败", e)
            panelView = null
            isPanelShowing = false
        }
    }

    /** 隐藏面板 */
    private fun hidePanel() {
        panelView?.let {
            windowManager.removeView(it)
        }
        panelView = null
        isPanelShowing = false
    }

    /** 构建常用语列表 */
    private fun buildPhraseList() {
        val container = panelView?.findViewById<LinearLayout>(R.id.phrase_container) ?: return
        container.removeAllViews()

        PhraseManager.getPhrases(this).forEachIndexed { index, phrase ->
            val tv = TextView(this).apply {
                text = phrase
                textSize = 13f
                setTextColor(0xFF333333.toInt())
                setPadding(16, 12, 16, 12)
                // 使用自定义 selector 而非系统 list_selector_background，避免系统主题属性缺失导致崩溃
                setBackgroundResource(R.drawable.bg_phrase_item)
                isClickable = true
                isFocusable = true
                // 单击：复制文本
                setOnClickListener {
                    CopyAction(phrase).execute(this@FloatButtonService)
                    hidePanel()
                }
                // 长按：弹出菜单（编辑/删除）
                setOnLongClickListener {
                    showPhraseMenu(index, phrase)
                    true
                }
            }
            container.addView(tv)
        }
    }

    /** 显示常用语操作菜单 */
    private fun showPhraseMenu(index: Int, phrase: String) {
        AlertDialog.Builder(this)
            .setTitle(phrase)
            .setItems(arrayOf("编辑", "删除")) { _, which ->
                when (which) {
                    0 -> {
                        // 编辑：跳转到编辑界面
                        hidePanel()
                        val intent = Intent(this, PhraseEditActivity::class.java).apply {
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        startActivity(intent)
                    }
                    1 -> {
                        // 删除：直接删除
                        PhraseManager.deletePhrase(this, index)
                        buildPhraseList()
                        Toast.makeText(this, "已删除", Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .show()
    }

    /** 设置面板底部功能按钮 */
    private fun setupFeatureButtons() {
        // 手电筒按钮
        val flashlightAction = FlashlightAction()
        val btnFlash = panelView?.findViewById<TextView>(R.id.btn_flashlight)
        btnFlash?.setOnClickListener {
            flashlightAction.execute(this)
        }
        btnFlash?.setOnLongClickListener {
            showFeatureDialog(
                title = getString(R.string.flashlight_long_title),
                message = getString(R.string.flashlight_long_desc),
                onConfirm = null
            )
            true
        }

        // 快捷静音按钮（一键静音/取消静音，再按一次恢复原音量）
        // 图标随状态切换：未静音 🔇，已静音 🔊
        val btnMute = panelView?.findViewById<TextView>(R.id.btn_mute)
        updateMuteIcon(btnMute)
        btnMute?.setOnClickListener {
            muteAction.execute(this)
            updateMuteIcon(btnMute)
        }
        btnMute?.setOnLongClickListener {
            showFeatureDialog(
                title = getString(R.string.mute_long_title),
                message = getString(R.string.mute_long_desc),
                onConfirm = null
            )
            true
        }

        // 翻转静音开关（点一次开启，再点一次关闭）
        // 图标随状态切换：未开启 🔄，已开启 ⏹
        val btnFlipMute = panelView?.findViewById<TextView>(R.id.btn_flip_mute)
        updateFlipMuteIcon(btnFlipMute)
        btnFlipMute?.setOnClickListener { view ->
            if (flipToMuteManager.isEnabled()) {
                flipToMuteManager.stop()
            } else {
                flipToMuteManager.start()
            }
            updateFlipMuteIcon(view as? TextView)
        }
        // 长按：弹出说明弹窗，并提供"直接开启/关闭"按钮
        btnFlipMute?.setOnLongClickListener {
            val title = getString(R.string.flip_mute_long_title)
            val message = getString(R.string.flip_mute_long_desc)
            if (flipToMuteManager.isEnabled()) {
                showFeatureDialog(
                    title = title,
                    message = message,
                    confirmText = getString(R.string.dialog_btn_close),
                    onConfirm = {
                        flipToMuteManager.stop()
                        updateFlipMuteIcon(btnFlipMute)
                    }
                )
            } else {
                showFeatureDialog(
                    title = title,
                    message = message,
                    confirmText = getString(R.string.dialog_btn_open),
                    onConfirm = {
                        flipToMuteManager.start()
                        updateFlipMuteIcon(btnFlipMute)
                    }
                )
            }
            true
        }

        // 更多功能按钮
        val btnMore = panelView?.findViewById<TextView>(R.id.btn_more)
        btnMore?.setOnClickListener {
            Toast.makeText(this, "更多功能开发中...", Toast.LENGTH_SHORT).show()
        }
        btnMore?.setOnLongClickListener {
            showFeatureDialog(
                title = getString(R.string.more_long_title),
                message = getString(R.string.more_long_desc),
                onConfirm = null
            )
            true
        }
    }

    /**
     * 弹出功能说明对话框
     * @param onConfirm 设置后会在弹窗上加一个"开启/关闭"按钮，点击会执行并关闭弹窗；传 null 则只显示"知道了"
     */
    private fun showFeatureDialog(
        title: String,
        message: String,
        confirmText: String? = null,
        onConfirm: (() -> Unit)? = null
    ) {
        try {
            val builder = android.app.AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(message)
                .setPositiveButton(getString(R.string.dialog_btn_got_it), null)
            if (confirmText != null && onConfirm != null) {
                builder.setNegativeButton(confirmText) { _, _ -> onConfirm() }
            }
            val dialog = builder.create()
            // 用系统悬浮窗类型，保证在悬浮面板里也能弹出来
            try {
                dialog.window?.setType(
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                    } else {
                        @Suppress("DEPRECATION")
                        WindowManager.LayoutParams.TYPE_PHONE
                    }
                )
            } catch (_: Exception) {
            }
            dialog.show()
        } catch (e: Exception) {
            Log.e(TAG, "弹窗失败", e)
            Toast.makeText(this, title + "\n" + message, Toast.LENGTH_LONG).show()
        }
    }

    /** 根据 MuteAction 状态更新按钮图标 */
    private fun updateMuteIcon(btn: TextView?) {
        btn ?: return
        btn.text = if (muteAction.isMuted()) "🔊\n静音" else "🔇\n静音"
    }

    /** 根据 FlipToMuteManager 状态更新按钮图标 */
    private fun updateFlipMuteIcon(btn: TextView?) {
        btn ?: return
        btn.text = if (flipToMuteManager.isEnabled()) "⏹\n翻转" else "🔄\n翻转"
    }

    /** 设置关闭按钮 */
    private fun setupCloseButton() {
        panelView?.findViewById<TextView>(R.id.btn_close)?.setOnClickListener {
            hidePanel()
        }

        // 编辑按钮：跳转到编辑界面
        panelView?.findViewById<TextView>(R.id.btn_edit)?.setOnClickListener {
            hidePanel()
            val intent = Intent(this, PhraseEditActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            startActivity(intent)
        }
    }
}