#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fig_ch2.py — 第 2 章插图

风格：参考用户样例 —— 线形图标为主体、粗箭头、虚线分组框、白底、单一蓝色强调。
画布宽度 6.65 in（= 正文栏宽 16.9 cm），图内字号即纸上字号。

图 2-2 多档内核与差分验证
图 2-3 SM3 与 SM4 内核优化要点
图 2-4 SM2 域运算与标量乘分流
图 2-5 Lim-Lee comb 预计算表
图 2-6 常量时间与预计算表的共存
图 2-7 DTLCP 记录层与密钥调度
图 2-9 软件工作流程与嵌入式移植
图 2-10 优化效果与外部基线对照（matplotlib，见 fig_ch2_data.py）
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import figlib as F
from pptx.oxml.ns import nsdecls
from pptx.oxml import parse_xml

F.FS = 1.0

INK = '1F2933'
ACCENT = '2C5F8A'
MUTED = '6B7B85'
RULE = 'C9D2D8'
WHITE = 'FFFFFF'


def canvas(w, h):
    prs = F.new_deck()
    prs.slide_width, prs.slide_height = F.Inches(w), F.Inches(h)
    s = prs.slides.add_slide(prs.slide_layouts[6])
    s.background.fill.solid()
    s.background.fill.fore_color.rgb = F.RGBColor.from_string(WHITE)
    return prs, s


def dashed_box(s, x, y, w, h, radius=0.10, color=INK, lw=1.0):
    shp = F.shape(s, F.MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h,
                  fill=None, line=color, lw=lw, radius=radius)
    ln = shp.line._get_or_add_ln()
    ln.append(parse_xml('<a:prstDash %s val="sysDash"/>' % nsdecls('a')))
    return shp


def group(s, x, y, w, h, title):
    dashed_box(s, x, y, w, h)
    F.label(s, x, y + 0.06, w, title, 9.0, INK, True, F.PP_ALIGN.CENTER, 0.24)


def node(s, cx, y, icon, title, sub='', isize=0.32, tsize=8.2, ssize=6.8, w=1.0):
    F.icon(s, icon, cx - isize / 2, y, isize, 'blue')
    lines = [(title, tsize, INK, True)]
    if sub:
        lines.append((sub, ssize, MUTED, False))
    F.block(s, cx - w / 2, y + isize + 0.04, w, 0.34, lines,
            F.MSO_ANCHOR.TOP, F.PP_ALIGN.CENTER)


def note(s, x, y, w, text, size=8.2):
    F.label(s, x, y, w, text, size, ACCENT, True, F.PP_ALIGN.CENTER, 0.22)


# ─────────────────────────────────────────── 图 2-2
def fig22(out):
    prs, s = canvas(6.65, 3.45)

    F.icon(s, 'target', 0.22, 0.55, 0.64, 'blue')
    F.label(s, 0.08, 1.24, 1.18, '固定输入集', 9.6, INK, True,
            F.PP_ALIGN.CENTER, 0.24)
    F.label(s, 0.08, 1.46, 1.18, '4 个操作', 8.0, MUTED, False,
            F.PP_ALIGN.CENTER, 0.22)

    dashed_box(s, 1.26, 0.24, 3.18, 2.06)
    F.label(s, 1.26, 0.30, 3.18, '6 个内核变体', 9.6, INK, True,
            F.PP_ALIGN.CENTER, 0.24)
    kers = [('ref', '基线', 'book-open'), ('opt', '优化', 'zap'),
            ('tiny', '最小内存', 'memory-stick'),
            ('notab', '关闭基点表', 'sliders-horizontal'),
            ('ct', '常量时间', 'lock'), ('lite', '功能裁剪', 'scan')]
    for i, (name, role, ic) in enumerate(kers):
        cx = [1.95, 3.45][i % 2]
        cy = [0.62, 1.18, 1.74][i // 2]
        F.icon(s, ic, cx - 0.62, cy, 0.38, 'blue')
        tb = F.textbox(s, cx - 0.18, cy - 0.01, 1.10, 0.40,
                       F.MSO_ANCHOR.MIDDLE)
        F.para(tb.text_frame, True,
               [(name + '  ', 8.4, INK, True), (role, 7.6, MUTED, False)])

    F.icon(s, 'fingerprint', 4.72, 0.55, 0.64, 'blue')
    F.icon(s, 'circle-check', 5.30, 0.55, 0.24, 'green')
    F.label(s, 4.55, 1.22, 1.85, 'SM3 差分指纹', 9.6, INK, True,
            F.PP_ALIGN.CENTER, 0.24)
    F.label(s, 4.55, 1.44, 1.85, '6 变体逐位一致', 8.0, ACCENT, False,
            F.PP_ALIGN.CENTER, 0.22)
    F.label(s, 4.55, 1.66, 1.85, '任一内核算错，指纹即改变', 7.4, MUTED, False,
            F.PP_ALIGN.CENTER, 0.22)

    F.arrow(s, 0.98, 1.05, 1.22, 1.05, color=INK, lw=3.0)
    F.arrow(s, 4.50, 1.05, 4.68, 1.05, color=INK, lw=3.0)

    dashed_box(s, 0.22, 2.56, 6.20, 0.76, radius=0.08)
    F.label(s, 0.40, 2.62, 1.40, '判定口径', 8.8, INK, True,
            F.PP_ALIGN.LEFT, 0.22)
    items = [('target', '同一输入集', '覆盖 4 个操作', 0.40),
             ('list-checks', '逐位比较', '256 位摘要', 2.55),
             ('circle-alert', '算错即变', '自动判据', 4.75)]
    for ic, t1, t2, x in items:
        F.icon(s, ic, x, 2.88, 0.22, 'blue')
        F.block(s, x + 0.29, 2.84, 1.55, 0.40,
                [(t1, 8.2, INK, True), (t2, 7.4, MUTED, False)])

    F.save(prs, out)
    return out


# ─────────────────────────────────────────── 图 2-3
def fig23(out):
    prs, s = canvas(6.65, 2.08)
    group(s, 0.18, 0.22, 3.10, 1.74, 'SM3 内核')
    for cx, ic, t, sub in [(0.73, 'book-open', 'REF', '完整扩展'),
                           (1.73, 'zap', 'FAST', '交织 + 展开'),
                           (2.73, 'memory-stick', 'TINY', '环形调度')]:
        node(s, cx, 0.64, ic, t, sub, isize=0.48, w=0.94)
    F.arrow(s, 1.02, 0.88, 1.32, 0.88, color=INK, lw=2.4)
    F.arrow(s, 2.02, 0.88, 2.32, 0.88, color=INK, lw=2.4)
    note(s, 0.18, 1.48, 3.10, 'TINY 单函数栈 64 B')

    group(s, 3.45, 0.22, 3.02, 1.74, 'SM4 内核')
    for cx, ic, t, sub in [(4.00, 'book-open', 'REF', '逐字节 τ/L'),
                           (4.96, 'zap', 'T-table', '4 KB 表'),
                           (5.92, 'memory-stick', 'SMALL', '256 B S-box')]:
        node(s, cx, 0.64, ic, t, sub, isize=0.48, w=0.92)
    F.arrow(s, 4.29, 0.88, 4.59, 0.88, color=INK, lw=2.4)
    F.arrow(s, 5.25, 0.88, 5.55, 0.88, color=INK, lw=2.4)
    note(s, 3.45, 1.48, 3.02, '单块 2.03× · CBC/CTR 1.73×')
    F.save(prs, out)
    return out


# ─────────────────────────────────────────── 图 2-4
def fig24(out):
    prs, s = canvas(6.65, 2.38)
    group(s, 0.18, 0.22, 3.10, 1.92, '域运算')
    F.icon(s, 'sigma', 0.40, 0.56, 0.32, 'blue')
    F.block(s, 0.80, 0.58, 2.32, 0.34,
            [('p = 2²⁵⁶ − 2²²⁴ − 2⁹⁶ + 2⁶⁴ − 1', 7.6, INK, False)])
    node(s, 0.85, 0.94, 'function-square', '试商长除法', '', isize=0.42, w=1.0)
    node(s, 2.35, 0.94, 'spline', 'Solinas 折叠', '迭代清零', isize=0.42, w=1.1)
    F.arrow(s, 1.35, 1.16, 1.75, 1.16, color=INK, lw=2.4)
    F.label(s, 0.18, 1.72, 3.10, '求逆：费马小定理 · 4 位窗口',
            7.8, MUTED, False, F.PP_ALIGN.CENTER, 0.22)

    group(s, 3.45, 0.22, 3.02, 1.92, '标量乘')
    for cx, ic, t, sub in [(4.05, 'table-2', 'k·G', 'comb 表'),
                           (4.95, 'function-square', 'k·P', 'wNAF（w = 4）'),
                           (5.85, 'workflow', 'sG + tP', '交织双标量乘')]:
        node(s, cx, 0.90, ic, t, sub, isize=0.42, w=0.84)
    F.arrow(s, 4.32, 1.12, 4.55, 1.12, color=INK, lw=2.4)
    F.arrow(s, 5.22, 1.12, 5.45, 1.12, color=INK, lw=2.4)
    F.label(s, 3.45, 1.74, 3.02, '两条腿共享一个倍点循环', 7.4, MUTED, False,
            F.PP_ALIGN.CENTER, 0.22)
    F.save(prs, out)
    return out


# ─────────────────────────────────────────── 图 2-5
def fig25(out):
    prs, s = canvas(6.65, 3.12)
    group(s, 0.18, 0.30, 6.29, 1.55, '表的结构')
    F.label(s, 3.10, 0.36, 3.20, 't = 5 · d = 52 · 32 项', 7.0, MUTED,
            False, F.PP_ALIGN.RIGHT, 0.22)
    nodes = [(0.72, 'binary', '256 位标量', '私钥或 nonce'),
             (2.10, 'grid-2x2', '52 个窗口', '每窗 5 位 · K_j'),
             (3.55, 'table-2', '32 项表', '静态仿射点 T[α]'),
             (5.05, 'spline', 'k·G', '51 倍点 + ≤51 混合加')]
    for cx, ic, t, sub in nodes:
        F.icon(s, ic, cx - 0.22, 0.68, 0.44, 'blue')
        F.block(s, cx - 0.62, 1.16, 1.24, 0.40,
                [(t, 7.6, INK, True), (sub, 6.8, MUTED, False)],
                F.MSO_ANCHOR.TOP, F.PP_ALIGN.CENTER)
    for x1, x2 in [(1.28, 1.55), (2.66, 2.95), (4.08, 4.38)]:
        F.arrow(s, x1, 0.90, x2, 0.90, color=INK, lw=2.6)
    F.label(s, 0.18, 1.62, 6.29, '表存 Flash · 静态 RAM 增量 0',
            7.0, MUTED, False, F.PP_ALIGN.CENTER, 0.20)

    group(s, 0.18, 1.95, 6.29, 1.07, '生成与校验')
    F.label(s, 3.10, 2.01, 3.20, '不自我循环论证', 7.0, MUTED, False,
            F.PP_ALIGN.RIGHT, 0.22)
    steps = [(0.75, 'book-open', '基元倍点', 'B_i = 2^{i·d}·G'),
             (2.15, 'refresh-cw', '批量求逆', '1 次 Montgomery'),
             (3.55, 'shield-check', '16 标量自检', '与基线逐位比对'),
             (4.95, 'list-checks', '--check + CI', '表与生成器一致')]
    for cx, ic, t, sub in steps:
        F.icon(s, ic, cx - 0.19, 2.30, 0.38, 'blue')
        F.block(s, cx - 0.62, 2.72, 1.24, 0.36,
                [(t, 7.4, INK, True), (sub, 6.4, MUTED, False)],
                F.MSO_ANCHOR.TOP, F.PP_ALIGN.CENTER)
    for x1, x2 in [(1.22, 1.55), (2.62, 2.95), (4.02, 4.35)]:
        F.arrow(s, x1, 2.49, x2, 2.49, color=INK, lw=2.0)
    F.save(prs, out)
    return out

# ─────────────────────────────────────────── 图 2-6
def fig26(out):
    prs, s = canvas(6.65, 2.35)
    group(s, 0.18, 0.30, 3.95, 1.80, '恒定工作量的做法')
    nodes = [(0.65, 'table-2', '32 项表', '静态仿射表'),
             (1.65, 'boxes', '逐项相加', '无提前返回'),
             (2.65, 'eye-off', '掩码选择', '按标量位采纳'),
             (3.55, 'key-round', 'k·G', '工作量恒定')]
    for cx, ic, t, sub in nodes:
        F.icon(s, ic, cx - 0.20, 0.66, 0.40, 'blue')
        F.block(s, cx - 0.50, 1.10, 1.00, 0.44,
                [(t, 7.6, INK, True), (sub, 6.8, MUTED, False)],
                F.MSO_ANCHOR.TOP, F.PP_ALIGN.CENTER)
    for x1, x2 in [(1.00, 1.25), (2.00, 2.25), (3.00, 3.20)]:
        F.arrow(s, x1, 0.86, x2, 0.86, color=INK, lw=2.6)
    F.label(s, 0.18, 1.62, 3.95, '访存模式 · 运算次数 · 与秘密无关',
            7.0, MUTED, False, F.PP_ALIGN.CENTER, 0.20)

    group(s, 4.30, 0.30, 2.17, 1.80, '代价')
    rows = [(4.55, 'circle-alert', '放弃查表', '2.8× 代价'),
            (4.55, 'gauge', '全表 + cmov', '1.13× / 1.18×'),
            (4.55, 'badge-check', '4,096 标量', '极差 0')]
    yy = 0.70
    for x, ic, t, v in rows:
        F.icon(s, ic, x, yy - 0.06, 0.32, 'blue')
        F.block(s, x + 0.36, yy - 0.05, 1.00, 0.22, [(t, 7.6, INK, True)])
        F.block(s, x + 0.36, yy + 0.14, 1.45, 0.22, [(v, 8.0, ACCENT, True)])
        yy += 0.50
    F.save(prs, out)
    return out


# ─────────────────────────────────────────── 图 2-7
def fig27(out):
    prs, s = canvas(6.65, 3.10)
    group(s, 0.18, 0.30, 6.29, 1.36, '记录层格式')
    x0, y0, W, H = 0.42, 0.62, 5.80, 0.26
    segs = [('头部 13 B', 13, 'DCE7F1'), ('IV 16 B', 16, 'EDF3F8'),
            ('载荷（密文）', 10, 'FFFFFF'), ('MAC 32 B', 32, 'DCE7F1')]
    tot = sum(w for _, w, _ in segs)
    x = x0
    for label, w, fill in segs:
        sw = W * w / tot
        F.shape(s, F.MSO_SHAPE.RECTANGLE, x, y0, sw, H, fill=fill,
                line='9FB0BC', lw=0.5)
        F.label(s, x, y0 + 0.01, sw, label, 7.2, INK, True,
                F.PP_ALIGN.CENTER, H - 0.02)
        x += sw
    F.label(s, 0.42, 0.96, 5.8,
            '头部：类型 · 版本 · epoch · 序列号 · 长度',
            7.2, MUTED, False, F.PP_ALIGN.CENTER, 0.20)
    F.label(s, 0.42, 1.16, 5.8,
            'IV：HMAC-SM3 派生 · MAC：覆盖头部与密文',
            7.2, MUTED, False, F.PP_ALIGN.CENTER, 0.20)

    group(s, 0.18, 1.72, 6.29, 1.26, '密钥调度')
    ks = [(0.72, 'handshake', 'PMS', 'ECDHE / PSK'),
          (2.20, 'hash', 'PRF', 'P_hash(HMAC-SM3)'),
          (3.85, 'key-round', 'master_secret', '48 B'),
          (5.35, 'lock', '密钥与 IV', 'MAC / data / IV')]
    for cx, ic, t, sub in ks:
        node(s, cx, 2.00, ic, t, sub, isize=0.36, tsize=7.8, ssize=6.6, w=1.15)
    for x1, x2 in [(1.38, 1.70), (2.86, 3.20), (4.52, 4.90)]:
        F.arrow(s, x1, 2.18, x2, 2.18, color=INK, lw=2.4)
    F.label(s, 0.18, 2.66, 6.29, 'PRF 输入：PMS · client_random · server_random',
            7.0, MUTED, False, F.PP_ALIGN.CENTER, 0.20)
    F.save(prs, out)
    return out


# ─────────────────────────────────────────── 图 2-9
def fig29(out):
    prs, s = canvas(6.65, 3.05)
    group(s, 0.18, 0.30, 6.29, 1.34, '软件流程')
    steps = [(0.68, 'plug', '上电', '复位启动'),
             (1.72, 'refresh-cw', '初始化', '时钟 · 串口 · 熵源'),
             (2.78, 'shield-check', '自检', '标准向量比对'),
             (3.84, 'gauge', '微基准', 'DWT 周期数'),
             (4.95, 'handshake', 'DTLCP', 'FULL / RESUME / PSK'),
             (6.02, 'radio', '应用', '遥测 · ACK')]
    for cx, ic, t, sub in steps:
        F.icon(s, ic, cx - 0.20, 0.58, 0.40, 'blue')
        F.block(s, cx - 0.52, 1.02, 1.04, 0.44,
                [(t, 7.6, INK, True), (sub, 6.6, MUTED, False)],
                F.MSO_ANCHOR.TOP, F.PP_ALIGN.CENTER)
    for x1, x2 in [(1.02, 1.34), (2.06, 2.40), (3.12, 3.46),
                   (4.18, 4.57), (5.29, 5.64)]:
        F.arrow(s, x1, 0.78, x2, 0.78, color=INK, lw=2.6)

    group(s, 0.18, 1.78, 6.29, 1.18, '平台层 6 类钩子')
    hooks = [(0.68, 'sparkles', '熵源', 'TRNG · 外部熵'),
             (1.72, 'activity', '周期计数', 'DWT CYCCNT'),
             (2.78, 'gauge', '主频', '频率查询'),
             (3.84, 'shield-check', '看门狗', '喂狗'),
             (4.95, 'timer', '延时', 'ms / us'),
             (6.02, 'refresh-cw', '初始化', '时钟 · 外设')]
    for cx, ic, t, sub in hooks:
        F.icon(s, ic, cx - 0.18, 2.04, 0.36, 'blue')
        F.block(s, cx - 0.52, 2.44, 1.04, 0.44,
                [(t, 7.4, INK, True), (sub, 6.6, MUTED, False)],
                F.MSO_ANCHOR.TOP, F.PP_ALIGN.CENTER)
    F.label(s, 0.18, 2.70, 6.29, '6 类函数 · 全部移植面',
            7.0, MUTED, False, F.PP_ALIGN.CENTER, 0.20)
    F.save(prs, out)
    return out


BUILDERS = {
    '2-2': fig22, '2-3': fig23, '2-4': fig24, '2-5': fig25,
    '2-6': fig26, '2-7': fig27, '2-9': fig29,
}

TITLES = {
    '2-2': '多档内核与差分验证', '2-3': 'SM3与SM4内核优化',
    '2-4': 'SM2域运算与标量乘', '2-5': 'comb预计算表',
    '2-6': '常量时间与预计算表', '2-7': '记录层与密钥调度',
    '2-9': '软件流程与移植',
}


HAND_EDITED = {'2-2', '2-3', '2-4'}   # 用户已在 PowerPoint 中手工压缩版式

if __name__ == '__main__':
    names = sys.argv[1:] or list(BUILDERS)
    for key in names:
        if key in HAND_EDITED and '--force' not in sys.argv:
            print('跳过 %s：该图已手工调整，避免覆盖' % key)
            continue
        print(BUILDERS[key]('图%s-%s.pptx' % (key, TITLES[key])))
