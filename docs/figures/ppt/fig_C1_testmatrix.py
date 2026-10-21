#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 3-1　测试体系（论文式：轴线 + 节点，无卡片）"""
from figlib import *

prs = new_deck()
s = plate(prs)

LAYERS = [
    ('标准向量与交叉验证', 'SM3／SM4／HMAC 对 OpenSSL 3.2.4；SM2 用曲线与阶的性质自检'),
    ('跨内核差分等价', '7 个内核变体跑同一输入集，SM3 指纹逐位一致'),
    ('协议端到端与攻击负向', '三模式＋0-RTT＋双套件；六类负向用例全部拒绝'),
    ('资源门禁', '32 KiB Flash 链接证明；F103 20 KiB RAM 预算'),
    ('外部基线与平台复现', 'GmSSL 交错测量；DTLCP 双档各 36 项；CI 双平台'),
]
X = 1.30                      # 主轴
Y0, STEP = 1.30, 1.20
vrule(s, X, Y0 - 0.42, Y0 + 4 * STEP + 0.72, color=HAIR, lw=1.0)
for i, (title, detail) in enumerate(LAYERS):
    y = Y0 + i * STEP
    shape(s, MSO_SHAPE.OVAL, X - 0.13, y - 0.13, 0.26, 0.26, fill=PAPER,
          line=hue('teal')[0], lw=1.75)
    label(s, X - 0.42, y - 0.50, 0.84, '%02d' % (i + 1), 12, hue('teal')[0], True,
          PP_ALIGN.CENTER, 0.30)
    block(s, X + 0.34, y - 0.44, 8.10, 0.40, [(title, 16, INK, True)],
          MSO_ANCHOR.MIDDLE)
    label(s, X + 0.34, y + 0.02, 8.60, detail, 12, MUTED, False, PP_ALIGN.LEFT, 0.34)
    # 右侧通过标记：细对勾 + 文字（不用色块）
    label(s, 9.30, y - 0.26, 1.30, '✓ 通过', 12, hue('green')[0], False,
          PP_ALIGN.LEFT, 0.24)
    if i < len(LAYERS) - 1:
        hrule(s, X + 0.34, y + 0.58, 10.4, color=RULE, lw=0.75)

save(prs, '图3-1-测试体系与证据链.pptx')
sys.exit(1 if qa(prs) else 0)
