package com.example.floatbutton

import android.app.AlertDialog
import android.content.Context
import android.os.Bundle
import android.text.InputType
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.ImageButton
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * 常用语编辑界面
 * 功能：
 *  - 显示当前所有常用语
 *  - 长按某条弹出编辑/删除菜单
 *  - 顶部"+ 新增"按钮添加新条目
 *  - 右上角菜单可"恢复默认"
 */
class PhraseEditActivity : AppCompatActivity() {

    private lateinit var listView: ListView
    private lateinit var adapter: PhraseAdapter
    private var phrases: MutableList<String> = mutableListOf()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_phrase_edit)

        // 初始化数据
        phrases = PhraseManager.getPhrases(this)

        // 找到视图
        listView = findViewById(R.id.lv_phrases)
        findViewById<ImageButton>(R.id.btn_add).setOnClickListener { showAddDialog() }
        findViewById<ImageButton>(R.id.btn_reset).setOnClickListener { showResetDialog() }
        findViewById<ImageButton>(R.id.btn_back).setOnClickListener { finish() }

        // 设置列表适配器
        adapter = PhraseAdapter(this, phrases)
        listView.adapter = adapter

        // 长按弹出编辑/删除菜单
        listView.setOnItemLongClickListener { _, _, position, _ ->
            showItemMenu(position)
            true
        }

        // 点击单项直接进入编辑
        listView.setOnItemClickListener { _, _, position, _ ->
            showEditDialog(position)
        }
    }

    /**
     * 显示新增对话框
     */
    private fun showAddDialog() {
        showInputDialog("新增常用语", "") { input ->
            if (PhraseManager.addPhrase(this, input)) {
                phrases.add(input)
                adapter.notifyDataSetChanged()
                Toast.makeText(this, "已添加", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "添加失败（已存在或为空）", Toast.LENGTH_SHORT).show()
            }
        }
    }

    /**
     * 显示编辑对话框
     */
    private fun showEditDialog(position: Int) {
        val current = phrases[position]
        showInputDialog("编辑常用语", current) { input ->
            if (PhraseManager.updatePhrase(this, position, input)) {
                phrases[position] = input
                adapter.notifyDataSetChanged()
                Toast.makeText(this, "已更新", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "更新失败", Toast.LENGTH_SHORT).show()
            }
        }
    }

    /**
     * 显示单项操作菜单（编辑/删除/上移/下移）
     */
    private fun showItemMenu(position: Int) {
        val items = arrayOf("编辑", "删除", "上移", "下移")
        AlertDialog.Builder(this)
            .setTitle("操作：${phrases[position]}")
            .setItems(items) { _, which ->
                when (which) {
                    0 -> showEditDialog(position)         // 编辑
                    1 -> deletePhrase(position)           // 删除
                    2 -> movePhrase(position, -1)         // 上移
                    3 -> movePhrase(position, 1)          // 下移
                }
            }
            .show()
    }

    /**
     * 删除常用语
     */
    private fun deletePhrase(position: Int) {
        AlertDialog.Builder(this)
            .setTitle("确认删除")
            .setMessage("确定要删除「${phrases[position]}」吗？")
            .setPositiveButton("删除") { _, _ ->
                if (PhraseManager.deletePhrase(this, position)) {
                    phrases.removeAt(position)
                    adapter.notifyDataSetChanged()
                    Toast.makeText(this, "已删除", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }

    /**
     * 移动常用语位置
     */
    private fun movePhrase(position: Int, delta: Int) {
        val newPos = position + delta
        if (newPos < 0 || newPos >= phrases.size) {
            Toast.makeText(this, "已到边界", Toast.LENGTH_SHORT).show()
            return
        }
        val item = phrases.removeAt(position)
        phrases.add(newPos, item)
        PhraseManager.savePhrases(this, phrases)
        adapter.notifyDataSetChanged()
        Toast.makeText(this, "已移动", Toast.LENGTH_SHORT).show()
    }

    /**
     * 显示恢复默认确认对话框
     */
    private fun showResetDialog() {
        AlertDialog.Builder(this)
            .setTitle("恢复默认常用语")
            .setMessage("将清除所有自定义常用语，恢复为系统默认。\n确定要继续吗？")
            .setPositiveButton("恢复") { _, _ ->
                PhraseManager.resetToDefault(this)
                phrases.clear()
                phrases.addAll(PhraseManager.getPhrases(this))
                adapter.notifyDataSetChanged()
                Toast.makeText(this, "已恢复默认", Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton("取消", null)
            .show()
    }

    /**
     * 通用输入对话框
     */
    private fun showInputDialog(title: String, defaultValue: String, onConfirm: (String) -> Unit) {
        val editText = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            setText(defaultValue)
            minLines = 2
            // 自动获取焦点并选中文本
            setSelectAllOnFocus(true)
        }
        AlertDialog.Builder(this)
            .setTitle(title)
            .setView(editText)
            .setPositiveButton("确定") { _, _ ->
                val input = editText.text.toString().trim()
                onConfirm(input)
            }
            .setNegativeButton("取消", null)
            .show()
        editText.requestFocus()
    }

    /**
     * 列表适配器（自定义布局，支持长按高亮）
     */
    private class PhraseAdapter(
        context: Context,
        private val items: List<String>
    ) : ArrayAdapter<String>(context, 0, items) {

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val view = convertView ?: LayoutInflater.from(context)
                .inflate(R.layout.item_phrase, parent, false)

            val tvIndex = view.findViewById<TextView>(R.id.tv_index)
            val tvText = view.findViewById<TextView>(R.id.tv_text)

            tvIndex.text = (position + 1).toString()
            tvText.text = items[position]
            return view
        }
    }
}
