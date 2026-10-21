#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
render_figs.py — 把 docs/figures/ppt/pptx/*.pptx 批量渲染为 docs/figures/ppt/png/*.png

流程：LibreOffice → PDF（矢量） → PyMuPDF 按 2.5× 缩放栅格化（约 240 dpi，
A4 正文栏宽下仍清晰）。不用 soffice 直接转 PNG——它只出 96 dpi 的位图。

用法：
    python docs/figures/ppt/render_figs.py [单个文件名关键词]
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PPTX_DIR = os.path.join(HERE, 'pptx')
PNG_DIR = os.path.join(HERE, 'png')
SOFFICE = r'C:\Program Files\LibreOffice\program\soffice.exe'
SCALE = 5.0 / 2.0          # PDF 点 → 像素的倍数（≈240 dpi）


def render(pptx_path):
    name = os.path.splitext(os.path.basename(pptx_path))[0]
    out_png = os.path.join(PNG_DIR, name + '.png')
    with tempfile.TemporaryDirectory() as td:
        # 独立 user profile：避免与已运行的 LibreOffice 实例抢锁
        cmd = [SOFFICE, '-env:UserInstallation=file:///' + td.replace('\\', '/'),
               '--headless', '--norestore', '--convert-to', 'pdf',
               '--outdir', td, pptx_path]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        pdf = os.path.join(td, name + '.pdf')
        if not os.path.isfile(pdf):
            print('  !! 转换失败：%s\n%s' % (name, r.stdout[-400:] + r.stderr[-400:]))
            return False
        import pymupdf
        doc = pymupdf.open(pdf)
        page = doc[0]
        pix = page.get_pixmap(matrix=pymupdf.Matrix(SCALE, SCALE), alpha=False)
        pix.save(out_png)
        doc.close()
    print('  → %s  (%d×%d)' % (os.path.relpath(out_png, HERE), pix.width, pix.height))
    return True


def main():
    os.makedirs(PNG_DIR, exist_ok=True)
    pats = sys.argv[1:] or ['*']
    files = sorted(f for p in pats for f in glob.glob(os.path.join(PPTX_DIR, p + '.pptx')) if p != '*'
                   or True)
    files = sorted(set(files))
    if not files:
        print('没有待渲染的 pptx')
        return 1
    ok = 0
    for f in files:
        print('[渲染]', os.path.basename(f))
        if render(f):
            ok += 1
    print('完成 %d/%d' % (ok, len(files)))
    return 0 if ok == len(files) else 1


if __name__ == '__main__':
    sys.exit(main())
