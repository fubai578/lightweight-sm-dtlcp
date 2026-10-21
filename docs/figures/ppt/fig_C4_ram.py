#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 3-4　F103 资源预算（论文式堆叠条 + 引导线，无卡片）"""
from figlib import *

prs = new_deck()
s = plate(prs)

LX, LW = 0.95, 11.30
# ── RAM
block(s, LX - 0.25, 0.62, 6.0, 0.40, [('F103 RAM（20,480 B）', 12, INK, True)],
      MSO_ANCHOR.MIDDLE, PP_ALIGN.LEFT)
BY, BH = 1.32, 1.20
SEGS = [('协议上下文', 5248, 'teal'), ('静态数据', 4104, 'blue'),
        ('最长栈（下界）', 4872, 'purple'), ('余量', 6256, 'green')]
SCALE = LW / 20480.0
x = LX
for name, v, hn in SEGS:
    w = v * SCALE
    shape(s, MSO_SHAPE.RECTANGLE, x, BY, w - 0.04, BH, fill=hue(hn)[1],
          line=hue(hn)[2], lw=1.0)
    label(s, x, BY + 0.10, w - 0.04, str(v), 14, hue(hn)[0], True, h=0.36)
    label(s, x, BY + 0.50, w - 0.04, name, 10, TEXT, False, h=0.36)
    x += w
# 容量标尺：细线 + 端点
hrule(s, LX, BY + BH + 0.30, LW, color=HAIR, lw=1.0)
seg(s, LX, BY + BH + 0.22, LX, BY + BH + 0.38, color=HAIR, lw=1.0)
seg(s, LX + LW, BY + BH + 0.22, LX + LW, BY + BH + 0.38, color=HAIR, lw=1.0)
label(s, LX, BY + BH + 0.36, 5.0, '0', 9.5, MUTED, False, PP_ALIGN.LEFT, 0.24)
label(s, LX + LW - 1.6, BY + BH + 0.36, 1.6, '20,480', 9.5, MUTED, False,
      PP_ALIGN.RIGHT, 0.24)
label(s, LX, BY + BH + 0.78, LW, '下界合计 14,224 B，余量 6,256 B',
      13, hue('amber')[0], True, PP_ALIGN.LEFT, 0.40)

# ── Flash
QY = 4.30
block(s, LX - 0.25, QY, 6.0, 0.40, [('Flash（32,768 B 器件）', 12, INK, True)],
      MSO_ANCHOR.MIDDLE, PP_ALIGN.LEFT)
BY2, BH2 = QY + 0.62, 1.05
fw = LW * 29720 / 32768.0
shape(s, MSO_SHAPE.RECTANGLE, LX, BY2, fw, BH2, fill=hue('teal')[1],
      line=hue('teal')[2], lw=1.0)
label(s, LX, BY2 + 0.10, fw, '29,720 B', 14, hue('teal')[0], True, h=0.36)
label(s, LX, BY2 + 0.50, fw, '最小配置固件', 10, TEXT, False, h=0.36)
shape(s, MSO_SHAPE.RECTANGLE, LX + fw, BY2, LW - fw, BH2, fill='FAFBFB',
      line=HAIR, lw=1.0)
hrule(s, LX, BY2 + BH2 + 0.30, LW, color=HAIR, lw=1.0)
label(s, LX, BY2 + BH2 + 0.36, 5.0, '0', 9.5, MUTED, False, PP_ALIGN.LEFT, 0.24)
label(s, LX + LW - 1.6, BY2 + BH2 + 0.36, 1.6, '32,768', 9.5, MUTED, False,
      PP_ALIGN.RIGHT, 0.24)
label(s, LX, BY2 + BH2 + 0.78, LW,
      'W5500 整机 text 26,116 / 26,556 B（client / server）', 11, MUTED, False,
      PP_ALIGN.LEFT, 0.36)

save(prs, '图3-4-资源预算.pptx')
sys.exit(1 if qa(prs) else 0)
