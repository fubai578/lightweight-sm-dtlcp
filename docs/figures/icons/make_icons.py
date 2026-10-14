#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_icons.py — 把黑色渲染的图标上色成图内配色（青绿 / 赭橙），透明底。

输入：/tmp/blkpng/*.png（由 README 里的 LibreOffice 命令从黑版 SVG 渲染得到）
输出：png/<name>.png（青绿）与 png/<name>_accent.png（赭橙）
"""
import glob
import os
import sys
from PIL import Image

SRC = sys.argv[1] if len(sys.argv) > 1 else '/tmp/blkpng'
PRIMARY = (0x0F, 0x4C, 0x5C)
ACCENT = (0xC2, 0x41, 0x0C)
os.makedirs('png', exist_ok=True)


def tint(path, rgb, out):
    im = Image.open(path).convert('L')       # 笔画黑、底白
    a = im.point(lambda v: 255 - v)          # 亮度取反 → alpha
    a = a.point(lambda v: min(255, int(v * 1.15)))   # 轻微增益，细笔画更清晰
    out_im = Image.new('RGBA', im.size, rgb + (0,))
    out_im.putalpha(a)
    out_im.save(out)


n = 0
for f in sorted(glob.glob(os.path.join(SRC, '*.png'))):
    name = os.path.splitext(os.path.basename(f))[0]
    tint(f, PRIMARY, 'png/%s.png' % name)
    tint(f, ACCENT, 'png/%s_accent.png' % name)
    n += 2
print('已生成 %d 个图标（青绿 + 赭橙）' % n)
