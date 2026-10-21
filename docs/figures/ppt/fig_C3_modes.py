#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 3-3　F103+W5500 四模式（论文式棒棒糖图）"""
import math

from figlib import *

prs = new_deck()
s = plate(prs)

MODES = [('FULL', 2274.97, 1198, 5, 'teal', True),
         ('RESUME', 58.04, 428, 0, 'teal', False),
         ('PSK', 74.77, 412, 0, 'teal', False),
         ('PSK 0-RTT', 92.65, 472, 0, 'amber', True)]

# ── 左：时延（log）
LX, LBASE = 2.30, 2.50
block(s, M, 0.80, 5.4, 0.40, [('握手时延（ms，对数刻度）', 12, INK, True)],
      MSO_ANCHOR.MIDDLE, PP_ALIGN.LEFT)
vrule(s, LBASE, 1.45, 1.45 + 3 * 1.45 + 0.50, color=INK, lw=1.0)
tick(s, LBASE, 1.45 + 3 * 1.45 + 0.56, '1 ms', 10, MUTED, w=1.0)
maxlog = math.log10(2274.97)
for i, (name, ms, wire, sm2, hn, hot) in enumerate(MODES):
    y = 1.60 + i * 1.45
    label(s, LBASE - 2.24, y - 0.22, 2.06, name, 12, TEXT, False, PP_ALIGN.RIGHT, 0.44)
    x1 = LBASE + math.log10(ms) / maxlog * 3.30
    dotbar(s, LBASE, x1, y, hn, dot=0.17, lw=2.0)
    label(s, x1 + 0.14, y - 0.22, 1.12, '%.2f' % ms, 12,
          hue(hn)[0] if hot else MUTED, True, PP_ALIGN.LEFT, 0.44)


# ── 右：字节
RX, RBASE = 8.60, 9.25
vrule(s, 7.95, 0.30, 7.10, color=RULE, lw=0.75)
block(s, RX, 0.80, 4.4, 0.40, [('线上字节（B）', 12, INK, True)], MSO_ANCHOR.MIDDLE,
      PP_ALIGN.LEFT)
vrule(s, RBASE, 1.45, 1.45 + 3 * 1.45 + 0.50, color=INK, lw=1.0)
tick(s, RBASE, 1.45 + 3 * 1.45 + 0.56, '0', 10, MUTED, w=0.8)
for i, (name, ms, wire, sm2, hn, hot) in enumerate(MODES):
    y = 1.60 + i * 1.45
    label(s, RBASE - 1.98, y - 0.22, 1.80, name, 11.5, TEXT, False, PP_ALIGN.RIGHT, 0.44)
    x1 = RBASE + wire / 1200.0 * 1.95
    dotbar(s, RBASE, x1, y, hn, dot=0.16, lw=2.0)
    label(s, x1 + 0.12, y - 0.22, 0.90, '%d' % wire, 11.5,
          hue(hn)[0] if hot else MUTED, True, PP_ALIGN.LEFT, 0.44)
label(s, RX, 6.05, 4.4, '应用 RTT（ms）', 11, INK, True, PP_ALIGN.LEFT, 0.34)
label(s, RX, 6.42, 4.4, '2877 / 27 / 30 / 9', 11.5, TEXT, False, PP_ALIGN.LEFT, 0.40)

save(prs, '图3-3-真机四模式.pptx')
sys.exit(1 if qa(prs) else 0)
