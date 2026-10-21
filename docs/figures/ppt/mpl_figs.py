#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mpl_figs.py — 第 3 章数据类插图（matplotlib）

版式规范（逐条可核对）：
  1. 画布宽度 5.77 in = 正文栏宽（Word 里按 14.66 cm 插入，1:1 显示）。
     字号：刻度 9.5 pt、数值 9.5 pt、轴标签 10 pt —— 视觉上比正文（12 pt）小一号，
     与图注（10.5 pt）齐平。
  2. 图内只保留三类元素：坐标轴（含单位）、数据条、数值。图题、来源、结论性注释
     一律不放进图里，交给正文与题注。
  3. 条宽约为行距的 0.34–0.40（细条，不用粗块）；颜色只承载语义：
     SM2=深青、SM4=灰蓝、SM3=浅灰蓝、REF=灰蓝、优化后/选中档=深青、余量=极浅灰。
  4. 生成后做两类几何检查，任一不通过即退出码 1：
     a) 文字包围盒两两相交；b) 文字压住色块边缘（既非完全在内、也非完全在外）。
     文字用"墨迹框"（按字号扣掉 em 上下留白）判定，避免字体度量造成假阳性。
"""
import os

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.transforms import Bbox

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'png')
PDF = os.path.join(HERE, 'pdf')

INK = '#2E454B'
TEXT = '#4C5F66'
TEAL = '#5E8F9C'
SLATE = '#8BA3B2'
PALE = '#C5D3D8'
GHOST = '#E6ECEE'
RULE = '#DDE5E7'
BASE = '#B7C6CA'
LIMIT = '#7F959D'

FS_TICK = 9.5
FS_VAL = 10.0
FS_LAB = 10.5

plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': ['Microsoft YaHei'],
    'axes.unicode_minus': False,
    'font.size': FS_TICK,
    'axes.edgecolor': BASE,
    'axes.labelcolor': TEXT,
    'xtick.color': TEXT,
    'ytick.color': TEXT,
    'text.color': INK,
    'figure.facecolor': 'white',
    'savefig.facecolor': 'white',
    'axes.linewidth': 0.7,
})


def style(ax, grid='x'):
    for s in ('top', 'right'):
        ax.spines[s].set_visible(False)
    for s in ('left', 'bottom'):
        ax.spines[s].set_color(BASE)
        ax.spines[s].set_linewidth(0.7)
    ax.tick_params(length=2.6, width=0.6, labelsize=FS_TICK, pad=2.5)
    ax.grid(axis=grid, color=RULE, linewidth=0.55, zorder=0)
    ax.set_axisbelow(True)


def ink_box(text, renderer):
    """文字的墨迹框：按字号扣掉 em 框的上下留白与左右边距。"""
    bb = text.get_window_extent(renderer)
    pt = text.get_fontsize()
    px = pt * 100.0 / 72.0          # 检查在默认 100 dpi 下进行
    return Bbox.from_extents(bb.x0 + 0.08 * px, bb.y0 + 0.22 * px,
                             bb.x1 - 0.08 * px, bb.y1 - 0.22 * px)


def check_text_text(fig):
    """文字墨迹框两两相交（同类刻度标签之间除外）。"""
    fig.canvas.draw()
    renderer = fig.canvas.get_renderer()
    items = []

    def collect(artists, tag):
        for t in artists:
            s = t.get_text().strip()
            if not s:
                continue
            bb = ink_box(t, renderer)
            if bb.width < 1 or bb.height < 1:
                continue
            items.append((s, bb, tag))

    for ax in fig.axes:
        collect(ax.texts, 'text')
        collect(ax.get_xticklabels(), 'xtick')
        collect(ax.get_yticklabels(), 'ytick')
        if ax.get_xlabel():
            collect([ax.xaxis.label], 'xlabel')
        if ax.get_ylabel():
            collect([ax.yaxis.label], 'ylabel')

    bad = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            a, b = items[i], items[j]
            if a[1].overlaps(b[1]):
                bad.append((a[0], b[0]))
    return bad


def check_text_bar(fig):
    """文字压色块边缘：文字必须完全落在色块内，或完全落在色块外。"""
    fig.canvas.draw()
    renderer = fig.canvas.get_renderer()
    bad = []
    for ax in fig.axes:
        patches = [(p, p.get_window_extent(renderer))
                   for p in ax.patches if p.get_width() > 0]
        for t in ax.texts:
            s = t.get_text().strip()
            if not s:
                continue
            tb = ink_box(t, renderer)
            for p, pb in patches:
                if not tb.overlaps(pb):
                    continue
                inside = (pb.x0 <= tb.x0 and tb.x1 <= pb.x1 and
                          pb.y0 <= tb.y0 and tb.y1 <= pb.y1)
                if not inside:
                    bad.append('%r 跨色块边缘' % s)
    return bad


def save(fig, name):
    bad = check_text_text(fig) + check_text_bar(fig)
    os.makedirs(PDF, exist_ok=True)
    p = os.path.join(OUT, name + '.png')
    fig.savefig(p, dpi=300, bbox_inches='tight', pad_inches=0.04)
    fig.savefig(os.path.join(PDF, name + '.pdf'), bbox_inches='tight',
                pad_inches=0.04)
    plt.close(fig)
    print('  → %s（几何问题 %d 处%s）' % (
        os.path.relpath(p, HERE), len(bad),
        '：' + str(bad[:4]) if bad else ''))
    return not bad


# ────────────────────────────────────────────── 共用：竖向柱
def vbars(ax, labels, vals, colors, log=False, ymax=None, fmt='%.2f',
          ylabel=None, xlabel=None, width=0.40):
    x = np.arange(len(vals))
    ax.bar(x, vals, width=width, color=colors, edgecolor='none', zorder=2)
    for xx, v in zip(x, vals):
        yy = v * 1.13 if log else v + (ymax or max(vals)) * 0.035
        ax.text(xx, yy, fmt % v, ha='center', va='bottom',
                fontsize=FS_VAL, color=TEXT)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=FS_TICK)
    ax.set_xlim(-0.55, len(vals) - 0.45)
    if log:
        ax.set_yscale('log')
        ax.set_ylim(20, ymax or max(vals) * 1.5)
    else:
        ax.set_ylim(0, ymax)
    if ylabel:
        ax.set_ylabel(ylabel, fontsize=FS_LAB, labelpad=2.5)
    if xlabel:
        ax.set_xlabel(xlabel, fontsize=FS_LAB, labelpad=2)
    style(ax, 'y')


# ────────────────────────────────────────────── 图 3-1 F103 算力
def fig31():
    sm2 = ['密钥生成', '签名', '验签', 'ECDH']
    sv = [5.44, 4.07, 3.24, 2.15]
    ker = ['REF', 'FAST', 'TINY', 'MCU']
    kv = [90.2, 106.6, 100.6, 76.5]

    fig, axes = plt.subplots(1, 2, figsize=(5.77, 2.55),
                             gridspec_kw={'width_ratios': [1.0, 1.0]})
    vbars(axes[0], sm2, sv, [TEAL] * 4, ymax=6.4, fmt='%.2f×',
          ylabel='加速比（REF ÷ MCU 档）', xlabel='SM2 操作')
    vbars(axes[1], ker, kv, [SLATE, PALE, PALE, TEAL], ymax=125, fmt='%.0f',
          ylabel='千 cycles（1 KB）', xlabel='SM3 内核')
    fig.tight_layout(w_pad=2.0)
    return save(fig, '图3-1-F103真机算力')


# ────────────────────────────────────────────── 图 3-2 四模式
def fig32():
    modes = ['FULL', 'RESUME', 'PSK', 'PSK' + chr(10) + '0-RTT']
    ms = [2274.97, 58.04, 74.77, 92.65]
    wire = [1198, 428, 412, 472]
    fig, axes = plt.subplots(1, 2, figsize=(5.77, 2.55),
                             gridspec_kw={'width_ratios': [1.0, 1.0]})
    vbars(axes[0], modes, ms, [TEAL, SLATE, SLATE, SLATE], log=True,
          ymax=3600, fmt='%.2f', ylabel='时延（ms，对数）')
    vbars(axes[1], modes, wire, [PALE] * 4, ymax=1500, fmt='%d',
          ylabel='握手线上字节（B）')
    fig.tight_layout(w_pad=2.0)
    return save(fig, '图3-2-真机四模式')


# ────────────────────────────────────────────── 图 3-3 资源预算
def fig33():
    ram = [('上下文', 5248, TEAL),
           ('静态区', 4104, SLATE),
           ('最长栈', 4872, PALE),
           ('余量', 6256, GHOST)]
    flash = [('固件', 29720, TEAL),
             ('余量', 32768 - 29720, GHOST)]

    fig, axes = plt.subplots(1, 2, figsize=(5.77, 2.35),
                             gridspec_kw={'width_ratios': [1.45, 1.0]})

    for ax, rows, cap, label in ((axes[0], ram, 20480, 'RAM（B）'),
                                 (axes[1], flash, 32768, 'Flash（B）')):
        vals = [r[1] for r in rows]
        cols = [r[2] for r in rows]
        vbars(ax, [r[0] for r in rows], vals, cols, ymax=cap * 1.20,
              fmt='%s', ylabel=label, width=0.34 if len(rows) > 2 else 0.24)
        ax.set_yticks([0, cap // 2, cap])
        ax.set_yticklabels(['0', str(cap // 2), str(cap)])
        # 不用千分位逗号（报告统一要求数字直接展示）

    fig.tight_layout(w_pad=1.8)
    return save(fig, '图3-3-资源预算')


# ────────────────────────────────────────────── 图 3-7 F103 板端功耗（实测）
def fig34():
    rows = ['FULL（REF）', 'FULL（MCU）', 'RESUME', 'PSK']
    vals = [18857, 5708, 66, 66]
    colors = [SLATE, TEAL, PALE, PALE]
    fig, ax = plt.subplots(figsize=(5.77, 2.20))
    vbars(ax, rows, vals, colors, log=True, ymax=26000, fmt='%d',
          ylabel='能耗（µJ，对数）', width=0.24)
    fig.tight_layout()
    return save(fig, '图3-7-F103功耗')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    ok = True
    for fn in (fig31, fig32, fig33, fig34):
        print('[%s]' % fn.__name__)
        ok = fn() and ok
    raise SystemExit(0 if ok else 1)
