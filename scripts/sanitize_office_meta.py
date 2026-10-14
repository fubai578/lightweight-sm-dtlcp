#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sanitize_office_meta.py — 清空 Office 文档属性里的可识别信息（匿名评审硬门槛）

用法：
    python scripts/sanitize_office_meta.py <file.docx|file.pptx|file.xlsx> [...]
    python scripts/sanitize_office_meta.py --check <file> ...     # 只检查不修改

清理字段（存在于 docProps/core.xml 与 docProps/app.xml）：
    dc:creator / cp:lastModifiedBy        —— Office 会写入系统用户名或昵称
    Company / Manager                     —— app.xml 里的单位与管理者
    cp:lastPrinted / dc:title(submission) —— 一并清空，避免指纹式残留

为什么必须做：本项目实际出现过两处——框架图 pptx 的 lastModifiedBy 是昵称、
飞书导出的报告 docx 的 creator/lastModifiedBy 是两个不同的个人标识。
**每次用 Office/WPS 打开另存后都会重新写入**，所以提交前必须再跑一次。

不改动正文、不重排压缩以外的任何内容（只替换 docProps 下的对应字段）。
"""
import os
import re
import shutil
import sys
import zipfile

# (部件, 标签名, 替换形式)  —— 标签可能是 <tag/> 或 <tag>值</tag>
FIELDS = {
    'docProps/core.xml': ['dc:creator', 'cp:lastModifiedBy', 'cp:lastPrinted', 'dc:title'],
    'docProps/app.xml':  ['Company', 'Manager'],
}


def scan(path):
    """返回 {字段: 值} —— 只报告非空且像"内容"的字段（排除模板默认值）。"""
    found = {}
    with zipfile.ZipFile(path) as z:
        for part, tags in FIELDS.items():
            try:
                t = z.read(part).decode('utf-8', 'ignore')
            except KeyError:
                continue
            for tag in tags:
                m = re.search(r'<%s(?:\s[^>]*)?>(.*?)</%s>' % (tag, tag), t, re.S)
                if m and m.group(1).strip():
                    found['%s/%s' % (part, tag)] = m.group(1).strip()
    return found


def sanitize(path):
    """把字段值清空；返回被清空的字段列表。"""
    tmp = path + '.sanitize-tmp'
    cleared = []
    with zipfile.ZipFile(path) as zin, zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename in FIELDS:
                t = data.decode('utf-8', 'ignore')
                for tag in FIELDS[item.filename]:
                    new_t, n = re.subn(
                        r'<%s(?:\s[^>]*)?>.*?</%s>' % (tag, tag),
                        '<%s></%s>' % (tag, tag), t, flags=re.S)
                    t = new_t
                    if n:
                        cleared.append('%s/%s' % (item.filename, tag))
                data = t.encode('utf-8')
            zout.writestr(item, data)
    shutil.move(tmp, path)
    return cleared


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    check_only = '--check' in argv
    if not args:
        print(__doc__)
        return 2
    rc = 0
    for path in args:
        if not os.path.isfile(path):
            print('跳过（不存在）:', path)
            rc = 1
            continue
        found = scan(path)
        if check_only:
            if found:
                print('[!] %s 含可识别字段：' % path)
                for k, v in found.items():
                    print('      %s = %r' % (k, v))
                rc = 1
            else:
                print('[ok] %s 无残留的作者/单位字段' % path)
            continue
        cleared = sanitize(path)
        print('%s：清理 %d 个字段' % (path, len(cleared)) +
              ('（%s）' % ', '.join(sorted(set(cleared))) if cleared else ''))
        left = scan(path)
        if left:
            print('      [警告] 仍有残留:', left)
            rc = 1
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
