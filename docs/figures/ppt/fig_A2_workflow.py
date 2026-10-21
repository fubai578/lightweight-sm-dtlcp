#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-9　软件工作流程与嵌入式移植（报告 2.1.1 / 2.1.11）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '软件工作流程与移植面',
              '上电到通信的固定顺序；与 MCU 的耦合被收敛为六类平台钩子',
              huename='teal')

# ── 左：五步流程
LW = 6.30
panel(s, M, 1.50, LW, 4.56, 'teal')
icon(s, 'workflow', M + 0.20, 1.68, 0.56, 'teal')
block(s, M + 0.90, 1.68, LW - 1.10, 0.60, [('上电到通信：五步', 19, hue('teal')[0], True)],
      MSO_ANCHOR.MIDDLE)
STEPS = [('plug', '平台初始化'), ('clipboard-check', '算法自检'), ('gauge', '微基准'),
         ('handshake', 'DTLCP 握手'), ('layers', '记录层承载报文')]
for i, (ic, t) in enumerate(STEPS):
    yy = 2.52 + i * 0.68
    chip(s, M + 0.26, yy, 0.56, 0.56, str(i + 1), 'teal', 15)
    icon(s, ic, M + 1.02, yy + 0.02, 0.52, 'teal')
    label(s, M + 1.72, yy, LW - 1.96, t, 16, INK, True, PP_ALIGN.LEFT, 0.56)
    if i < len(STEPS) - 1:
        seg(s, M + 0.54, yy + 0.58, M + 0.54, yy + 0.66, color=hue('teal')[2],
            lw=2.5, head=True)

# ── 右：六类钩子
X2 = M + LW + 0.33
PW2 = BODY_W - LW - 0.33
panel(s, X2, 1.50, PW2, 4.56, 'amber')
icon(s, 'plug', X2 + 0.20, 1.68, 0.56, 'amber')
block(s, X2 + 0.90, 1.68, PW2 - 1.10, 0.60, [('六类平台钩子', 19, hue('amber')[0], True)],
      MSO_ANCHOR.MIDDLE)
HOOKS = [('zap', '熵源'), ('clock', '周期计数'), ('gauge', '主频'),
         ('shield-check', '看门狗'), ('timer', '延时'), ('refresh-cw', '初始化')]
HW, HH, HG = (PW2 - 0.48 - 0.40) / 3, 1.06, 0.18
for i, (ic, t) in enumerate(HOOKS):
    cx = X2 + 0.24 + (i % 3) * (HW + 0.20)
    cy = 2.80 + (i // 3) * (HH + HG)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, cx, cy, HW, HH, fill=PAPER,
          line=hue('amber')[2], lw=1.25, radius=0.18)
    icon(s, ic, cx + HW / 2 - 0.30, cy + 0.14, 0.60, 'amber')
    label(s, cx, cy + 0.72, HW, t, 14, hue('amber')[0], True, h=0.32)
label(s, X2 + 0.26, 2.30, PW2 - 0.52, '实现 6 个函数即可运行算法库与协议栈',
      13, TEXT, False, PP_ALIGN.LEFT, 0.40)
card(s, X2 + 0.24, 5.20, PW2 - 0.48, 0.62, None, [], huename='purple', tone='white')
icon(s, 'sliders-horizontal', X2 + 0.38, 5.32, 0.40, 'purple')
label(s, X2 + 0.88, 5.20, PW2 - 1.10, '可选：调试输出 · RNG · TRNG',
      12.5, TEXT, False, PP_ALIGN.LEFT, 0.62)

footer(s, '图 2-9　软件工作流程与嵌入式移植：仓库另附裸机工程骨架、'
          '不依赖 make 的构建脚本与宿主仿真预检。',
       '数据来源：docs/08、embedded/。', fs=0.95)

save(prs, '图2-9-软件工作流程与移植.pptx')
sys.exit(1 if qa(prs) else 0)
