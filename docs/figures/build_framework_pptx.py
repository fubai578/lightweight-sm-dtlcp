#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_framework_pptx.py — 生成"系统框架图" PPTX（图形对象**手工**绘制，非插图片）

用途：作品设计报告 / 答辩用的系统分层框架图。全部为原生形状，可直接在 PowerPoint 里编辑。
输出：docs/figures/系统框架图.pptx（同名 .png 由脚本外部的渲染步骤产出）

科研插图风格约定（本脚本的实现依据）：
  * 无色块装饰、无阴影、无渐变；统一细线（0.75–1.25 pt）
  * 主色（青绿）承载结构，唯一强调色（赭橙）只落在承载两项新结论的 SM2 块
  * 每张卡配**用基础图形画的极简图标**（不引外部图标库、不用 emoji、不含图片对象）
  * 关键数字不进色块，而进**图注**（图 1 …），与论文插图习惯一致
  * CJK 正文字号 ≥12 pt（规范最小可读字号）

复制/修改：改内容或配色后重跑本脚本即可，无需手工拖拽。
    python docs/figures/build_framework_pptx.py        # 生成并自检（0 问题则退出码 0）

⚠️ **会覆盖同名文件**：脚本直接把结果写到 `系统框架图.pptx`。若该文件已在
   WPS/PowerPoint 里手工调整过（本项目实际发生过：手工补了 8 处说明文字），
   重跑本脚本会**丢失这些手工改动**。改版式前先另存一份，或改脚本后重跑再手工微调。
⚠️ 元数据：保存后自动清空 `docProps/core.xml` 的作者/最后修改者——竞赛要求匿名评审，
   而 WPS/PowerPoint 另存一次就会把系统用户名写进去（本项目实际出现过编辑者昵称）。
"""
import os
import sys
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE, MSO_CONNECTOR
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.oxml.ns import qn, nsdecls
from pptx.oxml import parse_xml

# ----------------------------------------------------------------- 配色（唯一来源）
BG        = 'FFFFFF'
PRIMARY   = '0F4C5C'   # 深青绿：结构、层标签、标题
PRIMARY_D = '0A343F'
TINT_A    = 'EDF4F6'   # 层带浅色调（交替）
TINT_B    = 'DFEBEE'
BOX_FILL  = 'FFFFFF'
BOX_LINE  = 'C3D5DA'
MUTED     = '5C6E76'
ACCENT    = 'C2410C'   # 唯一强调色（略深的赭橙，比亮橙更"科研"）
ACCENT_BG = 'FBEADD'
WHITE     = 'FFFFFF'
RULE      = 'C9D6DA'

FONT = '微软雅黑'
W, H, M = 13.333, 7.5, 0.5


# ----------------------------------------------------------------- 文本
def set_run(run, text, size, color, bold=False, font=FONT):
    run.text = text
    f = run.font
    f.name = font
    f.size = Pt(size)
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


def para(tf, first, runs, align=PP_ALIGN.LEFT):
    p = tf.paragraphs[0] if first else tf.add_paragraph()
    p.alignment = align
    for t, sz, col, bold in runs:
        set_run(p.add_run(), t, sz, col, bold)
    return p


def block(slide, x, y, w, h, lines, anchor=MSO_ANCHOR.TOP, align=PP_ALIGN.LEFT):
    tb = textbox(slide, x, y, w, h, anchor)
    for i, t in enumerate(lines):
        para(tb.text_frame, i == 0, [t], align=align)
    return tb


# ----------------------------------------------------------------- 形状
def shape(slide, kind, x, y, w, h, fill=None, line=None, lw=0.75, radius=None):
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
    shp.shadow.inherit = False            # 科研插图不用阴影
    return shp


def seg(slide, x1, y1, x2, y2, color=PRIMARY, lw=1.0, head=False):
    c = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT,
                                   Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    c.line.color.rgb = RGBColor.from_string(color)
    c.line.width = Pt(lw)
    c.shadow.inherit = False
    if head:
        c._element.spPr.get_or_add_ln().append(
            parse_xml('<a:tailEnd %s type="triangle" w="med" len="med"/>' % nsdecls('a')))
    return c


ICON_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'icons', 'png')
# kind → Lucide 图标文件（ISC 许可；描边色已按本图配色生成为两套 PNG）
ICON_FILES = {
    'node': 'radio', 'rack': 'server', 'term': 'square-terminal', 'exchange': 'handshake',
    'stack': 'layers', 'key': 'key-round', 'cert': 'file-badge', 'hash': 'hash',
    'blocks': 'grid-2x2', 'curve': 'spline', 'bars': 'shield-check',
    'plug': 'plug', 'chip': 'cpu', 'check': 'clipboard-check',
}


def icon(slide, kind, x, y, s=0.30, col=PRIMARY):
    """按 kind 插入已上色的 Lucide 图标 PNG（512px、透明底、单色描边）。
    色板两套：青绿（结构性）与赭橙（强调块）。"""
    name = ICON_FILES[kind] + ('_accent.png' if col == ACCENT else '.png')
    path = os.path.join(ICON_DIR, name)
    if not os.path.isfile(path):
        raise SystemExit('缺少图标文件：%s（见 docs/figures/icons/README.md 的生成步骤）' % path)
    slide.shapes.add_picture(path, Inches(x), Inches(y), Inches(s), Inches(s))


def card(slide, x, y, w, h, title, detail, kind, accent=False):
    """卡：左侧极简图标 + 右侧标题与说明。强调只用描边与暖底（不用边缘色条/阴影）。"""
    shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h,
          fill=ACCENT_BG if accent else BOX_FILL,
          line=ACCENT if accent else BOX_LINE,
          lw=1.25 if accent else 0.75, radius=0.07)
    icon(slide, kind, x + 0.09, y + (h - 0.26) / 2.0, s=0.26,
         col=ACCENT if accent else PRIMARY)
    tb = textbox(slide, x + 0.43, y + 0.09, w - 0.50, h - 0.18, MSO_ANCHOR.MIDDLE)
    para(tb.text_frame, True, [(title, 13, ACCENT if accent else PRIMARY, True)])
    if detail:
        para(tb.text_frame, False, [(detail, 12, MUTED, False)])
    return tb


# ----------------------------------------------------------------- 构建
def build(path):
    prs = Presentation()
    prs.slide_width, prs.slide_height = Inches(W), Inches(H)
    s = prs.slides.add_slide(prs.slide_layouts[6])
    s.background.fill.solid()
    s.background.fill.fore_color.rgb = RGBColor.from_string(BG)

    block(s, M, 0.22, 12.3, 0.56,
          [('系统框架：轻量级国密算法栈 ＋ DTLCP 国密传输', 28, PRIMARY_D, True)])
    block(s, M, 0.80, 12.3, 0.22,
          [('资源受限物联网终端 ｜ 零 libc · 零动态内存 · 多档内核编译期切换 ｜ 上层依赖下层',
            12, MUTED, False)])

    LB_W = 1.45
    CX = M + LB_W + 0.15
    CW = (W - M) - CX
    ROW_Y, ROW_H, GAP = 1.02, 1.22, 0.09
    CH = ROW_H - 0.32

    def band(i, name, sub):
        y = ROW_Y + i * (ROW_H + GAP)
        shape(s, MSO_SHAPE.RECTANGLE, M, y, W - 2 * M, ROW_H,
              fill=TINT_A if i % 2 == 0 else TINT_B)
        chip = shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, M + 0.10, y + 0.18,
                     LB_W - 0.20, ROW_H - 0.36, fill=PRIMARY, radius=0.07)
        chip.text_frame.vertical_anchor = MSO_ANCHOR.MIDDLE
        chip.text_frame.margin_left = chip.text_frame.margin_right = Inches(0.04)
        para(chip.text_frame, True, [(name, 14, WHITE, True)], align=PP_ALIGN.CENTER)
        para(chip.text_frame, False, [(sub, 11, 'CFE3E7', False)], align=PP_ALIGN.CENTER)
        return y

    # ── 应用层
    y = band(0, '应用层', 'apps/demo')
    b1w = 3.30
    card(s, CX, y + 0.16, b1w, CH, '传感终端（client）', '连接 / 周期遥测 / 收 ACK / 统计', 'node')
    card(s, CX + b1w + 1.35, y + 0.16, b1w, CH, '网关（server）', '接受连接 / 回执 / 会话缓存', 'rack')
    ax1, ax2 = CX + b1w + 0.16, CX + b1w + 1.19
    seg(s, ax1, y + ROW_H / 2 + 0.12, ax2, y + ROW_H / 2 + 0.12, head=True, lw=1.25)
    block(s, ax1 - 0.02, y + 0.26, (ax2 - ax1) + 0.04, 0.44,
          [('DTLCP', 11, PRIMARY, True), ('over UDP', 11, PRIMARY, True)], align=PP_ALIGN.CENTER)
    card(s, CX + 2 * b1w + 1.48, y + 0.16, CW - (2 * b1w + 1.48), CH,
         'demo_run.sh', '三场景演示 · 一键复现\n含 0-RTT 早期数据', 'term')

    # ── 协议层（4 卡）
    y = band(1, '协议层', 'dtlcp')
    n, g = 4, 0.11
    bw = (CW - (n - 1) * g) / n
    for i, (t, d, k) in enumerate([
            ('握手状态机（SM2）', 'FULL 1.5-RTT｜5 次\nRESUME/PSK｜0 次', 'exchange'),
            ('记录层', 'EtM / MtE 双套件\n64 位滑动窗口防重放', 'stack'),
            ('密钥调度', 'PRF=P_hash(HMAC-SM3)\nmaster_secret 绑定双方', 'key'),
            ('紧凑证书', 'SM2 签发与校验 · TLV\nX.509 映射', 'cert')]):
        card(s, CX + i * (bw + g), y + 0.16, bw, CH, t, d, k)

    # ── 算法层（4 卡；SM2 为唯一强调块）
    y = band(2, '算法层', 'crypto')
    for i, (t, d, k, acc) in enumerate([
            ('SM3', 'REF / FAST / TINY\nTINY 单函数栈 ~64 B', 'hash', False),
            ('SM4', 'REF / T-fast 4 KB\nSMALL：RAM 增量 0', 'blocks', False),
            ('SM2', '折叠约减 · wNAF · 交织\ncomb 基点表 + 对端表', 'curve', True),
            ('HMAC · KDF · DRBG', '认证 · 密钥派生 · 随机数', 'bars', False)]):
        card(s, CX + i * (bw + g), y + 0.16, bw, CH, t, d, k, accent=acc)

    # ── 平台层（3 卡）
    y = band(3, '平台层', 'embedded')
    x = CX
    for t, d, k, w0 in [
            ('平台钩子（6 个）', 'TRNG · DWT · 看门狗 · 延时\n初始化 · 调试输出', 'plug', 3.60),
            ('目标平台', 'STM32F407 Cortex-M4F\n板上自检 + DWT 出真实 cycles', 'chip', 3.60),
            ('上板前预检', '宿主仿真跑同一份板级代码\n＋ 语法检查 ＋ 32K 链接证明', 'check', 0)]:
        w = w0 if w0 else (CX + CW - x)
        card(s, x, y + 0.16, w, CH, t, d, k)
        x += w + 0.13

    # ── 图注（数字进图注，不做色块：与论文插图习惯一致）
    ry = 6.24
    seg(s, M, ry, W - M, ry, color=RULE, lw=0.75)
    block(s, M, ry + 0.10, W - 2 * M, 0.24,
          [('图 1　系统分层框架。keygen 3.67× · 验签 1.83×（对端公钥 comb 表）· '
            '常量时间代价约 1.1× · 最小 Flash 20.6 KB / 最大调用路径栈深 3216 B',
            12, PRIMARY_D, False)])
    block(s, M, ry + 0.38, W - 2 * M, 0.22,
          [('口径：加速比取自同一次基准输出的同轮比值（RDTSC 绝对值跨运行不可比）。'
            '数据：bin/bench_results.md、bin/ct_cost.md', 11.5, MUTED, False)])

    prs.save(path)
    return prs


# ----------------------------------------------------------------- 自检
def qa(prs, verbose=True):
    """溢出与重叠自检。行数估算：CJK 全角、拉丁 0.62 宽；仅比较含文字形状的包围盒。"""
    def seg_w(t, pt, bold=False):
        k = 0.58 * (1.06 if bold else 1.0)
        return sum((1.0 if ord(c) > 0x2E7F else k) for c in t) * pt / 72.0

    problems = []
    for slide in prs.slides:
        boxes = []
        for shp in slide.shapes:
            if shp.shape_type is not None and shp.shape_type == 13:   # PICTURE
                px, py = shp.left / 914400.0, shp.top / 914400.0
                pw, ph = shp.width / 914400.0, shp.height / 914400.0
                if px < -0.01 or py < -0.01 or px + pw > W + 0.01 or py + ph > H + 0.01:
                    problems.append('图片越界: (%.2f,%.2f,%.2f,%.2f)' % (px, py, pw, ph))
                continue
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
                    need += pt * 1.32 / 72.0
                    continue
                for sg in txt.split(chr(10)):
                    need += max(1, int(seg_w(sg, pt, bold) / avail + 0.999)) * pt * 1.32 / 72.0
            if need > h + 0.05:
                problems.append('可能溢出: %r 需 %.2f in > 框高 %.2f in'
                                % (shp.text_frame.text[:22].replace(chr(10), '/'), need, h))
            boxes.append((x, y, w, h, shp.text_frame.text[:14].replace(chr(10), '/')))
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a_, b_ = boxes[i], boxes[j]
                ox = min(a_[0] + a_[2], b_[0] + b_[2]) - max(a_[0], b_[0])
                oy = min(a_[1] + a_[3], b_[1] + b_[3]) - max(a_[1], b_[1])
                if ox > 0.05 and oy > 0.05:
                    problems.append('重叠: %r x %r (%.2f x %.2f in)'
                                    % (a_[4], b_[4], ox, oy))
    if verbose:
        print('自检：%d 个问题' % len(problems))
        for pr in problems:
            print('  -', pr)
    return problems


def sanitize_meta(path):
    """清空文档属性（作者/最后修改者）。

    竞赛要求匿名评审，而 PPTX 的 docProps/core.xml 会记录编辑者昵称
    （用 WPS/PowerPoint 打开另存一次就会写入系统用户名）。生成后必须清空，
    否则支撑材料里的图会带出可识别信息。"""
    import re
    import shutil
    import zipfile
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
    zout.close(); zin.close()
    shutil.move(tmp, path)
    print('元数据已清空（作者/最后修改者）')


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'docs/figures/系统框架图.pptx'
    os.makedirs(os.path.dirname(out), exist_ok=True)
    prs = build(out)
    sanitize_meta(out)
    print('已生成', out)
    sys.exit(1 if qa(prs) else 0)
