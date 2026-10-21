#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
figlib.py — 作品设计报告插图统一绘图库（v2：多色系 · 大字版式 · 内置小图标）

设计规则（v2 针对"小字太多、主体不清晰、缺小图标、色调单一"重做）：
  * **多色系**：六个色族按语义分配（协议=青、传输=蓝、SM3=绿、SM4=紫、算力=琥珀、风险=玫红），
    每族含"深/浅底/中调"三档，一图内用 3–4 族，避免全图一个颜色。
  * **字号下限**：正文标签 ≥13.5 pt，注释 ≥12 pt，面板标题 19 pt，关键数字 22–34 pt；
    能画出来的少写字——图内只留标签与数字，解释留给报告正文。
  * **小图标**：每张卡/每个步骤配一个 Lucide 图标（40 图标 × 9 色，见 icons/png2/），
    另有 packet/bar/ring/window 等"小图"原语。
  * 形状一律无阴影、无渐变；画布 13.333 × 7.5 in。

用法：
    from figlib import *
    prs = new_deck(); s = new_slide(prs, '标题', '副标题')
    ...
    save(prs, '图X-Y-名称.pptx')
"""
import math
import os
import re
import shutil
import sys
import zipfile

from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE, MSO_CONNECTOR
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.oxml.ns import qn, nsdecls
from pptx.oxml import parse_xml

# ----------------------------------------------------------------- 色族
# 低饱和"论文图表"色板：一个主色（青）+ 一个强调色（赭），其余为不同明度的中性色
HUE = {
    'teal':   ('1B4F5C', 'EEF3F4', 'A8C0C6'),
    'blue':   ('2C4F6B', 'EEF2F6', 'AFBECD'),
    'green':  ('3D6B58', 'EEF3EF', 'ADC5B8'),
    'purple': ('5A4C6A', 'F2EFF4', 'BFB4C7'),
    'amber':  ('9C5A24', 'F8F2E9', 'D5B995'),
    'rose':   ('8C4351', 'F7EFEF', 'CBABB0'),
}
INK, TEXT, MUTED = '1F3A40', '47585E', '74878D'
LINE, RULE, PAPER, WHITE = 'DCE4E6', 'E6ECEE', 'FFFFFF', 'FFFFFF'
HAIR = 'C9D5D8'

# 字号表（v2 下限更高）
T_TITLE, T_SUB = 22, 13
T_PANEL, T_BODY, T_LABEL, T_NOTE = 16, 13, 12.5, 10.5
T_BIG, T_NUM, T_HUGE = 24, 17, 26

FONT = '微软雅黑'
# 图上字号 → 纸上字号的换算：Word 正文栏宽 6.3 in，图为 13.333 in 画布，
# 缩放 0.4725。故图内 13.5 pt 仅剩 6.4 pt——本项目曾因此被批"小字太多"。
# FS 把所有图内字号整体放大，使纸上字号 ≥10 pt（正文标签）/ ≥8.5 pt（脚注）。
FS = 1.30
W, H, M = 13.333, 7.5, 0.5
BODY_W = W - 2 * M
ICON_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'icons', 'png2')


def hue(name):
    return HUE[name]


# ----------------------------------------------------------------- 文本
def set_run(run, text, size, color, bold=False, font=FONT):
    run.text = text
    f = run.font
    f.name = font
    f.size = Pt(size * FS)
    f.bold = bold
    f.color.rgb = RGBColor.from_string(color)
    rPr = run._r.get_or_add_rPr()
    for tag in ('a:ea', 'a:cs'):
        el = rPr.find(qn(tag))
        if el is None:
            el = parse_xml('<%s %s typeface="%s"/>' % (tag, nsdecls('a'), font))
            rPr.append(el)
        else:
            el.set('typeface', font)


def textbox(slide, x, y, w, h, anchor=MSO_ANCHOR.TOP):
    tb = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = tb.text_frame
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = tf.margin_top = tf.margin_bottom = 0
    tf.vertical_anchor = anchor
    return tb


def para(tf, first, runs, align=PP_ALIGN.LEFT, space=1.0):
    p = tf.paragraphs[0] if first else tf.add_paragraph()
    p.alignment = align
    p.line_spacing = space
    for t in runs:
        set_run(p.add_run(), t[0], t[1], t[2], t[3])
    return p


def _need_h(lines, w):
    """按放大后的字号估算多行文本所需高度（行高 1.22×字号）"""
    need = 0.0
    for t in lines:
        txt, size = t[0], t[1]
        pt = size * FS
        for sg in str(txt).splitlines():
            n = max(1, int(_seg_w(sg, pt) / max(0.2, w) + 0.999))
            need += n * pt * 1.22 / 72.0
    return need + 0.03


def block(slide, x, y, w, h, lines, anchor=MSO_ANCHOR.TOP, align=PP_ALIGN.LEFT,
          space=1.02):
    h = max(h, _need_h(lines, w))          # 框高按字号自动取足
    tb = textbox(slide, x, y, w, h, anchor)
    for i, t in enumerate(lines):
        para(tb.text_frame, i == 0, [t], align, space)
    return tb


def label(slide, x, y, w, text, size=T_LABEL, color=TEXT, bold=False,
          align=PP_ALIGN.CENTER, h=0.30):
    return block(slide, x, y, w, h, [(text, size, color, bold)],
                 MSO_ANCHOR.MIDDLE, align)


# ----------------------------------------------------------------- 形状
def shape(slide, kind, x, y, w, h, fill=None, line=None, lw=1.0, radius=None):
    shp = slide.shapes.add_shape(
        MSO_SHAPE.ROUNDED_RECTANGLE if radius is not None else kind,
        Inches(x), Inches(y), Inches(w), Inches(h))
    if fill:
        shp.fill.solid()
        shp.fill.fore_color.rgb = RGBColor.from_string(fill)
    else:
        shp.fill.background()
    if line:
        shp.line.color.rgb = RGBColor.from_string(line)
        shp.line.width = Pt(lw)
    else:
        shp.line.fill.background()
    if radius is not None:
        shp.adjustments[0] = radius
    shp.shadow.inherit = False
    # 去掉主题样式引用（否则 LibreOffice 会按主题把阴影/描边加回来）
    style_el = shp._element.find(qn('p:style'))
    if style_el is not None:
        shp._element.remove(style_el)
    shp.text_frame.word_wrap = True
    return shp


def panel(slide, x, y, w, h, huename, fill=None, lw=1.25, radius=0.045):
    """色族浅底 + 同族描边的面板"""
    deep, tint, mid = hue(huename)
    return shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h,
                 fill=fill or tint, line=mid, lw=lw, radius=radius)


def seg(slide, x1, y1, x2, y2, color=INK, lw=1.0, head=False, dash=None,
        tail=False):
    c = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT,
                                   Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    c.line.color.rgb = RGBColor.from_string(color)
    c.line.width = Pt(lw)
    c.shadow.inherit = False
    ln = c._element.spPr.get_or_add_ln()
    if head:
        ln.append(parse_xml('<a:tailEnd %s type="triangle" w="lg" len="med"/>'
                            % nsdecls('a')))
    if tail:
        ln.append(parse_xml('<a:headEnd %s type="triangle" w="lg" len="med"/>'
                            % nsdecls('a')))
    if dash:
        ln.append(parse_xml('<a:prstDash %s val="%s"/>' % (nsdecls('a'), dash)))
    return c


def arrow(slide, x1, y1, x2, y2, huename=None, color=None, lw=2.5, dash=None):
    deep = hue(huename)[0] if huename else (color or INK)
    return seg(slide, x1, y1, x2, y2, color=deep, lw=lw, head=True, dash=dash)


def icon(slide, name, x, y, size=0.46, color='teal'):
    """Lucide 图标（png2/<色>/<名>.png），用于卡片/步骤/面板标题"""
    path = os.path.join(ICON_DIR, color, name + '.png')
    if not os.path.isfile(path):
        raise SystemExit('缺图标：%s（色 %s）' % (name, color))
    slide.shapes.add_picture(path, Inches(x), Inches(y), Inches(size),
                             Inches(size))


def chip(slide, x, y, w, h, text, huename='teal', size=T_BODY, on_dark=True,
         icon_name=None, radius=0.22):
    """小圆角标签：可带图标（深底白字 / 浅底深字）"""
    deep, tint, mid = hue(huename)
    fill = deep if on_dark else tint
    col = WHITE if on_dark else deep
    shp = shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h, fill=fill,
                line=None if on_dark else mid, lw=1.0, radius=radius)
    tf = shp.text_frame
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    tf.margin_left = tf.margin_right = Inches(0.04)
    if icon_name:
        icon(slide, icon_name, x + 0.10, y + (h - 0.26) / 2, 0.26,
             'white' if on_dark else huename)
        tf.margin_left = Inches(0.42)
    para(tf, True, [(text, size, col, True)], PP_ALIGN.CENTER)
    return shp


def card(slide, x, y, w, h, title=None, lines=None, huename='teal',
         icon_name=None, icon_size=0.52, tsize=T_PANEL, lsize=T_LABEL,
         accent=False, tone='tint'):
    """卡片：图标 + 标题 + 少量说明行（浅底，或 tone='white' 白底描边）"""
    deep, tint, mid = hue(huename)
    fill = tint if tone == 'tint' else PAPER
    shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h, fill=fill,
          line=deep if accent else mid, lw=1.6 if accent else 1.0, radius=0.07)
    tx = x + 0.18
    if icon_name:
        icon(slide, icon_name, tx, y + 0.18, icon_size, huename)
        tx += icon_size + 0.16
    if title or lines:
        block(slide, tx, y + 0.16, x + w - 0.18 - tx, h - 0.32,
              ([(title, tsize, deep, True)] if title else []) +
              [(l, lsize, TEXT, False) for l in (lines or [])],
              MSO_ANCHOR.MIDDLE)
    return x + w


def big_num(slide, x, y, w, num, unit='', label_text='', huename='amber',
            nsize=T_HUGE, align=PP_ALIGN.CENTER, lsize=T_LABEL):
    """关键数字（大号、色族深色）+ 下方小字说明"""
    deep = hue(huename)[0]
    runs = [(num, nsize, deep, True)]
    if unit:
        runs.append((' ' + unit, T_BODY, deep, True))
    tb = textbox(slide, x, y, w, 1.00, MSO_ANCHOR.MIDDLE)
    para(tb.text_frame, True, runs, align)
    if label_text:
        label(slide, x, y + 1.00, w, label_text, lsize, MUTED, False, align, 0.34)
    return tb


def badge(slide, x, y, w, h, text, huename='green', icon_name='badge-check',
          size=T_LABEL):
    return chip(slide, x, y, w, h, text, huename, size, on_dark=False,
                icon_name=icon_name, radius=0.30)


# ----------------------------------------------------------------- 小图原语
def packet(slide, x, y, w, h, fields, size=T_LABEL):
    """报文格式条：fields = [(文本, 标注, 权重, 色族)]"""
    tot = sum(f[2] for f in fields)
    cx = x
    for text, note, weight, huename in fields:
        cw = w * weight / tot
        deep, tint, mid = hue(huename)
        shp = shape(slide, MSO_SHAPE.RECTANGLE, cx, y, cw, h, fill=tint,
                    line=mid, lw=1.0)
        tf = shp.text_frame
        tf.vertical_anchor = MSO_ANCHOR.MIDDLE
        tf.margin_left = tf.margin_right = Inches(0.03)
        para(tf, True, [(text, size, deep, True)], PP_ALIGN.CENTER)
        if note:
            para(tf, False, [(note, T_NOTE, MUTED, False)], PP_ALIGN.CENTER)
        cx += cw
    return cx


def bars(slide, x, y, w, rows, maxv, hgt=0.42, gap=0.16, show_val=True):
    """横向条形对比：rows = [(名称, 值, 色族, 是否高亮)]"""
    for i, (name, val, huename, hot) in enumerate(rows):
        yy = y + i * (hgt + gap)
        deep, tint, mid = hue(huename)
        bw = max(0.05, w * val / maxv)
        shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, yy, w, hgt,
              fill='F2F6F7', line=None, radius=0.5)
        shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, yy, bw, hgt,
              fill=deep, line=None, radius=0.5)
        if show_val:
            label(slide, x + bw + 0.12, yy, w - bw - 0.12,
                  '%.2f×' % val, T_NUM, deep if hot else MUTED, True,
                  PP_ALIGN.LEFT, hgt)
    return y + len(rows) * (hgt + gap)


def named_bars(slide, x, y, w, rows, maxv, name_w=1.85, hgt=0.42, gap=0.18,
               fmt='%.2f×', baseline=None, name_size=T_BODY, val_size=T_NUM):
    """带名称列的横向条形：rows = [(名称, 值, 色族, 是否高亮)]；baseline=参考竖线（如 1.0）"""
    bx = x + name_w + 0.14
    val_w = 1.12                      # 数值列固定预留，避免长条把数字挤出面板
    span = w - name_w - 0.14 - val_w
    for i, (name, val, hn, hot) in enumerate(rows):
        yy = y + i * (hgt + gap)
        deep, tint, mid = hue(hn)
        label(slide, x, yy, name_w, name, name_size, TEXT, False, PP_ALIGN.RIGHT, hgt)
        shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, bx, yy, span, hgt,
              fill='F2F6F7', line=None, radius=0.5)
        bw = max(0.05, span * val / maxv)
        shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, bx, yy, bw, hgt, fill=deep,
              line=None, radius=0.5)
        label(slide, bx + span + 0.10, yy, val_w, fmt % val, val_size,
              deep if hot else MUTED, True, PP_ALIGN.LEFT, hgt)
    if baseline is not None:
        bz = bx + span * baseline / maxv
        seg(slide, bz, y - 0.08, bz,
            y + len(rows) * (hgt + gap) - gap + 0.08, color=INK, lw=1.5,
            dash='dash')
    return y + len(rows) * (hgt + gap)


def ring_slots(slide, cx, cy, r, n, hot=(), size=0.28, huename='green',
               hot_hue='amber'):
    """环形槽位（示意滚动调度/环状结构）"""
    deep = hue(huename)[0]
    hot_deep = hue(hot_hue)[0]
    for i in range(n):
        a = -math.pi / 2 + 2 * math.pi * i / n
        x = cx + r * math.cos(a) - size / 2
        y = cy + r * math.sin(a) - size / 2
        shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, size, size,
              fill=hot_deep if i in hot else deep, line=None, radius=0.25)


def grid_cells(slide, x, y, cols, rows, cw, ch, huename='purple', gap=0.04,
               hot=()):
    """小方格阵列（示意查表/矩阵）"""
    deep, tint, mid = hue(huename)
    for r in range(rows):
        for c in range(cols):
            i = r * cols + c
            f = deep if i in hot else tint
            shape(slide, MSO_SHAPE.RECTANGLE, x + c * (cw + gap),
                  y + r * (ch + gap), cw, ch, fill=f,
                  line=mid if f == tint else None, lw=0.5)


def hrule(slide, x, y, w, color=RULE, lw=0.75):
    return seg(slide, x, y, x + w, y, color=color, lw=lw)


def vrule(slide, x, y0, y1, color=RULE, lw=0.75):
    return seg(slide, x, y0, x, y1, color=color, lw=lw)


def dotbar(slide, x0, x1, y, hue_name='teal', dot=0.15, lw=2.0, color=None):
    """横向棒棒糖：从 x0 到 x1 的细杆 + 端点圆点（x1>x0 向右）"""
    deep = color or hue(hue_name)[0]
    if abs(x1 - x0) > 0.02:
        seg(slide, x0, y, x1, y, color=deep, lw=lw)
    shape(slide, MSO_SHAPE.OVAL, x1 - dot / 2, y - dot / 2, dot, dot,
          fill=deep, line=None)


def axis_label(slide, x, y, w, text, size=12, color=MUTED, align=PP_ALIGN.LEFT,
               h=0.34):
    return label(slide, x, y, w, text, size, color, False, align, h)


def tick(slide, x, y, text, size=10.5, color=MUTED, w=0.9,
         align=PP_ALIGN.CENTER):
    return label(slide, x - w / 2, y, w, text, size, color, False, align, 0.28)


def window_strip(slide, x, y, n, win_start, win_len, huename='blue', size=0.30,
                 gap=0.05):
    """滑动窗口（示意防重放窗口）"""
    deep, tint, mid = hue(huename)
    for i in range(n):
        inside = win_start <= i < win_start + win_len
        shape(slide, MSO_SHAPE.RECTANGLE, x + i * (size + gap), y, size, size,
              fill=deep if inside else PAPER,
              line=mid if inside else LINE, lw=0.75)
    return x + n * (size + gap)


# ----------------------------------------------------------------- 版式
def new_deck():
    prs = Presentation()
    prs.slide_width, prs.slide_height = Inches(W), Inches(H)
    return prs


def new_slide(prs, title, sub=None, huename='teal'):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    s.background.fill.solid()
    s.background.fill.fore_color.rgb = RGBColor.from_string(PAPER)
    deep = hue(huename)[0]
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, M, 0.30, 0.16, 0.56, fill=deep,
          line=None, radius=0.5)
    block(s, M + 0.29, 0.24, BODY_W - 0.29, 0.84, [(title, T_TITLE, INK, True)])
    if sub:
        block(s, M + 0.29, 0.96, BODY_W - 0.29, 0.46, [(sub, T_SUB, MUTED, False)])
    return s


def plate(prs):
    """纯图版画布：无大标题、无副标题、无页脚——题注与来源交给正文。"""
    s = prs.slides.add_slide(prs.slide_layouts[6])
    s.background.fill.solid()
    s.background.fill.fore_color.rgb = RGBColor.from_string(PAPER)
    return s


def footer(s, caption, source=None, y=6.30, fs=1.0):
    seg(s, M, y, W - M, y, color=RULE, lw=1.0)
    lines = [(caption, 11 * fs, TEXT, False)]
    if source:
        lines.append((source, 10 * fs, MUTED, False))
    block(s, M, y + 0.08, BODY_W, H - y - 0.12, lines)


# ----------------------------------------------------------------- 元数据与自检
def sanitize_meta(path):
    tmp = path + '.tmp'
    zin = zipfile.ZipFile(path)
    zout = zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED)
    for item in zin.infolist():
        data = zin.read(item.filename)
        if item.filename == 'docProps/core.xml':
            t = data.decode('utf-8')
            t = re.sub(r'<dc:creator>.*?</dc:creator>', '<dc:creator></dc:creator>', t)
            t = re.sub(r'<cp:lastModifiedBy>.*?</cp:lastModifiedBy>',
                       '<cp:lastModifiedBy></cp:lastModifiedBy>', t)
            data = t.encode('utf-8')
        zout.writestr(item, data)
    zout.close()
    zin.close()
    shutil.move(tmp, path)


def _seg_w(t, pt, bold=False):
    k = 0.58 * (1.06 if bold else 1.0)
    return sum((1.0 if ord(c) > 0x2E7F else k) for c in t) * pt / 72.0


def qa(prs, verbose=True):
    problems = []
    for slide in prs.slides:
        boxes = []
        for shp in slide.shapes:
            try:
                if int(shp.shape_type or 0) == 13:
                    px, py = shp.left / 914400.0, shp.top / 914400.0
                    pw, ph = shp.width / 914400.0, shp.height / 914400.0
                    if px < -0.01 or py < -0.01 or px + pw > W + 0.01 or py + ph > H + 0.01:
                        problems.append('图片越界: (%.2f,%.2f,%.2f,%.2f)' % (px, py, pw, ph))
                    continue
            except Exception:
                pass
            if not shp.has_text_frame or not shp.text_frame.text.strip():
                continue
            x, y = shp.left / 914400.0, shp.top / 914400.0
            w, h = shp.width / 914400.0, shp.height / 914400.0
            ml = (shp.text_frame.margin_left or 0) / 914400.0
            mr = (shp.text_frame.margin_right or 0) / 914400.0
            if x < -0.01 or y < -0.01 or x + w > W + 0.01 or y + h > H + 0.01:
                problems.append('越界: %r (%.2f,%.2f,%.2f,%.2f)'
                                % (shp.text_frame.text[:18], x, y, w, h))
            avail = max(0.2, w - ml - mr - 0.03)
            need = 0.0
            for pa in shp.text_frame.paragraphs:
                txt = ''.join(r.text for r in pa.runs)
                runs = [r for r in pa.runs if r.font.size]
                pt = max(r.font.size.pt for r in runs) if runs else 12.0
                bold = any(r.font.bold for r in runs)
                if not txt:
                    need += pt * 1.22 / 72.0
                    continue
                for sg in txt.split('\n'):
                    need += max(1, int(_seg_w(sg, pt, bold) / avail + 0.999)) * pt * 1.22 / 72.0
            if need > h + 0.06:
                problems.append('可能溢出: %r 需 %.2f in > 框高 %.2f in'
                                % (shp.text_frame.text[:22].replace('\n', '/'), need, h))
            plain = int(shp.shape_type or 0) == 17        # 17 = 纯文本框
            boxes.append((x, y, w, h, shp.text_frame.text[:14].replace('\n', '/'),
                          plain))
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a_, b_ = boxes[i], boxes[j]
                ox = min(a_[0] + a_[2], b_[0] + b_[2]) - max(a_[0], b_[0])
                oy = min(a_[1] + a_[3], b_[1] + b_[3]) - max(a_[1], b_[1])
                # 两个纯文本框相邻：框高按字号取足后允许小幅交叠（文字不会真撞上）；
                # 只要有一个是卡片/色块/面板（有边框或底色），就按严格阈值判定。
                lim = (0.22, 0.25) if (a_[5] and b_[5]) else (0.06, 0.06)
                if ox > lim[1] and oy > lim[0]:
                    problems.append('重叠: %r x %r (%.2f x %.2f in)'
                                    % (a_[4], b_[4], ox, oy))
    if verbose:
        print('自检：%d 个问题' % len(problems))
        for pr in problems:
            print('  -', pr)
    return problems


HERE = os.path.dirname(os.path.abspath(__file__))


def save(prs, name, quiet=False):
    out = os.path.join(HERE, 'pptx', name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    prs.save(out)
    sanitize_meta(out)
    if not quiet:
        print('已生成', os.path.relpath(out, HERE))
    return out
