package com.example.floatbutton

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.hardware.camera2.CameraManager
import android.media.AudioManager
import android.os.Build
import android.util.Log
import android.widget.Toast
import kotlin.math.sqrt

/**
 * 可扩展的功能动作处理器
 * 后续新增功能只需实现此接口并注册即可
 */
interface ActionHandler {
    /** 动作名称，用于显示 */
    val name: String
    /** 执行动作 */
    fun execute(context: Context)
}

/**
 * 复制文本到剪贴板
 */
class CopyAction(private val text: String) : ActionHandler {
    override val name: String = text

    override fun execute(context: Context) {
        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
        val clip = android.content.ClipData.newPlainText("phrase", text)
        clipboard.setPrimaryClip(clip)
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
            Toast.makeText(context, "已复制: $text", Toast.LENGTH_SHORT).show()
        }
    }
}

/**
 * 手电筒开关
 */
class FlashlightAction : ActionHandler {
    override val name: String = "手电筒"
    private var isOn = false

    override fun execute(context: Context) {
        try {
            val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
            val cameraId = cameraManager.cameraIdList.firstOrNull() ?: return
            isOn = !isOn
            cameraManager.setTorchMode(cameraId, isOn)
            Toast.makeText(context, if (isOn) "手电筒已开启" else "手电筒已关闭", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(context, "手电筒不可用", Toast.LENGTH_SHORT).show()
        }
    }
}

/**
 * 一键静音/取消静音
 * 策略：
 *   1. 优先 setStreamVolume（最快最准）
 *   2. 如果调用后音量没变化（被 OEM 拒了），自动 fallback 到 adjustStreamVolume 循环减
 *   3. 用 FLAG_SHOW_UI 让系统音量条弹出来，用户能直观确认
 *   4. 取消静音时优先 adjustStreamVolume（也兼容 OEM），再回退到 setStreamVolume
 */
class MuteAction : ActionHandler {
    override val name: String = "静音"

    /** 保存静音前各音频流的音量，便于恢复 */
    private val savedVolumes = HashMap<Int, Int>()
    /** 是否处于静音状态 */
    private var isMuted = false

    /** 当前是否处于静音状态（供 UI 切换图标用） */
    fun isMuted(): Boolean = isMuted

    override fun execute(context: Context) {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        try {
            if (isMuted) {
                unMute(audioManager, context)
            } else {
                doMute(audioManager, context)
            }
        } catch (e: Exception) {
            Log.e(TAG, "静音操作整体失败", e)
            Toast.makeText(context, "静音失败: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    /** 执行静音：保存原值 + 置 0 */
    private fun doMute(audioManager: AudioManager, context: Context) {
        val streams = intArrayOf(
            AudioManager.STREAM_MUSIC,
            AudioManager.STREAM_NOTIFICATION,
            AudioManager.STREAM_ALARM,
            AudioManager.STREAM_RING,
            AudioManager.STREAM_SYSTEM
        )
        savedVolumes.clear()
        val results = StringBuilder()
        streams.forEach { streamType ->
            val before = audioManager.getStreamVolume(streamType)
            savedVolumes[streamType] = before

            // 主方案：setStreamVolume
            var ok = false
            try {
                audioManager.setStreamVolume(streamType, 0, AudioManager.FLAG_SHOW_UI)
                val after = audioManager.getStreamVolume(streamType)
                ok = (after == 0)
                Log.d(TAG, "[静音] stream=$streamType setStreamVolume before=$before after=$after ok=$ok")
            } catch (e: Exception) {
                Log.e(TAG, "setStreamVolume 异常 stream=$streamType", e)
            }

            // Fallback：adjustStreamVolume 循环减到 0
            if (!ok) {
                try {
                    var cur = audioManager.getStreamVolume(streamType)
                    var safety = 0
                    while (cur > 0 && safety < 30) {
                        audioManager.adjustStreamVolume(
                            streamType,
                            AudioManager.ADJUST_LOWER,
                            0
                        )
                        val newCur = audioManager.getStreamVolume(streamType)
                        if (newCur >= cur) {
                            Log.w(TAG, "[静音] adjustStreamVolume 不生效 stream=$streamType cur=$cur new=$newCur，跳出")
                            break
                        }
                        cur = newCur
                        safety++
                    }
                    val after = audioManager.getStreamVolume(streamType)
                    Log.d(TAG, "[静音] stream=$streamType adjustStreamVolume after=$after")
                } catch (e: Exception) {
                    Log.e(TAG, "adjustStreamVolume 异常 stream=$streamType", e)
                }
            }
            val after = audioManager.getStreamVolume(streamType)
            results.append("[$streamType:$before→$after] ")
        }
        isMuted = true
        Log.d(TAG, "[静音完成] savedVolumes=$savedVolumes")
        Toast.makeText(context, "已静音 $results", Toast.LENGTH_SHORT).show()
    }

    /** 执行取消静音：恢复 savedVolumes */
    private fun unMute(audioManager: AudioManager, context: Context) {
        if (savedVolumes.isEmpty()) {
            Log.w(TAG, "[取消静音] savedVolumes 为空，可能上次未保存；尝试直接拉满")
            // fallback：把所有流拉到最大
            val streams = intArrayOf(
                AudioManager.STREAM_MUSIC,
                AudioManager.STREAM_NOTIFICATION,
                AudioManager.STREAM_ALARM,
                AudioManager.STREAM_RING,
                AudioManager.STREAM_SYSTEM
            )
            streams.forEach { streamType ->
                try {
                    val max = audioManager.getStreamMaxVolume(streamType)
                    audioManager.setStreamVolume(streamType, max / 2, AudioManager.FLAG_SHOW_UI)
                } catch (_: Exception) {
                }
            }
            isMuted = false
            Toast.makeText(context, "已恢复音量（取最大的一半）", Toast.LENGTH_SHORT).show()
            return
        }

        val results = StringBuilder()
        savedVolumes.forEach { (streamType, targetVol) ->
            val before = audioManager.getStreamVolume(streamType)
            // 同时解除可能的 mute 状态
            try {
                @Suppress("DEPRECATION")
                audioManager.setStreamMute(streamType, false)
            } catch (_: Exception) {
            }
            // 主方案：setStreamVolume
            var ok = false
            try {
                audioManager.setStreamVolume(streamType, targetVol, AudioManager.FLAG_SHOW_UI)
                val after = audioManager.getStreamVolume(streamType)
                ok = (after == targetVol)
                Log.d(TAG, "[恢复] stream=$streamType setStreamVolume 目标=$targetVol before=$before after=$after ok=$ok")
            } catch (e: Exception) {
                Log.e(TAG, "setStreamVolume 异常 stream=$streamType", e)
            }
            // Fallback：adjustStreamVolume 循环加
            if (!ok) {
                try {
                    var cur = audioManager.getStreamVolume(streamType)
                    var safety = 0
                    while (cur < targetVol && safety < 30) {
                        audioManager.adjustStreamVolume(
                            streamType,
                            AudioManager.ADJUST_RAISE,
                            0
                        )
                        val newCur = audioManager.getStreamVolume(streamType)
                        if (newCur <= cur) {
                            Log.w(TAG, "[恢复] adjustStreamVolume 不生效 stream=$streamType cur=$cur new=$newCur，跳出")
                            break
                        }
                        cur = newCur
                        safety++
                    }
                    val after = audioManager.getStreamVolume(streamType)
                    Log.d(TAG, "[恢复] stream=$streamType adjustStreamVolume after=$after")
                } catch (e: Exception) {
                    Log.e(TAG, "adjustStreamVolume 异常 stream=$streamType", e)
                }
            }
            val after = audioManager.getStreamVolume(streamType)
            results.append("[$streamType:$before→$after] ")
        }
        savedVolumes.clear()
        isMuted = false
        Log.d(TAG, "[恢复完成]")
        Toast.makeText(context, "已取消静音 $results", Toast.LENGTH_SHORT).show()
    }

    companion object {
        private const val TAG = "MuteAction"
    }
}

/**
 * 翻转手机自动静音管理器
 *
 * 检测原理：
 *   加速度传感器返回的 (x, y, z) 是"重力 + 线性加速度"。
 *   静止时 magnitude = sqrt(x²+y²+z²) ≈ 9.8（重力）。
 *   归一化 nz = z / magnitude ∈ [-1, +1]：
 *     - 屏幕完全朝上：nz ≈ +1
 *     - 屏幕完全朝下：nz ≈ -1
 *     - 倾斜 60° 屏幕朝下：nz ≈ -0.5（依然能识别！）
 *   用归一化 nz 代替原始 z 可以稳定识别"屏幕法线"朝向，不受手机倾斜角度影响。
 */
class FlipToMuteManager(private val context: Context) : SensorEventListener {
    private var sensorManager: SensorManager? = null
    private var sensor: Sensor? = null
    private var sensorTypeName: String = "?"
    private var enabled: Boolean = false
    private var hasReceivedData: Boolean = false

    private val savedVolumes = HashMap<Int, Int>()
    private var hasMuted = false

    private var faceDownCount = 0
    private var faceUpCount = 0
    private val confirmThreshold = 2  // 连续 2 次确认即可

    // 触发阈值（归一化 nz）：
    //   屏幕朝上：nz > 0.5（约 60° 以内都算）
    //   屏幕朝下：nz < -0.5
    private val faceDownNz = -0.5f
    private val faceUpNz = 0.5f

    // magnitude 合理性检查：静止时应约等于 9.8，过滤明显异常的数据
    private val magnitudeLow = 5.0f
    private val magnitudeHigh = 15.0f

    /** 启动监听 */
    fun start() {
        if (enabled) return
        try {
            sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager
            if (sensorManager == null) {
                Toast.makeText(context, "设备不支持传感器服务", Toast.LENGTH_SHORT).show()
                return
            }
            // 优先用 TYPE_ACCELEROMETER（最稳，所有手机都有真硬件）
            sensor = sensorManager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
            sensorTypeName = "ACCELEROMETER"
            if (sensor == null) {
                sensor = sensorManager?.getDefaultSensor(Sensor.TYPE_GRAVITY)
                sensorTypeName = "GRAVITY"
            }
            if (sensor == null) {
                Toast.makeText(context, "设备没有加速度/重力传感器", Toast.LENGTH_SHORT).show()
                Log.e(TAG, "启动失败：找不到可用传感器")
                return
            }

            // 注册监听器，检查返回值
            val registered = sensorManager?.registerListener(
                this, sensor, SensorManager.SENSOR_DELAY_GAME
            ) ?: false
            if (!registered) {
                Toast.makeText(context, "传感器注册失败", Toast.LENGTH_SHORT).show()
                Log.e(TAG, "registerListener 返回 false，传感器=$sensorTypeName")
                enabled = false
                return
            }

            enabled = true
            hasReceivedData = false
            faceDownCount = 0
            faceUpCount = 0
            Log.d(TAG, "翻转静音已开启，传感器=$sensorTypeName 名称=${sensor?.name}")
            Toast.makeText(context, "翻转静音：把手机翻过去（屏幕朝下）自动静音", Toast.LENGTH_LONG).show()
        } catch (e: Exception) {
            Log.e(TAG, "启动失败", e)
            Toast.makeText(context, "翻转静音启动失败: ${e.message}", Toast.LENGTH_SHORT).show()
            enabled = false
        }
    }

    /** 停止监听并恢复音量 */
    fun stop() {
        if (!enabled) return
        try {
            sensorManager?.unregisterListener(this)
        } catch (_: Exception) {
        }
        enabled = false
        hasReceivedData = false
        faceDownCount = 0
        faceUpCount = 0
        if (hasMuted) {
            restoreVolume()
            hasMuted = false
        }
        Log.d(TAG, "翻转静音已关闭")
        Toast.makeText(context, "翻转静音已关闭", Toast.LENGTH_SHORT).show()
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (event == null || event.values.size < 3) return
        val x = event.values[0]
        val y = event.values[1]
        val z = event.values[2]
        val mag = sqrt(x * x + y * y + z * z)

        // 首次收到数据：弹个 Toast 让用户确认传感器在工作
        if (!hasReceivedData) {
            hasReceivedData = true
            Log.d(TAG, "首次传感器数据：x=$x y=$y z=$z mag=$mag 传感器=$sensorTypeName")
            Toast.makeText(context, "传感器工作中（$sensorTypeName）", Toast.LENGTH_SHORT).show()
        }

        if (mag < magnitudeLow || mag > magnitudeHigh) {
            // magnitude 异常（设备剧烈运动或传感器异常），跳过本次
            return
        }
        // 归一化：用 nz 判断朝向
        val nz = z / mag

        if (!hasMuted) {
            // 待触发：等屏幕朝下
            if (nz < faceDownNz) {
                faceDownCount++
                faceUpCount = 0
                if (faceDownCount % 2 == 0) {
                    // 偶数次时打日志（避免日志刷屏）
                    Log.d(TAG, "[候选-朝下] count=$faceDownCount nz=$nz mag=$mag")
                }
                if (faceDownCount >= confirmThreshold) {
                    Log.d(TAG, "[触发静音] nz=$nz mag=$mag")
                    muteAndSave()
                    hasMuted = true
                    faceDownCount = 0
                    Toast.makeText(context, "已自动静音", Toast.LENGTH_SHORT).show()
                }
            } else {
                faceDownCount = 0
            }
        } else {
            // 已静音：等屏幕朝上恢复
            if (nz > faceUpNz) {
                faceUpCount++
                faceDownCount = 0
                if (faceUpCount % 2 == 0) {
                    Log.d(TAG, "[候选-朝上] count=$faceUpCount nz=$nz mag=$mag")
                }
                if (faceUpCount >= confirmThreshold) {
                    Log.d(TAG, "[触发恢复] nz=$nz mag=$mag")
                    restoreVolume()
                    hasMuted = false
                    faceUpCount = 0
                    Toast.makeText(context, "已自动恢复音量", Toast.LENGTH_SHORT).show()
                }
            } else {
                faceUpCount = 0
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    /** 静音并保存原音量 */
    private fun muteAndSave() {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        val streams = intArrayOf(
            AudioManager.STREAM_MUSIC,
            AudioManager.STREAM_NOTIFICATION,
            AudioManager.STREAM_ALARM,
            AudioManager.STREAM_RING,
            AudioManager.STREAM_SYSTEM
        )
        savedVolumes.clear()
        streams.forEach { streamType ->
            val cur = audioManager.getStreamVolume(streamType)
            savedVolumes[streamType] = cur
            try {
                audioManager.setStreamVolume(streamType, 0, AudioManager.FLAG_SHOW_UI)
            } catch (e: Exception) {
                Log.e(TAG, "setStreamVolume(0) 失败 stream=$streamType", e)
            }
            val after = audioManager.getStreamVolume(streamType)
            if (after != 0) {
                // fallback：adjustStreamVolume 循环减
                try {
                    var safety = 0
                    while (audioManager.getStreamVolume(streamType) > 0 && safety < 30) {
                        audioManager.adjustStreamVolume(streamType, AudioManager.ADJUST_LOWER, 0)
                        safety++
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "adjustStreamVolume 失败", e)
                }
            }
        }
        Log.d(TAG, "已静音，保存原音量=$savedVolumes")
    }

    /** 恢复之前保存的音量 */
    private fun restoreVolume() {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        Log.d(TAG, "恢复音量 savedVolumes=$savedVolumes")
        savedVolumes.forEach { (streamType, vol) ->
            try {
                @Suppress("DEPRECATION")
                audioManager.setStreamMute(streamType, false)
            } catch (_: Exception) {
            }
            try {
                audioManager.setStreamVolume(streamType, vol, AudioManager.FLAG_SHOW_UI)
            } catch (e: Exception) {
                Log.e(TAG, "setStreamVolume 失败 stream=$streamType vol=$vol", e)
            }
            val cur = audioManager.getStreamVolume(streamType)
            if (cur < vol) {
                // fallback：adjustStreamVolume 循环加
                try {
                    var safety = 0
                    while (audioManager.getStreamVolume(streamType) < vol && safety < 30) {
                        audioManager.adjustStreamVolume(streamType, AudioManager.ADJUST_RAISE, 0)
                        safety++
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "adjustStreamVolume 恢复失败", e)
                }
            }
        }
        savedVolumes.clear()
    }

    /** 是否正在监听 */
    fun isEnabled(): Boolean = enabled

    /** 是否当前处于翻转静音状态 */
    fun isMuted(): Boolean = hasMuted

    companion object {
        private const val TAG = "FlipToMute"
    }
}