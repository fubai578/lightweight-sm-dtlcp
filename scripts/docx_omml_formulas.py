#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
docx_omml_formulas.py — 把报告 2.1.7 节的行内 ASCII 公式换成 OMML 真公式

背景：作品设计报告 2.1.7（SM2 基点 comb 预计算表）里的公式此前是纯文本，
      ⌈256/t⌉、Σᵢ、2^(i·d)·G、2ᵗ 等均为 ASCII/Unicode 近似写法：
        · 上下标靠 Unicode 造字（ᵢ ⁱ ᵗ ⱼ），字库缺字时会掉字形
        · 求和号没有上下限，Σ_{i} 读不出求和范围
        · 2^(i·d) 用 ^ 表示幂，与全文其他公式风格不一致
      本脚本把这些片段替换为 Word 原生公式对象（OMML，m:oMath），
      由 Word/WPS 的公式引擎排版：分式、上下标、求和上下限均为真结构。

公式语义与仓库实现同源（改公式前先核这两处）：
      apps/gen/gen_fixed_base.c:45-52   comb_idx / comb_mul：K_j 的 i∈[0,t)、循环 j=d-1→0
      crypto/src/sm2_arith.c:938-975    scalarmult_base_comb：d−1 次倍点 + ≤d−1 次混合加
      crypto/src/sm2_base_table.h:7-9   T[α] = Σ_{i=0}^{t-1} α_i·2^{i·d}·G，d = ceil(256/t)

用法：
      python scripts/docx_omml_formulas.py --check  <in.docx>
      python scripts/docx_omml_formulas.py --apply  <in.docx> <out.docx>
      python scripts/docx_omml_formulas.py --inplace <in.docx>

--check 只报告将要改哪些段落、不写文件（用于确认定位无误）。
"""

import argparse
import re
import shutil
import sys
import zipfile

# ---------------------------------------------------------------- OMML 基元

_MATH_FONT = '<w:rFonts w:ascii="Cambria Math" w:hAnsi="Cambria Math"/>'
_CTRL = ('<m:ctrlPr><w:rPr>' + _MATH_FONT +
         '<w:i/></w:rPr></m:ctrlPr>')


def _esc(s):
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;'))


def mr(t, upright=False):
    """数学 run。upright=True 用于函数名/文字（直立体），默认变量斜体。"""
    sty = '<m:rPr><m:sty m:val="p"/></m:rPr>' if upright else ''
    return ('<m:r>' + sty + '<w:rPr>' + _MATH_FONT +
            '</w:rPr><m:t xml:space="preserve">' + _esc(t) + '</m:t></m:r>')


def ctrl():
    return _CTRL


def msub(base, s):
    """下标：base_s"""
    return ('<m:sSub><m:sSubPr>' + _CTRL + '</m:sSubPr>'
            '<m:e>' + base + '</m:e><m:sub>' + s + '</m:sub></m:sSub>')


def msup(base, s):
    """上标：base^s"""
    return ('<m:sSup><m:sSupPr>' + _CTRL + '</m:sSupPr>'
            '<m:e>' + base + '</m:e><m:sup>' + s + '</m:sup></m:sSup>')


def mfrac(num, den):
    """分式 num/den"""
    return ('<m:f><m:fPr><m:type m:val="bar"/>' + _CTRL + '</m:fPr>'
            '<m:num>' + num + '</m:num><m:den>' + den + '</m:den></m:f>')


def mdelim(beg, end, body):
    """定界符 beg body end（beg/end 可不同，用于 [0, 2^t) 这类半开区间）"""
    return ('<m:d><m:dPr><m:begChr m:val="' + _esc(beg) + '"/>'
            '<m:endChr m:val="' + _esc(end) + '"/>' + _CTRL + '</m:dPr>'
            '<m:e>' + body + '</m:e></m:d>')


def mnary(sub, sup, body, ch='∑'):
    """n-ary 求和。行内公式用 subSup（上下限置于符号右侧），避免撑高行距。"""
    return ('<m:nary><m:naryPr><m:chr m:val="' + ch + '"/>'
            '<m:limLoc m:val="subSup"/>'
            '<m:subHide m:val="0"/><m:supHide m:val="0"/>' + _CTRL +
            '</m:naryPr>'
            '<m:sub>' + sub + '</m:sub><m:sup>' + sup + '</m:sup>'
            '<m:e>' + body + '</m:e></m:nary>')


def mfunc(name, args):
    """函数调用：name(args)，函数名直立体"""
    return ('<m:func><m:funcPr>' + _CTRL + '</m:funcPr>'
            '<m:fName>' + mr(name, upright=True) + '</m:fName>'
            '<m:e>' + args + '</m:e></m:func>')


def omath(*parts):
    """行内公式对象。m:oMath 可直接作为 w:p 的子元素（EG_PContent）。"""
    return '<m:oMath>' + ''.join(parts) + '</m:oMath>'


# ------------------------------------------------------- 2.1.7 的公式定义
# 变量一律斜体（数学默认）；函数名 bit 直立体；数字直立体由 Word 自动处理。

def f_d_ceil():
    """d = ⌈256/t⌉"""
    return omath(mr('d'), mr('='),
                 mdelim('⌈', '⌉', mfrac(mr('256'), mr('t'))))


def f_kj():
    """K_j = Σ_{i=0}^{t-1} bit(k, j + i·d) · 2^i"""
    arg = (mr('k') + mr(', ') +
           mr('j') + mr('+') + mr('i') + mr('·') + mr('d'))
    body = (mfunc('bit', arg) + mr('·') +
            msup(mr('2'), mr('i')))
    return omath(msub(mr('K'), mr('j')), mr('='),
                 mnary(mr('i') + mr('=') + mr('0'),
                       mr('t') + mr('−') + mr('1'),
                       body))


def f_table():
    """T[α] = Σ_{i=0}^{t-1} α_i · 2^{i·d} · G"""
    body = (msub(mr('α'), mr('i')) + mr('·') +
            msup(mr('2'), mr('i') + mr('·') + mr('d')) + mr('·') +
            mr('G'))
    return omath(mr('T') + mdelim('[', ']', mr('α')), mr('='),
                 mnary(mr('i') + mr('=') + mr('0'),
                       mr('t') + mr('−') + mr('1'),
                       body))


def f_alpha_range():
    """α ∈ [0, 2^t)"""
    body = mr('0') + mr(', ') + msup(mr('2'), mr('t'))
    return omath(mr('α'), mr('∈'), mdelim('[', ')', body))


def f_comb():
    """k·G = Σ_{j=0}^{d-1} 2^j · T[K_j]"""
    body = (msup(mr('2'), mr('j')) + mr('·') +
            mr('T') + mdelim('[', ']', msub(mr('K'), mr('j'))))
    return omath(mr('k') + mr('·') + mr('G'), mr('='),
                 mnary(mr('j') + mr('=') + mr('0'),
                       mr('d') + mr('−') + mr('1'),
                       body))


def f_d_minus_1():
    """d − 1"""
    return omath(mr('d'), mr('−'), mr('1'))


def f_t_eq_5():
    """t = 5"""
    return omath(mr('t'), mr('='), mr('5'))


def f_basis():
    """B_i = 2^{i·d}·G"""
    return omath(msub(mr('B'), mr('i')), mr('='),
                 msup(mr('2'), mr('i') + mr('·') + mr('d')) + mr('·') +
                 mr('G'))


def f_table_sym():
    """T[α]（单独引用的表项符号）"""
    return omath(mr('T') + mdelim('[', ']', mr('α')))


# ------------------------------------------------------------ 段落改写表
# key = 段落纯文本的唯一起始片段；value = [(kind, payload), ...]
# kind: "t" 普通文本（沿用正文字体） / "m" 公式（payload 为 omath 串）

# 公式替换完成后，2.1.7 标题尾部红色待办标记"（公式替换）"一并去掉。
# 该 run 带 <w:color w:val="FF0000"/>，是遗留的编辑标记而非正文内容。
_TITLE_TODO = (
    '<w:r><w:rPr><w:rFonts w:hint="eastAsia" w:ascii="Times New Roman" '
    'w:hAnsi="Times New Roman" w:eastAsia="黑体" w:cs="Times New Roman"/>'
    '<w:color w:val="FF0000"/><w:lang w:eastAsia="zh"/>'
    '<w:woUserID w:val="1"/><w:sz w:val="24"/><w:szCs w:val="24"/>'
    '<w:b/><w:bCs/></w:rPr><w:t>（公式替换）</w:t></w:r>'
)


def drop_title_todo(xml):
    """删除标题里的红色"（公式替换）"待办 run"""
    if _TITLE_TODO not in xml:
        print('  [警告] 未找到标题待办标记"（公式替换）"，未做删除')
        return xml, False
    print('  [标题] 删除红色待办标记"（公式替换）"')
    return xml.replace(_TITLE_TODO, ''), True


_REWRITES = [
    # 2.1.7 正文段：comb 结构定义
    ('密钥生成与签名都要计算', [
        ('t', '密钥生成与签名都要计算 '),
        ('m', omath(mr('k') + mr('·') + mr('G'))),
        ('t', '，而 G 是固定基点，因此可以用预计算表把倍点次数从 257 次降到 51 次。'
              '本作品采用 Lim-Lee comb 结构：取 t 位索引、把 256 位标量切成 '),
        ('m', f_d_ceil()),
        ('t', ' 个平衡量 '),
        ('m', f_kj()),
        ('t', '，预计算表 '),
        ('m', f_table()),
        ('t', '（'),
        ('m', f_alpha_range()),
        ('t', '），则 '),
        ('m', f_comb()),
        ('t', '，用 Horner 迭代只需 '),
        ('m', f_d_minus_1()),
        ('t', ' 次倍点与不超过 d 次混合加。取 '),
        ('m', f_t_eq_5()),
        ('t', ' 时表为 32 项、2048 B，且以只读常量形式存放在 Flash，'
              '静态 RAM 增量为 0。'),
    ]),
    # 生成过程第 1 条：基元定义
    ('基元 B', [
        ('t', '基元 '),
        ('m', f_basis()),
        ('t', ' 由反复倍点独立算出，不经过任何标量乘算法，'
              '避免用被优化的算法来验证自己；'),
    ]),
    # 生成过程第 2 条：表项坐标转换
    ('表项 T', [
        ('t', '表项 '),
        ('m', f_table_sym()),
        ('t', ' 先以 Jacobian 坐标链式累加，'
              '再用一次 Montgomery 批量求逆（替代 31 次单独求逆）转换为仿射坐标；'),
    ]),
]

# 正文字体（与原段落 run 属性保持一致：Times New Roman + 宋体，小四）
_BODY_RPR = ('<w:rPr><w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman"'
             ' w:cs="Times New Roman" w:eastAsia="宋体"/>'
             '<w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr>')


def text_run(s):
    return ('<w:r>' + _BODY_RPR +
            '<w:t xml:space="preserve">' + _esc(s) + '</w:t></w:r>')


def para_text(p):
    return ''.join(re.findall(r'<w:t(?:\s[^>]*)?>(.*?)</w:t>', p, re.S))


def build_paragraph(p, segs):
    """保留原 pPr，替换 run 内容"""
    m = re.search(r'<w:pPr>.*?</w:pPr>', p, re.S)
    ppr = m.group(0) if m else ''
    body = ''.join(text_run(payload) if kind == 't' else payload
                   for kind, payload in segs)
    return '<w:p>' + ppr + body + '</w:p>'


def rewrite_spans(xml):
    """在 xml 中定位目标段落并给出 (start, end, 新段落) 列表。

    关键：只报告目标段落的位置区间，**不重组全文**。
    早期实现用 re.findall 切出所有 <w:p> 再 join 回去，那会把 <w:tbl>
    内部的段落提升到顶层，导致 7 张表格的 <w:tbl>/<w:tc> 壳子被拆散丢失
    （表现为"表格全没了"，但段落数不变 322，肉眼很难发现）。
    正确做法是对原文做外科式定点替换，其余字节原样保留。
    """
    spans = []
    pos = 0
    # 逐个扫描 <w:p ...> ... </w:p>，记录其起止偏移
    for m in re.finditer(r'<w:p(?:\s[^>]*)?>.*?</w:p>|<w:p(?:\s[^>]*)?/>',
                         xml, re.S):
        p = m.group(0)
        txt = para_text(p)
        for key, segs in _REWRITES:
            if txt.startswith(key):
                spans.append((m.start(), m.end(), key, segs, txt))
                break
        pos = m.end()
    return spans


def apply_rewrites(xml):
    """按位置从后往前替换，避免前面的替换使后面偏移失效"""
    spans = rewrite_spans(xml)
    hits = len(spans)
    for start, end, key, segs, txt in reversed(spans):
        p = xml[start:end]
        print('  [命中] %-28s → %d 段片段（%d 个公式对象）'
              % (key, len(segs), sum(1 for k, _ in segs if k == 'm')))
        print('         原：%s' % txt[:60] + ('…' if len(txt) > 60 else ''))
        xml = xml[:start] + build_paragraph(p, segs) + xml[end:]
    return hits, xml


def report(xml):
    for _start, _end, key, segs, txt in rewrite_spans(xml):
        print('  [命中] %-28s → %d 段片段（%d 个公式对象）'
              % (key, len(segs), sum(1 for k, _ in segs if k == 'm')))
        print('         原：%s' % txt[:60] + ('…' if len(txt) > 60 else ''))
    return len(rewrite_spans(xml))


def rewrite_docx(src, dst):
    zin = zipfile.ZipFile(src, 'r')
    doc = zin.read('word/document.xml').decode('utf-8')

    head, body = doc.split('<w:body>', 1)
    body, tail = body.rsplit('</w:body>', 1)

    hits, new_body = apply_rewrites(body)
    if hits != len(_REWRITES):
        print('!! 只命中 %d/%d 段，拒绝写出（定位可能已失效）'
              % (hits, len(_REWRITES)))
        zin.close()
        return False

    new_body, _ = drop_title_todo(new_body)
    new_doc = head + '<w:body>' + new_body + '</w:body>' + tail

    # 结构护栏：表格/图片/单元格数量必须与原文一致，防止定点替换误伤
    for tag, label in (('<w:tbl>', '表格'), ('<w:tc>', '单元格'),
                       ('<w:drawing>', '图片')):
        n_old, n_new = doc.count(tag), new_doc.count(tag)
        if n_old != n_new:
            print('!! %s数量 %d → %d，拒绝写出' % (label, n_old, n_new))
            zin.close()
            return False
    print('  [护栏] 表格 %d / 单元格 %d / 图片 %d 均与原文一致 ✓'
          % (new_doc.count('<w:tbl>'), new_doc.count('<w:tc>'),
             new_doc.count('<w:drawing>')))

    shutil.copyfile(src, dst)
    # 重写 zip：保留其余部件字节不变
    ztmp = dst + '.tmp'
    zout = zipfile.ZipFile(ztmp, 'w', zipfile.ZIP_DEFLATED)
    for item in zin.infolist():
        data = zin.read(item.filename)
        if item.filename == 'word/document.xml':
            data = new_doc.encode('utf-8')
        zout.writestr(item, data)
    zout.close()
    zin.close()
    shutil.move(ztmp, dst)
    print('  已写出：%s' % dst)
    return True


def check(src):
    zin = zipfile.ZipFile(src, 'r')
    doc = zin.read('word/document.xml').decode('utf-8')
    body = doc.split('<w:body>', 1)[1].rsplit('</w:body>', 1)[0]
    hits = report(body)
    zin.close()
    print('\n命中 %d/%d 段' % (hits, len(_REWRITES)))
    return hits == len(_REWRITES)


def main():
    ap = argparse.ArgumentParser(description='2.1.7 公式 → OMML 真公式')
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--check', metavar='DOCX')
    g.add_argument('--apply', nargs=2, metavar=('IN', 'OUT'))
    g.add_argument('--inplace', metavar='DOCX')
    a = ap.parse_args()

    ok = False
    if a.check:
        ok = check(a.check)
    elif a.apply:
        ok = rewrite_docx(a.apply[0], a.apply[1])
    else:
        tmp = a.inplace + '.new'
        ok = rewrite_docx(a.inplace, tmp)
        if ok:
            shutil.move(tmp, a.inplace)
            print('  已原地替换：%s' % a.inplace)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
