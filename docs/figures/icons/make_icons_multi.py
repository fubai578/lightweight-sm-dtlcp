#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_icons_multi.py — 把图标渲染结果上色成**多色系**成品（透明底）

输入：/tmp/blkpng/*.png（由 README 的 LibreOffice 命令从黑版 SVG 渲染，512×512）
输出：png2/<色名>/<图标名>.png

配色族（与 figlib 的 P 字典一一对应）——报告插图按语义分配色族：
    teal 结构/协议   blue 传输/流程   green 算法-SM3   purple 算法-SM4
    amber 算力/成本  rose 风险/落后    ink 标题与中性     white 色块内/深底上
"""
import glob
import os
import sys
from PIL import Image

SRC = sys.argv[1] if len(sys.argv) > 1 else '/tmp/blkpng'
HERE = os.path.dirname(os.path.abspath(__file__))

COLORS = {
    'teal':   (0x0E, 0x74, 0x90),
    'blue':   (0x1D, 0x4E, 0x89),
    'green':  (0x2F, 0x7D, 0x4F),
    'purple': (0x6D, 0x4A, 0xA8),
    'amber':  (0xB4, 0x53, 0x09),
    'rose':   (0xB0, 0x30, 0x52),
    'ink':    (0x12, 0x31, 0x3A),
    'gray':   (0x55, 0x70, 0x7A),
    'white':  (0xFF, 0xFF, 0xFF),
}


def tint(path, rgb, out):
    im = Image.open(path).convert('L')            # 笔画黑、底白
    a = im.point(lambda v: 255 - v)               # 亮度取反 → alpha
    a = a.point(lambda v: min(255, int(v * 1.15)))
    out_im = Image.new('RGBA', im.size, rgb + (0,))
    out_im.putalpha(a)
    out_im.save(out)


n = 0
for f in sorted(glob.glob(os.path.join(SRC, '*.png'))):
    name = os.path.splitext(os.path.basename(f))[0]
    for cname, rgb in COLORS.items():
        d = os.path.join(HERE, 'png2', cname)
        os.makedirs(d, exist_ok=True)
        tint(f, rgb, os.path.join(d, name + '.png'))
        n += 1
print('已生成 %d 个图标（%d 图标 × %d 色）' % (n, len(glob.glob(os.path.join(SRC, '*.png'))),
                                       len(COLORS)))
