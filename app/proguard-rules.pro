# 混淆规则（目前不启用混淆）
-keep class com.example.floatbutton.** { *; }

# 保留 Kotlin 相关
-keepattributes *Annotation*
-keep class kotlin.** { *; }
-keep class kotlinx.** { *; }