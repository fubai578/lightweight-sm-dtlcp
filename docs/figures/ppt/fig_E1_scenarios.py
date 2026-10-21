#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 4-1　典型应用场景与配置选配（报告第 4 章）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '典型应用场景与配置选配',
              '共同特征：长期电池供电 + 国密合规要求 + 周期性小报文上行',
              huename='green')

# ── 四类场景
CW, GAP, Y0, CH = 3.04, 0.09, 1.32, 2.30
SCN = [
    ('gauge', 'blue', '智能表计', '周期小报文'),
    ('sprout', 'green', '环境与管网监测', '环境不可控'),
    ('radio', 'purple', '资产追踪／墒情', '超低占空比'),
    ('shield-check', 'rose', '安全敏感场合', '侧信道威胁'),
]
for i, (ic, hn, t, d) in enumerate(SCN):
    x = M + i * (CW + GAP)
    panel(s, x, Y0, CW, CH, hn)
    deep, tint, mid = hue(hn)
    shape(s, MSO_SHAPE.OVAL, x + CW / 2 - 0.62, Y0 + 0.22, 1.24, 1.24,
          fill=PAPER, line=deep if i == 3 else mid, lw=2.0 if i == 3 else 1.25)
    icon(s, ic, x + CW / 2 - 0.38, Y0 + 0.46, 0.76, hn)
    label(s, x + 0.14, Y0 + 1.56, CW - 0.28, t, 17, deep, True, h=0.32)
    label(s, x + 0.14, Y0 + 1.90, CW - 0.28, d, 12.5, TEXT, False, h=0.30)

# ── 三套配置
Y1, H1 = 3.86, 1.62
CFG = [
    ('zap', 'blue', '性能优先', [('优化内核 + 基点表', 12.5, TEXT, False),
                                 ('29824 B / 4128 B', 17, hue('blue')[0], True)]),
    ('database', 'green', '内存优先', [('SMALL + TINY 内核', 12.5, TEXT, False),
                                       ('20624 B / 0 B', 17, hue('green')[0], True)]),
    ('lock', 'amber', '抗侧信道优先', [('常量时间构建', 12.5, TEXT, False),
                                       ('运算次数极差 0', 17, hue('amber')[0], True)]),
]
for i, (ic, hn, t, lines) in enumerate(CFG):
    x = M + i * (4.13)
    panel(s, x, Y1, 3.95, H1, hn)
    icon(s, ic, x + 0.20, Y1 + 0.20, 0.56, hn)
    block(s, x + 0.90, Y1 + 0.20, 3.95 - 1.10, 0.56, [(t, 18, hue(hn)[0], True)],
          MSO_ANCHOR.MIDDLE)
    block(s, x + 0.24, Y1 + 0.70, 3.95 - 0.48, 0.68, lines, MSO_ANCHOR.MIDDLE)
label(s, M, Y1 + H1 + 0.16, BODY_W,
      '编译开关决定配置而非代码分支：同一份源码服务不同档位的终端',
      13, TEXT, False, PP_ALIGN.LEFT, 0.36)

# 题注由报告正文（Word 题注）负责，图内不再嵌图号与数据来源

save(prs, '图4-1-应用场景与选配.pptx')
sys.exit(1 if qa(prs) else 0)
