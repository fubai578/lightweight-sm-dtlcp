#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-7　DTLCP 记录层与密钥调度（报告 2.1.10）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '记录层：一个记录 = 一个数据报',
              '显式长度字段定界；epoch 与 48 位序列号配合 64 位滑动窗口防重放',
              huename='blue')

# ── 报文格式
label(s, M, 1.44, 6.0, '记录格式', 15, INK, True, PP_ALIGN.LEFT, 0.40)
packet(s, M, 1.92, BODY_W, 1.06, [
    ('类型', '1 B', 1.0, 'blue'), ('版本', '2 B', 1.0, 'blue'),
    ('epoch', '2 B', 1.4, 'teal'), ('序列号', '6 B', 2.6, 'teal'),
    ('长度', '2 B', 1.0, 'blue'), ('IV', '16 B', 3.0, 'rose'),
    ('载荷', '可变长', 4.6, 'green'), ('MAC', '32 B', 2.6, 'purple')],
    size=13.5)

# ── 左：防重放窗口
Y0 = 3.30
panel(s, M, Y0, 6.05, 2.60, 'teal')
icon(s, 'shield-check', M + 0.20, Y0 + 0.18, 0.56, 'teal')
block(s, M + 0.90, Y0 + 0.18, 5.0, 0.60, [('防重放：64 位窗口', 19, hue('teal')[0], True)],
      MSO_ANCHOR.MIDDLE)
window_strip(s, M + 0.34, Y0 + 1.02, 14, 3, 5, 'teal', size=0.36, gap=0.06)
label(s, M + 0.34, Y0 + 1.56, 2.40, '已认证（可丢弃）', 12, MUTED, False, PP_ALIGN.LEFT, 0.32)
label(s, M + 3.14, Y0 + 1.56, 1.60, '窗口内', 12, hue('teal')[0], True, PP_ALIGN.LEFT, 0.32)
label(s, M + 0.34, Y0 + 1.98, 5.30, '窗口只在 MAC 校验通过后推进', 13, TEXT, False,
      PP_ALIGN.LEFT, 0.36)

# ── 右：密钥调度
X2, PW2 = M + 6.33, BODY_W - 6.33
panel(s, X2, Y0, PW2, 2.60, 'purple')
icon(s, 'key-round', X2 + 0.20, Y0 + 0.18, 0.56, 'purple')
block(s, X2 + 0.90, Y0 + 0.18, PW2 - 1.05, 0.60, [('密钥调度', 19, hue('purple')[0], True)],
      MSO_ANCHOR.MIDDLE)
chip(s, X2 + 0.24, Y0 + 1.02, 1.36, 0.52, '预主密钥', 'purple', 12.5, on_dark=False)
arrow(s, X2 + 1.66, Y0 + 1.28, X2 + 1.92, Y0 + 1.28, 'purple', 2.5)
chip(s, X2 + 1.98, Y0 + 1.02, 1.20, 0.52, '主密钥', 'purple', 12.5)
arrow(s, X2 + 3.24, Y0 + 1.28, X2 + 3.50, Y0 + 1.28, 'purple', 2.5)
label(s, X2 + 3.56, Y0 + 1.02, PW2 - 3.80, '读写密钥 · IV', 12.5,
      hue('purple')[0], True, PP_ALIGN.LEFT, 0.52)
label(s, X2 + 0.24, Y0 + 1.72, PW2 - 0.48, 'PRF = P_hash(HMAC-SM3)', 13, TEXT, False,
      PP_ALIGN.LEFT, 0.34)
label(s, X2 + 0.24, Y0 + 2.06, PW2 - 0.48, 'CCS 后首条 Finished 切新密钥', 13, TEXT,
      False, PP_ALIGN.LEFT, 0.36)

# ── 套件行
Y1 = 5.88
SUITE = [('handshake', 'blue', 'CBC 套件 · MAC-then-encrypt', False),
         ('zap', 'green', 'CTR 套件 · Encrypt-then-MAC', True),
         ('lock', 'amber', 'PSK 套件 · 0 次公钥运算', True)]
CW3 = (BODY_W - 2 * 0.22) / 3
for i, (ic, hn, d, hot) in enumerate(SUITE):
    x = M + i * (CW3 + 0.22)
    card(s, x, Y1, CW3, 0.92, None, [d], huename=hn, icon_name=ic, icon_size=0.56,
         lsize=13.5, tone='tint' if hot else 'white', accent=hot)

footer(s, '图 2-7　长度字段与防重放窗口共同构成记录层；轻量套件用 Encrypt-then-MAC。',
       '数据来源：dtlcp/、docs/02。', y=6.86, fs=0.9)

save(prs, '图2-7-记录层与密钥调度.pptx')
sys.exit(1 if qa(prs) else 0)
