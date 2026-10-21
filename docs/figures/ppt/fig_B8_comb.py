#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-5　Lim-Lee comb 预计算表的结构与迭代（报告 2.1.7）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, 'comb 预计算表：257 次倍点 → 51 次',
              '密钥生成与签名都要计算 k·G，而 G 是固定基点',
              huename='amber')

# ① 标量按固定间隔分组
label(s, M, 1.48, 9.0, '① 按固定间隔取 t 位，组成平衡量 K_j', 14, INK, True,
      PP_ALIGN.LEFT, 0.40)
BX, BY, CW, BH = M, 1.94, 0.372, 0.62
HS = ['teal', 'purple', 'green', 'amber']
for i in range(32):
    hn = HS[(i // 8) % 4]
    shape(s, MSO_SHAPE.RECTANGLE, BX + i * CW, BY, CW - 0.02, BH,
          fill=hue(hn)[1], line=hue(hn)[2], lw=1.0)
    label(s, BX + i * CW, BY, CW - 0.02, str((i * 7) % 2), 11, hue(hn)[0], True, h=BH)
for j in range(4):
    x0 = BX + j * 8 * CW
    seg(s, x0, BY + BH + 0.04, x0 + 8 * CW - 0.02, BY + BH + 0.04,
        color=hue(HS[j])[0], lw=2.5, tail=True, head=True)
    label(s, x0, BY + BH + 0.06, 8 * CW, 'K_%d' % j, 12, hue(HS[j])[0], True, h=0.32)

# ② 预计算表
TY = 3.44
label(s, M, TY - 0.44, 11.5, '② 预计算表 T[α]：32 项 · 2,048 B · 只读常量存 Flash · RAM 增量 0',
      14, INK, True, PP_ALIGN.LEFT, 0.40)
for i in range(32):
    shape(s, MSO_SHAPE.RECTANGLE, M + i * CW, TY, CW - 0.02, BH,
          fill=hue('amber')[1] if i % 2 == 0 else PAPER, line=hue('amber')[2], lw=1.0)
    label(s, M + i * CW, TY, CW - 0.02, str(i), 10.5, hue('amber')[0], False, h=BH)

# ③ 迭代与自检
Y3, PH3 = 4.42, 1.72
panel(s, M, Y3, BODY_W, PH3, 'blue')
big_num(s, M + 0.40, Y3 + 0.26, 2.30, '257', '', '倍点次数（基线）',
        huename='rose', nsize=34, lsize=12.5, align=PP_ALIGN.LEFT)
arrow(s, M + 2.96, Y3 + 0.76, M + 3.52, Y3 + 0.76, 'amber', 3.5)
big_num(s, M + 3.72, Y3 + 0.26, 2.30, '51', '', '倍点次数（comb）',
        huename='green', nsize=34, lsize=12.5, align=PP_ALIGN.LEFT)
icon(s, 'fingerprint', M + 6.30, Y3 + 0.28, 0.52, 'teal')
block(s, M + 6.94, Y3 + 0.26, BODY_W - 7.20, 0.60,
      [('生成器自检', 17, hue('teal')[0], True)], MSO_ANCHOR.MIDDLE)
label(s, M + 6.94, Y3 + 0.94, BODY_W - 7.20,
      '基元独立算出 · 16 个标量与基线逐位一致', 12.5, TEXT, False,
      PP_ALIGN.LEFT, 0.66)

footer(s, '图 2-5　净收益用同内核、仅关表的 A/B 变体隔离测量：密钥生成 2.62×、签名 2.07×。',
       '数据来源：apps/gen/gen_fixed_base.c、bin/bench_results.md。', fs=0.95)

save(prs, '图2-5-comb预计算表结构.pptx')
sys.exit(1 if qa(prs) else 0)
