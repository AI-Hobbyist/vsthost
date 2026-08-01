# make_icon.py : 把根目录 icon.png 去掉白色背景并生成全尺寸 ICO
#   输出 res\icon.ico（16/24/32/48/64/128/256），供 exe 图标与托盘图标使用
import os
from PIL import Image

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(BASE, "icon.png")
OUT = os.path.join(BASE, "res", "icon.ico")
SIZES = [16, 24, 32, 48, 64, 128, 256]

if not os.path.exists(SRC):
    raise SystemExit("未找到 icon.png：" + SRC)

img = Image.open(SRC).convert("RGBA")
w, h = img.size
px = img.load()

# 去掉白色背景：白度越高越透明（235~200 之间线性过渡，边缘平滑）
for y in range(h):
    for x in range(w):
        r, g, b, a = px[x, y]
        white = min(r, g, b)
        if white >= 235:
            a = 0
        elif white > 200:
            a = int(a * (235 - white) / 35)
        px[x, y] = (r, g, b, a)

# 以原图保存 ICO（Pillow 按 sizes 内部生成各尺寸帧）
os.makedirs(os.path.dirname(OUT), exist_ok=True)
img.save(OUT, format="ICO", sizes=[(s, s) for s in SIZES])
print("已生成:", OUT)
print("尺寸:", ", ".join(f"{s}x{s}" for s in SIZES))
