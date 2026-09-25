package com.example.floatbutton

import android.content.Context

/**
 * 常用语管理器
 * 提供常用语的读取、添加、修改、删除、持久化（SharedPreferences）功能
 * 替代之前硬编码的 defaultPhrases 列表
 */
object PhraseManager {

    private const val PREF_NAME = "phrase_manager"
    private const val KEY_PHRASES = "phrases"
    private const val SEPARATOR = "||PHRASE_SEP||"  // 短语分隔符

    /** 默认常用语（首次安装时使用） */
    private val defaultPhrases = listOf(
        "好的，收到！",
        "没问题，我来处理。",
        "稍等一下，马上好。",
        "这个我需要确认一下。",
        "已完成，请查收。",
        "在吗？",
        "谢谢！",
        "辛苦了！",
        "请问这个怎么处理？",
        "发我一份，谢谢。",
        "正在处理中...",
        "明天再跟进一下。",
        "收到，我看看。",
        "好的，没问题～",
        "这个需要讨论一下。",
    )

    /**
     * 获取所有常用语
     * 第一次会写入默认列表
     */
    fun getPhrases(context: Context): MutableList<String> {
        val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
        val saved = prefs.getString(KEY_PHRASES, null)
        if (saved.isNullOrEmpty()) {
            // 首次使用，写入默认列表
            savePhrases(context, defaultPhrases)
            return defaultPhrases.toMutableList()
        }
        return saved.split(SEPARATOR).filter { it.isNotEmpty() }.toMutableList()
    }

    /**
     * 保存常用语列表
     */
    fun savePhrases(context: Context, phrases: List<String>) {
        val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
        // 过滤空字符串后用分隔符合并
        val joined = phrases.filter { it.isNotEmpty() }.joinToString(SEPARATOR)
        prefs.edit().putString(KEY_PHRASES, joined).apply()
    }

    /**
     * 添加一条常用语
     */
    fun addPhrase(context: Context, phrase: String): Boolean {
        if (phrase.isBlank()) return false
        val list = getPhrases(context)
        if (list.contains(phrase)) return false  // 重复
        list.add(phrase)
        savePhrases(context, list)
        return true
    }

    /**
     * 修改指定索引的常用语
     */
    fun updatePhrase(context: Context, index: Int, newPhrase: String): Boolean {
        if (newPhrase.isBlank()) return false
        val list = getPhrases(context)
        if (index < 0 || index >= list.size) return false
        list[index] = newPhrase
        savePhrases(context, list)
        return true
    }

    /**
     * 删除指定索引的常用语
     */
    fun deletePhrase(context: Context, index: Int): Boolean {
        val list = getPhrases(context)
        if (index < 0 || index >= list.size) return false
        list.removeAt(index)
        savePhrases(context, list)
        return true
    }

    /**
     * 重置为默认常用语
     */
    fun resetToDefault(context: Context) {
        savePhrases(context, defaultPhrases)
    }

    /**
     * 获取短语对应的动作（悬浮按钮面板使用）
     */
    fun getActions(context: Context): List<ActionHandler> =
        getPhrases(context).map { CopyAction(it) }
}
