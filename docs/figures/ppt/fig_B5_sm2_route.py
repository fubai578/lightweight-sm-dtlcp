#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-4　SM2 域运算与标量乘分流（报告 2.1.6）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, 'SM2 的两条优化线',
              '开销集中在有限域运算与标量乘；点运算统一在 Jacobian 坐标下实现',
              huename='amber')

CX, CY, R = 1.60, 3.85, 1.02
shape(s, MSO_SHAPE.OVAL, CX - R, CY - R, 2 * R, 2 * R, fill=hue('amber')[1],
      line=hue('amber')[0], lw=2.5)
icon(s, 'spline', CX - 0.46, CY - 0.72, 0.92, 'amber')
label(s, CX - R, CY + 0.32, 2 * R, 'SM2', 26, hue('amber')[0], True, h=0.56)

PW, PH = 4.30, 2.20
X1, X2 = 3.36, 7.94
Y1, Y2 = 1.50, 3.80

# 域运算
panel(s, X1, Y1, PW, PH, 'rose')
icon(s, 'function-square', X1 + 0.18, Y1 + 0.16, 0.54, 'rose')
block(s, X1 + 0.86, Y1 + 0.16, PW - 1.05, 0.62, [('域运算：约减方式', 15, hue('rose')[0], True)],
      MSO_ANCHOR.MIDDLE)
chip(s, X1 + 0.24, Y1 + 1.06, 1.62, 0.56, '试商除法', 'rose', 14, on_dark=False)
arrow(s, X1 + 1.96, Y1 + 1.34, X1 + 2.34, Y1 + 1.34, 'rose', 3.0)
chip(s, X1 + 2.44, Y1 + 1.06, 1.62, 0.56, '折叠约减', 'rose', 14)

# 分流判据
panel(s, X2, Y1, W - M - X2, PH, 'teal')
icon(s, 'target', X2 + 0.18, Y1 + 0.16, 0.54, 'teal')
block(s, X2 + 0.86, Y1 + 0.16, W - M - X2 - 1.05, 0.62,
      [('分流判据：基点是否固定', 15, hue('teal')[0], True)], MSO_ANCHOR.MIDDLE)
chip(s, X2 + 0.24, Y1 + 1.06, W - M - X2 - 0.48, 0.52, '固定基点 → 预计算表',
     'teal', 14, on_dark=False)
chip(s, X2 + 0.24, Y1 + 1.66, W - M - X2 - 0.48, 0.52, '变基点 → 优化加法密度',
     'teal', 14, on_dark=False)

# 标量乘
panel(s, X1, Y2, PW, 2.34, 'blue')
icon(s, 'activity', X1 + 0.18, Y2 + 0.16, 0.54, 'blue')
block(s, X1 + 0.86, Y2 + 0.16, PW - 1.05, 0.62, [('标量乘：四级策略', 15, hue('blue')[0], True)],
      MSO_ANCHOR.MIDDLE)
for i, (t, hot) in enumerate([('二进制倍点', False), ('wNAF w=4', True),
                              ('交织双标量', True), ('comb 表', True)]):
    cx = X1 + 0.24 + (i % 2) * 1.94
    cy = Y2 + 1.00 + (i // 2) * 0.60
    chip(s, cx, cy, 1.80, 0.52, t, 'blue', 14, on_dark=hot)

# 点运算
panel(s, X2, Y2, W - M - X2, 2.34, 'purple')
icon(s, 'sigma', X2 + 0.18, Y2 + 0.16, 0.54, 'purple')
block(s, X2 + 0.86, Y2 + 0.16, W - M - X2 - 1.05, 0.62,
      [('点运算：Jacobian 坐标', 15, hue('purple')[0], True)], MSO_ANCHOR.MIDDLE)
for i, t in enumerate(['a = −3 优化的倍点公式', '混合加法（Z₂ = 1）',
                       '无穷远与 P = ±Q 显式处理']):
    yy = Y2 + 0.96 + i * 0.46
    seg(s, X2 + 0.28, yy + 0.18, X2 + 0.44, yy + 0.18, color=hue('purple')[2], lw=2.5)
    label(s, X2 + 0.54, yy, W - M - X2 - 0.78, t, 13, TEXT, False, PP_ALIGN.LEFT, 0.36)

seg(s, CX + R - 0.12, CY - 0.60, X1 - 0.06, Y1 + PH / 2, color=hue('amber')[2], lw=2.0)
seg(s, CX + R - 0.12, CY + 0.30, X1 - 0.06, Y2 + PH / 2, color=hue('amber')[2], lw=2.0)

footer(s, '图 2-4　分流的依据是基点是否固定：固定基点可预计算，变基点只能优化窗口与加法密度。',
       '数据来源：docs/02、docs/04。')

save(prs, '图2-4-SM2优化路线.pptx')
sys.exit(1 if qa(prs) else 0)
