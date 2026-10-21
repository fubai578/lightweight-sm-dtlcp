#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fig_ch2_data.py — 图 2-10 优化效果与外部基线对照（matplotlib，期刊风）"""
import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'png')
PDF = os.path.join(HERE, 'pdf')

INK = '#1F2933'
ACCENT = '#2C5F8A'
GRAY = '#A9B6BE'
MUTED = '#6B7B85'
RULE = '#E3E8EC'
BASE = '#9FB0BC'

plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': ['Microsoft YaHei'],
    'axes.unicode_minus': False,
    'font.size': 7.5,
    'axes.edgecolor': BASE,
    'axes.labelcolor': INK,
    'xtick.color': INK,
    'ytick.color': MUTED,
    'text.color': INK,
    'figure.facecolor': 'white',
    'savefig.facecolor': 'white',
    'axes.linewidth': 0.6,
})


def style(ax, ymax, ylabel):
    for s in ('top', 'right'):
        ax.spines[s].set_visible(False)
    ax.spines['left'].set_linewidth(0.6)
    ax.spines['bottom'].set_linewidth(0.6)
    ax.tick_params(length=2.2, width=0.5, labelsize=7.2, pad=2)
    ax.set_ylim(0, ymax)
    ax.set_ylabel(ylabel, fontsize=7.6, labelpad=2)
    ax.grid(axis='y', color=RULE, linewidth=0.5)
    ax.set_axisbelow(True)


def main():
    fig, axes = plt.subplots(1, 2, figsize=(6.65, 2.30),
                             gridspec_kw={'width_ratios': [1.55, 1.0]})

    # (a) 本库 REF → OPT
    ax = axes[0]
    labels = ['keygen', 'sign', 'verify', 'ECDH', 'SM4', 'CBC', 'CTR']
    vals = [3.29, 3.21, 2.44, 1.45, 2.03, 1.73, 1.73]
    x = np.arange(len(vals))
    ax.bar(x, vals, width=0.40, color=ACCENT, edgecolor='none', zorder=2)
    for xx, v in zip(x, vals):
        ax.text(xx, v + 0.07, '%.2f' % v, ha='center', va='bottom', fontsize=7.2)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=7.2)
    style(ax, 4.0, '加速比（REF ÷ OPT）')
    ax.text(0.0, 1.04, '(a) 本库 REF → OPT', transform=ax.transAxes,
            fontsize=8.4, fontweight='bold', ha='left', va='bottom')

    # (b) 对 GmSSL 3.1.1
    ax = axes[1]
    labels = ['k·G', 'verify', 'SM4', 'SM3', 'k·P']
    vals = [1.73, 2.14, 1.04, 0.88, 0.70]
    colors = [ACCENT if v >= 1 else GRAY for v in vals]
    x = np.arange(len(vals))
    ax.bar(x, vals, width=0.38, color=colors, edgecolor='none', zorder=2)
    for xx, v in zip(x, vals):
        ax.text(xx, v + 0.045, '%.2f' % v, ha='center', va='bottom', fontsize=7.2)
    ax.axhline(1.0, color=MUTED, linewidth=0.7, linestyle=(0, (3, 2)), zorder=1)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=7.2)
    style(ax, 2.55, '本库 ÷ GmSSL（>1 更快）')
    ax.text(0.0, 1.04, '(b) 对 GmSSL 3.1.1', transform=ax.transAxes,
            fontsize=8.4, fontweight='bold', ha='left', va='bottom')

    fig.tight_layout(w_pad=1.6)
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PDF, exist_ok=True)
    p = os.path.join(OUT, '图2-10-优化效果与外部基线.png')
    fig.savefig(p, dpi=600, bbox_inches='tight', pad_inches=0.02)
    fig.savefig(os.path.join(PDF, '图2-10-优化效果与外部基线.pdf'),
                bbox_inches='tight', pad_inches=0.02)
    plt.close(fig)
    print(p)


if __name__ == '__main__':
    main()
