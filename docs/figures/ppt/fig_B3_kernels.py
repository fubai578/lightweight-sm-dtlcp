#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-2　多档内核与差分验证（报告 2.1.3）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '多档内核与差分验证',
              '同一套 API、同一输入集；输出汇聚为 SM3 差分指纹，要求逐位一致',
              huename='green')

# 左：输入
card(s, M, 2.30, 2.46, 1.90, '固定输入集', ['4 个操作', '确定性种子'],
     huename='teal', icon_name='target', icon_size=0.62, tsize=14, lsize=13,
     tone='white')

# 中：六个变体
MX, MY, MW, MH = 3.32, 1.50, 4.60, 3.30
panel(s, MX, MY, MW, MH, 'green')
icon(s, 'layers', MX + 0.22, MY + 0.20, 0.56, 'green')
block(s, MX + 0.90, MY + 0.20, MW - 1.1, 0.62, [('7 个内核变体', 20, hue('green')[0], True)],
      MSO_ANCHOR.MIDDLE)
KEYS = [('layers', 'ref', 'teal'), ('zap', 'opt', 'green'), ('database', 'tiny', 'purple'),
        ('cpu', 'mcu', 'rose'), ('sliders-horizontal', 'notab', 'blue'),
        ('lock', 'ct', 'amber'), ('boxes', 'lite', 'purple')]
CW2, CH2, GAP2 = 1.02, 1.00, 0.10
for i, (ic, t, hn) in enumerate(KEYS):
    cx = MX + 0.22 + (i % 4) * (CW2 + GAP2)
    cy = MY + 1.00 + (i // 4) * (CH2 + GAP2)
    deep, tint, mid = hue(hn)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, cx, cy, CW2, CH2, fill=PAPER,
          line=deep, lw=1.5, radius=0.18)
    icon(s, ic, cx + CW2 / 2 - 0.24, cy + 0.14, 0.48, hn)
    label(s, cx, cy + 0.66, CW2, t, 13.5, deep, True, h=0.30)

# 右：指纹
RX, RY, RW, RH = 8.20, 1.86, W - M - 8.20, 2.90
panel(s, RX, RY, RW, RH, 'amber')
icon(s, 'fingerprint', RX + 0.22, RY + 0.20, 0.56, 'amber')
block(s, RX + 0.90, RY + 0.20, RW - 1.1, 0.62, [('SM3 差分指纹', 20, hue('amber')[0], True)],
      MSO_ANCHOR.MIDDLE)
tb = textbox(s, RX + 0.24, RY + 1.00, RW - 0.48, 1.30, MSO_ANCHOR.MIDDLE)
for i, seg_txt in enumerate(['64a5385d17ad44be', '141970496e5b70dc', '…f5b49eaf']):
    para(tb.text_frame, i == 0, [(seg_txt, 13, INK, True)], PP_ALIGN.CENTER)
for p_ in tb.text_frame.paragraphs:
    r = p_.runs[0]
    r.font.name = 'Consolas'
    rPr = r._r.get_or_add_rPr()
    from pptx.oxml.ns import qn as _qn
    for tag in ('a:ea', 'a:cs'):
        el = rPr.find(_qn(tag))
        if el is not None:
            el.set('typeface', 'Consolas')
badge(s, RX + 0.24, RY + 2.40, RW - 0.48, 0.50, '7 变体逐位一致', 'green',
      'circle-check', 14)

arrow(s, M + 2.56, 3.05, MX - 0.08, 3.05, 'teal', 3.5)
arrow(s, MX + MW + 0.08, 3.05, RX - 0.08, 3.05, 'amber', 3.5)

card(s, M, 5.10, BODY_W, 0.72, '任何一处优化算错，指纹立即改变',
     None, huename='rose', icon_name='badge-check', icon_size=0.56, tsize=18,
     tone='white')

# 题注由报告正文（Word 题注）负责，图内不再嵌图号与数据来源

save(prs, '图2-2-多档内核与差分验证.pptx')
sys.exit(1 if qa(prs) else 0)
