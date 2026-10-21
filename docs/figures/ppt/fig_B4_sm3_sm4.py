#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-3　SM3 与 SM4 内核优化（报告 2.1.4 / 2.1.5）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, 'SM3 压栈、SM4 换查表',
              '两算法的优化方向不同，代价都落在「用哪个内核」这一个编译期开关上',
              huename='green')

PW, GAP = 6.05, 0.28
Y0, PH = 1.54, 4.06

# ══ 左：SM3
x = M
panel(s, x, Y0, PW, PH, 'green')
icon(s, 'hash', x + 0.20, Y0 + 0.18, 0.58, 'green')
block(s, x + 0.92, Y0 + 0.18, PW - 1.15, 0.66, [('SM3：以栈占用为中心', 19, hue('green')[0], True)],
      MSO_ANCHOR.MIDDLE)
label(s, x + 0.30, Y0 + 0.90, PW - 0.60, 'W[68] + W′[64] → 16 字环形调度',
      13, TEXT, False, PP_ALIGN.LEFT, 0.38)
for i, (t, hot) in enumerate([('REF', False), ('FAST', True), ('TINY', False)]):
    chip(s, x + 0.30, Y0 + 1.46 + i * 0.74, 1.24, 0.60, t, 'green', 15, on_dark=hot)
ring_slots(s, x + 2.44, Y0 + 2.94, 0.52, 16, hot=(0, 1, 2, 3), size=0.22,
           huename='green', hot_hue='amber')
label(s, x + 1.74, Y0 + 3.58, 1.40, '16 字环形', 12, MUTED, False, h=0.30)
big_num(s, x + 3.36, Y0 + 2.40, PW - 3.62, '368', 'B', 'MCU 内核压缩栈',
        huename='green', nsize=34, lsize=13)

# ══ 右：SM4
x = M + PW + GAP
panel(s, x, Y0, PW, PH, 'purple')
icon(s, 'grid-2x2', x + 0.20, Y0 + 0.18, 0.58, 'purple')
block(s, x + 0.92, Y0 + 0.18, PW - 1.15, 0.66, [('SM4：以访存为中心', 19, hue('purple')[0], True)],
      MSO_ANCHOR.MIDDLE)
label(s, x + 0.30, Y0 + 0.90, PW - 0.60, 'τ 与 L 分步 → T-table 合并', 13, TEXT, False,
      PP_ALIGN.LEFT, 0.38)
for i, (t, hot) in enumerate([('REF', False), ('FAST', True), ('SMALL', False)]):
    chip(s, x + 0.30, Y0 + 1.46 + i * 0.74, 1.46, 0.60, t, 'purple', 15, on_dark=hot)
grid_cells(s, x + 2.10, Y0 + 1.52, 8, 4, 0.26, 0.21, huename='purple')
label(s, x + 1.94, Y0 + 2.62, 1.60, 'T-table 4 KB', 12, MUTED, False,
      PP_ALIGN.CENTER, 0.30)
big_num(s, x + 3.66, Y0 + 2.70, PW - 3.92, '1.55×', '', '单块加密',
        huename='purple', nsize=30, lsize=13)

footer(s, '图 2-3　SM3 的判据是栈深，SM4 的判据是速度与静态 RAM 的取舍；数据取同轮基准输出。',
       '数据来源：docs/04、bin/bench_results.md。', fs=0.95)

save(prs, '图2-3-SM3与SM4内核优化.pptx')
sys.exit(1 if qa(prs) else 0)
