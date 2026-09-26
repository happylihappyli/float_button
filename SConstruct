"""
SConstruct - Windows 浮动按钮 C++ 项目构建脚本
- 工具链: MSVC (VS2022 Community, x64)
- C++ 标准: C++20
- 编码: UTF-8
- 输出: bin/float_button.exe
- 中间文件: obj/*.obj
"""

import os
import sys
import glob
import subprocess
from datetime import datetime

# ============================================================
# 配置区
# ============================================================
APP_NAME = "float_button"
SRC_DIR = "src"
OBJ_DIR = "obj"
BIN_DIR = "bin"

# C++ 源文件
SOURCES = [
    "src/main.cpp",
    "src/float_window.cpp",
    "src/phrase_mgr.cpp",
    "src/shortcut_mgr.cpp",
    "src/shortcut_edit_dialog.cpp",
    "src/tray.cpp",
    "src/hotkey.cpp",
    "src/window_helper.cpp",
    "src/phrase_edit_dialog.cpp",
    "src/config.cpp",
    "src/settings_dialog.cpp",
    "src/tts_dialog.cpp",
    "src/autostart.cpp",
    "src/llm_client.cpp",
    "src/llm_settings_dialog.cpp",
]

# 资源文件（.rc 会引用 res/app_icon.ico）
RES_SOURCES = [
    "app_icon.rc",
]

# Windows 库
LIBS = [
    "user32", "gdi32", "gdiplus", "shell32", "comctl32",
    "comdlg32", "advapi32", "ole32", "uuid", "sapi",
    "wininet",
]

# ============================================================
# 时间显示
# ============================================================
def timestamp():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

print(f"[SCons start] {timestamp()}")

# ============================================================
# 编译选项
# ============================================================
env = Environment(tools=["default", "msvc"])

# 强制使用 vcvars 设置的工具（vsvars 已经设了）
# 设置目标为 x64
env["TARGET_ARCH"] = "x64"
env["HOST_ARCH"] = "x64"
env["MSVC_ARCH"] = "x64"

# C++20 + UTF-8
env.Append(CPPFLAGS=[
    "/std:c++20",
    "/utf-8",
    "/EHsc",
    "/W3",
    "/Zc:__cplusplus",
    "/O2",
    "/MD",
    "/nologo",
])

env.Append(CPPDEFINES=[
    "UNICODE",
    "_UNICODE",
    "WIN32",
    "_WIN32_WINNT=0x0601",
    "NOMINMAX",
])

# 包含路径
env.Append(CPPPATH=["src"])

# 链接器
env.Append(LINKFLAGS=[
    "/SUBSYSTEM:WINDOWS",
    "/ENTRY:WinMainCRTStartup",
    "/MACHINE:X64",
    "/nologo",
])

# 输出目录
if not os.path.exists(OBJ_DIR):
    os.makedirs(OBJ_DIR)
if not os.path.exists(BIN_DIR):
    os.makedirs(BIN_DIR)

# ============================================================
# 编译前自动结束正在运行的 exe，避免 "拒绝访问"
# ============================================================
def kill_running_app():
    """结束正在运行的 float_button.exe 进程"""
    exe_name = f"{APP_NAME}.exe"
    try:
        # /F 强制结束，/T 结束子进程，/IM 按映像名匹配
        result = subprocess.run(
            ["taskkill", "/F", "/T", "/IM", exe_name],
            capture_output=True, text=True, encoding="gbk", errors="ignore"
        )
        if result.returncode == 0:
            print(f"[Kill] 已结束 {exe_name} 进程，等待进程释放...")
            import time
            time.sleep(0.5)  # 等待进程对象完全释放，避免删除 exe 时拒绝访问
        # returncode == 128 表示进程不存在，正常情况，不打印
    except Exception as e:
        print(f"[Kill] 结束进程失败: {e}")

kill_running_app()

# 清理旧文件（带重试，避免进程未完全释放时删除失败）
def safe_remove(path, retries=5, delay=0.3):
    """安全删除文件，带重试机制"""
    import time
    for i in range(retries):
        try:
            if os.path.exists(path):
                os.remove(path)
            return True
        except PermissionError:
            if i < retries - 1:
                time.sleep(delay)
            else:
                print(f"[Warn] 无法删除 {path}（可能仍被占用）")
                return False
    return False

for old in glob.glob(os.path.join(OBJ_DIR, "*.obj")):
    safe_remove(old)
for old in glob.glob(os.path.join(OBJ_DIR, "*.res")):
    safe_remove(old)
safe_remove(os.path.join(BIN_DIR, f"{APP_NAME}.exe"))
safe_remove(os.path.join(BIN_DIR, f"{APP_NAME}.log"))

# ============================================================
# 资源生成：先调用 Python 脚本把 SVG 设计稿栅格化成 .ico
# （这一步是 .rc 文件的前置依赖）
# ============================================================
def find_python():
    """找一个可用的 python 解释器（兼容 vcvars 切换后 PATH 丢失的情况）。"""
    import shutil
    # 优先使用 PATH 中的 python / py
    for cand in ("python", "py", "python3"):
        p = shutil.which(cand)
        if p:
            return p
    # 常见 conda 路径兜底
    fallback = r"D:\Code\anaconda3\python.exe"
    if os.path.exists(fallback):
        return fallback
    return "python"  # 最后让 OS 报错

icon_ico = os.path.join("res", "app_icon.ico")
icon_gen_script = os.path.join("res", "gen_icon.py")
icon_svg = os.path.join("res", "app_icon.svg")

gen_icon_action = env.Command(
    target=icon_ico,
    source=[icon_gen_script, icon_svg],
    action=f'"{find_python()}" "{icon_gen_script}"',
)

# ============================================================
# 构建目标
# ============================================================
objects = []
for src in SOURCES:
    obj = env.Object(
        target=os.path.join(OBJ_DIR, os.path.basename(src).replace(".cpp", ".obj")),
        source=src,
    )
    objects.append(obj)

# 把 .rc 编译成 .res，并加入链接目标
for rc in RES_SOURCES:
    res_target = os.path.join(OBJ_DIR, os.path.basename(rc).replace(".rc", ".res"))
    res_obj = env.RES(res_target, source=rc)
    objects.append(res_obj)

target = env.Program(
    target=os.path.join(BIN_DIR, f"{APP_NAME}.exe"),
    source=objects,
    LIBS=LIBS,
)

def print_finish(target, source, env):
    print(f"[SCons end] {timestamp()}")
    print(f"[Output] {target[0].abspath}")
    print(f"[Status] BUILD SUCCESSFUL")
    return None

env.AddPostAction(target, print_finish)

Default(target)
