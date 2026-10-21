#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 1-2　作品功能构成（报告 1.2）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '作品功能构成',
              '六个部分沿「算法层—协议层—应用层」衔接，另有平台层承接与 MCU 的耦合',
              huename='teal')

W3, G3, Y0, H3 = 3.95, 0.24, 1.52, 3.30
COLS = [
    ('green', 'hash', '算法库', [
        ('hash', 'SM3'), ('grid-2x2', 'SM4'), ('spline', 'SM2'), ('shield-check', 'DRBG')]),
    ('blue', 'handshake', '协议栈', [
        ('handshake', '握手'), ('layers', '记录层'), ('key-round', '密钥调度'), ('file-badge', '证书')]),
    ('teal', 'radio', '应用示例', [
        ('radio', '传感终端'), ('server', '网关'), ('refresh-cw', '会话恢复'), ('zap', '早期数据')]),
]
TW, TH2, TG = 0.86, 1.30, 0.03
for ci, (hn, hic, name, tiles) in enumerate(COLS):
    x = M + ci * (W3 + G3)
    deep, tint, mid = hue(hn)
    panel(s, x, Y0, W3, H3, hn)
    icon(s, hic, x + 0.18, Y0 + 0.18, 0.58, hn)
    block(s, x + 0.88, Y0 + 0.18, W3 - 1.02, 0.66, [(name, 20, deep, True)],
          MSO_ANCHOR.MIDDLE)
    seg(s, x + 0.16, Y0 + 0.96, x + W3 - 0.16, Y0 + 0.96, color=mid, lw=1.0)
    for ti, (ic, t) in enumerate(tiles):
        tx = x + 0.16 + ti * (TW + TG)
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, tx, Y0 + 1.10, TW, TH2,
              fill=PAPER, line=mid, lw=1.0, radius=0.12)
        icon(s, ic, tx + TW / 2 - 0.27, Y0 + 1.26, 0.54, hn)
        label(s, tx, Y0 + 1.96, TW, t, 14, deep, True, h=0.34)

Y1, H1 = 5.02, 1.26
SUP = [('plug', 'teal', '嵌入式支持'), ('gauge', 'amber', '评测工具'),
       ('clipboard-check', 'purple', '规范文档')]
for i, (ic, hn, t) in enumerate(SUP):
    x = M + i * (W3 + G3)
    card(s, x, Y1, W3, H1, t, None, huename=hn, icon_name=ic, icon_size=0.72,
         tsize=19, tone='white')

footer(s, '图 1-2　作品功能构成：算法库与协议栈共用同一套内核与裁剪开关。',
       '数据来源：仓库目录结构与 docs/01—03。')

save(prs, '图1-2-功能模块矩阵.pptx')
sys.exit(1 if qa(prs) else 0)
