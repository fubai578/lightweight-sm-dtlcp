#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 1-1　需求背景与三重资源瓶颈（报告 1.1）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '终端侧的三重资源瓶颈',
              '低成本 MCU：几十 KB Flash／RAM、几十 MHz 主频，电池供电、多年免维护',
              huename='amber')

W3, G3, Y0, H3 = 3.95, 0.24, 1.50, 3.72
BOT = [
    ('cpu',          'amber',  '算力瓶颈', '数百 ms', '单次 SM2 标量乘'),
    ('memory-stick', 'purple', '内存瓶颈', '数 KB',   '中间量展开在栈上'),
    ('network',      'blue',   '传输瓶颈', '3 RTT',   '完整握手多航班'),
]
for i, (ic, hn, name, num, note) in enumerate(BOT):
    x = M + i * (W3 + G3)
    deep, tint, mid = hue(hn)
    panel(s, x, Y0, W3, H3, hn)
    shape(s, MSO_SHAPE.OVAL, x + W3 / 2 - 0.66, Y0 + 0.22, 1.32, 1.32,
          fill=PAPER, line=mid, lw=1.5)
    icon(s, ic, x + W3 / 2 - 0.43, Y0 + 0.45, 0.86, hn)
    label(s, x + 0.20, Y0 + 1.60, W3 - 0.40, name, 22, deep, True, h=0.62)
    big_num(s, x + 0.20, Y0 + 2.26, W3 - 0.40, num, '', note, huename=hn,
            nsize=T_HUGE, lsize=T_LABEL)

Y1, H1 = 5.42, 0.98
SOL = [
    ('sliders-horizontal', 'green',  '算法轻量化与裁剪'),
    ('database',           'purple', '预计算表：Flash 换时间'),
    ('refresh-cw',         'blue',   '握手精简 1.5 / 1 / 0-RTT'),
]
for i, (ic, hn, t) in enumerate(SOL):
    x = M + i * (W3 + G3)
    seg(s, x + W3 / 2, Y0 + H3 + 0.02, x + W3 / 2, Y1 - 0.06,
        color=hue(BOT[i][1])[0], lw=3.0, head=True)
    card(s, x, Y1, W3, H1, t, None, huename=hn, icon_name=ic, icon_size=0.56,
         tsize=17, tone='white')

footer(s, '图 1-1　终端侧的三重资源瓶颈与对应优化方向：三条对策各自独立可用。',
       '数据来源：docs/01、docs/02。')

save(prs, '图1-1-需求与瓶颈分析.pptx')
sys.exit(1 if qa(prs) else 0)
