# -*- coding: utf-8 -*-
"""
gen_icon.py
============
根据 res/app_icon.svg 的设计（v3：扁平 + 蓝色球 + 白色加号），
用 Pillow 渲染多尺寸 .ico。

为什么不用 cairosvg / svglib：
    依赖 cairo 库（cairo-2.dll），当前环境未安装。
    改用 Pillow 的绘图 API 复刻 SVG 设计。

输出：res/app_icon.ico
    内含尺寸：16, 24, 32, 48, 64, 128, 256
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SVG_PATH = os.path.join(HERE, "app_icon.svg")
OUT_ICO = os.path.join(HERE, "app_icon.ico")

SIZES = [16, 24, 32, 48, 64, 128, 256]

# 颜色（与 SVG 保持一致）
BG_COLOR    = (0x1E, 0x1B, 0x3A)   # 圆角矩形底：深紫蓝
BALL_COLOR  = (0x3B, 0x82, 0xF6)   # 蓝球
PLUS_COLOR  = (0xFF, 0xFF, 0xFF)   # 白色加号

# 设计稿中的几何参数（按 256 视口）
DESIGN = {
    "corner_radius": 48,     # 圆角矩形圆角
    "ball_cx": 128, "ball_cy": 128, "ball_r": 76,
    "plus_thickness": 16,    # 加号粗细
    "plus_length": 72,       # 加号一边长度
    "plus_corner": 2,        # 加号线端圆角
}


def render_one(size):
    """渲染 size×size 的图标（纯扁平，无抗锯齿以外的渐变/光晕）。"""
    s = size
    scale = s / 256.0
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 1. 圆角矩形底
    radius = max(1, int(DESIGN["corner_radius"] * scale))
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=radius, fill=(*BG_COLOR, 255))

    # 2. 蓝色球
    cx, cy, r = int(DESIGN["ball_cx"] * scale), int(DESIGN["ball_cy"] * scale), int(DESIGN["ball_r"] * scale)
    # 用 4x 超采样画圆避免锯齿
    supersample = 4 if s >= 48 else 2
    big_s = s * supersample
    big_img = Image.new("RGBA", (big_s, big_s), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big_img)
    # 圆角矩形
    bd.rounded_rectangle(
        (0, 0, big_s - 1, big_s - 1),
        radius=max(1, int(radius * supersample)),
        fill=(*BG_COLOR, 255),
    )
    # 圆
    bd.ellipse(
        (int((cx - r) * supersample), int((cy - r) * supersample),
         int((cx + r) * supersample), int((cy + r) * supersample)),
        fill=(*BALL_COLOR, 255),
    )
    # 加号
    thick = int(DESIGN["plus_thickness"] * scale * supersample)
    length = int(DESIGN["plus_length"] * scale * supersample)
    plus_corner = max(1, int(DESIGN["plus_corner"] * scale * supersample))
    half = thick // 2
    # 水平线
    bd.rounded_rectangle(
        (int(cx * supersample) - length // 2, int(cy * supersample) - half,
         int(cx * supersample) + length // 2, int(cy * supersample) - half + thick),
        radius=plus_corner,
        fill=(*PLUS_COLOR, 255),
    )
    # 垂直线
    bd.rounded_rectangle(
        (int(cx * supersample) - half, int(cy * supersample) - length // 2,
         int(cx * supersample) - half + thick, int(cy * supersample) + length // 2),
        radius=plus_corner,
        fill=(*PLUS_COLOR, 255),
    )
    # 下采样回原尺寸（高质量重采样）
    img = big_img.resize((s, s), Image.LANCZOS)
    return img


def main():
    if not os.path.exists(SVG_PATH):
        print(f"[gen_icon] 警告：未找到设计稿 {SVG_PATH}，仍然继续生成。")

    print(f"[gen_icon] 开始生成多尺寸图标: {SIZES}")
    images = []
    for s in SIZES:
        im = render_one(s)
        images.append(im)
        debug_path = os.path.join(HERE, f"_debug_icon_{s}.png")
        im.save(debug_path, "PNG")
        print(f"  - {s}x{s} OK -> {debug_path}")

    # 保存为 .ico（多尺寸）
    # 注意：Pillow 在同时传 sizes= 与 append_images= 时只保留首帧，
    # 所以这里以最大尺寸图为主帧，用 append_images 追加其余尺寸。
    biggest = max(images, key=lambda im: im.size)
    biggest.save(
        OUT_ICO,
        format="ICO",
        append_images=images,
    )
    print(f"[gen_icon] 已生成 {OUT_ICO}")
    print(f"[gen_icon] 包含尺寸: {SIZES}")


if __name__ == "__main__":
    main()
