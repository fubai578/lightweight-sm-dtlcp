#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 3-2　F103 真机算力（论文式棒棒糖图，无卡片）"""
from figlib import *

prs = new_deck()
s = plate(prs)

LX, BASE = 2.80, 3.00          # 名称列宽 / 基线位置
VMAX = 6.0
XSCALE = 5.60
ROWS = [
    ('SM2 密钥生成', 5.44, 'teal', True),
    ('SM2 签名', 4.07, 'teal', True),
    ('SM2 验签', 3.24, 'teal', True),
    ('SM2 ECDH', 2.15, 'teal', False),
    ('SM4 单块', 1.56, 'blue', True),
    ('SM4-CBC 1 KB', 1.48, 'blue', False),
    ('SM4-CTR 1 KB', 1.48, 'blue', False),
    ('SM3 1 KB', 1.18, 'green', True),
    ('HMAC-SM3', 1.17, 'green', False),
]
Y0, STEP = 0.72, 0.66
# 基线（1.00×）
vrule(s, BASE, Y0 - 0.20, Y0 + 8 * STEP + 0.42, color=INK, lw=1.0)
tick(s, BASE, Y0 + 8 * STEP + 0.50, '1.00×', 10, MUTED, w=1.0)
for i, (name, v, hn, hot) in enumerate(ROWS):
    y = Y0 + i * STEP
    label(s, M - 0.10, y - 0.20, LX - 0.30, name, 12, TEXT, False, PP_ALIGN.RIGHT, 0.40)
    x1 = BASE + (v - 1.0) / (VMAX - 1.0) * XSCALE
    dotbar(s, BASE, x1, y, hn, dot=0.16, lw=2.0)
    label(s, x1 + 0.12, y - 0.20, 0.95, '%.2f×' % v, 12,
          hue(hn)[0] if hot else MUTED, True, PP_ALIGN.LEFT, 0.40)
# 分组细线
for yy in (Y0 + 3.5 * STEP, Y0 + 6.5 * STEP):
    hrule(s, M, yy, 9.2, color=RULE, lw=0.75)
label(s, M + 0.20, Y0 - 0.52, 7.0, 'REF → MCU 档加速比', 11, MUTED, False,
      PP_ALIGN.LEFT, 0.30)

# ── 右：SM3 四档（同一基线风格的棒棒糖）
RX, RB = 9.60, 11.30
vrule(s, RB, Y0 - 0.20, Y0 + 3 * STEP + 0.42, color=RULE, lw=1.0)
block(s, RX + 0.10, Y0 - 0.72, 3.1, 0.40, [('SM3 四档内核（1 KB）', 12, INK, True)],
      MSO_ANCHOR.MIDDLE, PP_ALIGN.LEFT)
KER = [('REF', 90.2, 'blue', False), ('FAST', 106.6, 'amber', True),
       ('TINY', 100.6, 'blue', False), ('MCU', 76.5, 'green', True)]
for i, (t, v, hn, hot) in enumerate(KER):
    y = Y0 + i * STEP
    label(s, RX + 0.10, y - 0.20, 0.90, t, 11.5, TEXT, False, PP_ALIGN.RIGHT, 0.40)
    x1 = RB + v / 120.0 * 0.72
    dotbar(s, RB, x1, y, hn, dot=0.15, lw=2.0)
    label(s, x1 + 0.08, y - 0.20, 0.70, '%.0f' % v, 11, hue(hn)[0], True,
          PP_ALIGN.LEFT, 0.40)
label(s, RX + 0.10, Y0 + 3 * STEP + 0.62, 3.1, '千 cycles，越低越快', 10, MUTED,
      False, PP_ALIGN.LEFT, 0.28)

save(prs, '图3-2-F103真机算力.pptx')
sys.exit(1 if qa(prs) else 0)
