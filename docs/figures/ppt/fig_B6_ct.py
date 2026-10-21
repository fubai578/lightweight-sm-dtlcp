#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-6　常量时间与预计算表的共存（报告 2.1.8）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '常量时间与预计算表不再二选一',
              '对静态 comb 表做全表扫描 + 条件搬运（cmov）',
              huename='rose')

PW = 5.90
X1, X2, Y0, PH = M, M + PW + 0.53, 1.54, 3.62

# ── 左：常规做法
panel(s, X1, Y0, PW, PH, 'rose')
icon(s, 'eye-off', X1 + 0.22, Y0 + 0.20, 0.56, 'rose')
block(s, X1 + 0.90, Y0 + 0.20, PW - 1.2, 0.56,
      [('常规做法：放弃查表', 18, hue('rose')[0], True)], MSO_ANCHOR.MIDDLE)
label(s, X1 + 0.30, Y0 + 0.94, PW - 0.60, '表项下标由秘密位决定',
      13.5, TEXT, False, PP_ALIGN.LEFT, 0.34)
big_num(s, X1 + 0.30, Y0 + 1.36, 3.20, '2.8×', '', '相对非防护构建',
        huename='rose', nsize=32, lsize=12, align=PP_ALIGN.LEFT)
bars(s, X1 + 0.30, Y0 + 2.80, PW - 0.90,
     [('', 2.8, 'rose', False)], 3.0, hgt=0.30, gap=0.06, show_val=False)
label(s, X1 + 0.30, Y0 + 3.14, PW - 0.60, '退回逐位倍点—点加', 12.5, MUTED, False,
      PP_ALIGN.LEFT, 0.26)

# ── 右：本作品
panel(s, X2, Y0, PW, PH, 'green')
icon(s, 'lock', X2 + 0.22, Y0 + 0.20, 0.56, 'green')
block(s, X2 + 0.90, Y0 + 0.20, PW - 1.2, 0.56,
      [('本作品：全表扫描＋掩码', 18, hue('green')[0], True)], MSO_ANCHOR.MIDDLE)
label(s, X2 + 0.30, Y0 + 0.94, PW - 0.60, '32 项全做加法，掩码选择',
      13.5, TEXT, False, PP_ALIGN.LEFT, 0.34)
big_num(s, X2 + 0.30, Y0 + 1.36, 3.20, '1.14×', '', '基点路径（通用 1.10×）',
        huename='green', nsize=32, lsize=12, align=PP_ALIGN.LEFT)
bars(s, X2 + 0.30, Y0 + 2.80, PW - 0.90,
     [('', 1.13, 'green', True)], 3.0, hgt=0.30, gap=0.06, show_val=False)
label(s, X2 + 0.30, Y0 + 3.14, PW - 0.60, '均值口径，判据含域乘次数', 12.5, MUTED, False,
      PP_ALIGN.LEFT, 0.26)

arrow(s, X1 + PW + 0.08, Y0 + PH / 2, X2 - 0.08, Y0 + PH / 2, 'amber', 3.0)

# ── 底部：三条恒定性与口径
Y1 = 5.28
BAD = [('circle-check', 'green', '点加 51/51'), ('circle-check', 'green', '倍点 51/51'),
       ('circle-check', 'amber', '极差 0')]
for i, (ic, hn, t) in enumerate(BAD):
    badge(s, M + i * 2.62, Y1, 2.50, 0.62, t, hn, ic, 14)
card(s, M + 7.96, Y1 - 0.04, BODY_W - 7.46, 0.70, None, [], huename='teal',
     tone='white')
icon(s, 'eye', M + 8.14, Y1 + 0.10, 0.46, 'teal')
block(s, M + 8.70, Y1 - 0.02, BODY_W - 8.36, 0.66,
      [('判据必须含域乘次数', 14, hue('teal')[0], True),
       ('仅数调用次数会漏掉实际工作量', 12, TEXT, False)], MSO_ANCHOR.MIDDLE)


footer(s, '图 2-6　结论：用预计算表与常量时间并不冲突。防护有效性用确定性操作计数判定，'
          '4096 标量扫描的极差为 0。',
       '数据来源：scripts/ct_cost.sh、apps/bench/bench_sct.c。')

save(prs, '图2-6-常量时间与预计算表共存.pptx')
sys.exit(1 if qa(prs) else 0)
