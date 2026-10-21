#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
package_support.py — 生成复赛「作品相关支撑材料」压缩包（可复现 + 匿名性门禁）

用法：
    python scripts/package_support.py                # 构建并写入 dist/
    python scripts/package_support.py --check         # 只做门禁检查，不打包
    python scripts/package_support.py --out <目录>    # 指定输出目录（默认 dist/）

产物：
    dist/CACR2026XTP27G-2026密码竞赛复赛作品相关支撑材料.zip
    dist/CACR2026XTP27G-2026密码竞赛复赛作品相关支撑材料.zip.sha256

包内结构：
    01-完整仓库/     仓库快照（源码、脚本、文档、插图、实测证据）
    README-支撑材料说明.md
    MANIFEST.csv     包内全部文件的路径、大小、SHA-256
    SHA256SUMS.txt   同上（sha256sum 格式）

匿名性硬门槛（AGENTS.md §〇）：
  1. 打包排除：复赛通知附件/（名单与签字页）、.git/、AGENTS.md（内部笔记）；
  2. 报告本身（docs/06-*.md 与 docs/作品设计报告_*.docx）单独提交，不随包分发；
  3. 二进制文档（docx/pptx/xlsx）逐个检查 docProps：无作者/单位/最后修改者残留；
  4. 全文扫描邮箱、代码托管账号 URL、Windows 用户路径与个人身份词；
  5. 上述任一项不通过即中止打包（退出码 1）。

一致性约定：
  bin/ 下的体积、CT 代价、第三方基线三份报告与
  results/report-evidence-2026-10-17/raw/host-local/ 下的快照逐字节一致——
  它们就是提交报告引用的那一轮数据，避免包内出现两套互相矛盾的数字。
"""
import argparse
import base64
import csv
import hashlib
import os
import re
import shutil
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG_NAME = 'CACR2026XTP27G-2026密码竞赛复赛作品相关支撑材料'
REPO_DIR = '01-完整仓库'
EVID = os.path.join('results', 'report-evidence-2026-10-17')

# ---------------------------------------------------------------- 打包清单
TOP_INCLUDE = ('apps', 'bin', 'crypto', 'dtlcp', 'embedded', 'results', 'scripts',
                'tests', 'docs', '.github', 'Makefile', 'README.md',
                '.gitattributes')

# 仓库根/子目录中一律不进入包的名字
DENY_DIRS = {'.git', '复赛通知附件', 'obj', '__pycache__', '.idea', '.vscode',
             'node_modules', 'dist', '.workbuddy', '报告润色', 'gmssl', 'gmssl-src',
             'delivery', 'feishu', 'edit8', 'edit9', 'review', 'user_edit',
             'ch3_render', 'feishu_render', 'render_check', 'report_render',
             'ref_figs', 'formula_test', 'acl-fix'}

# 报告生成链路的临时脚本（报告单独提交，故不随包分发）
DENY_FILES = {
    'AGENTS.md',
    'package_support.py',      # 打包工具自身属内部工具，不随包分发
    'anon_tokens.local.txt',
    'build_report_docx.py', 'build_ch3_doc.py', 'docx_crossref.py',
    'docx_omml_formulas.py', 'patch_ch2.py', 'polish_ch2.py',
    'splice_ch3.py', 'splice_ch345.py', '_ch3_block.txt',
}

# docs/ 下不随包分发（报告单独提交）
# 宣发改动清单.md 是报告改写的内部工作记录（含原文/新文对照），同样不属于支撑材料。
DENY_DOCS_RE = re.compile(r'^(06-.*\.md|作品设计报告_.*\.docx|CACR2026.*\.docx|宣发改动清单\.md|07-分工与推进计划\.md)$')

# 明确排除的子树（相对仓库根，前缀匹配）
DENY_PATHS = (
    'docs/figures/ppt/pdf/',            # LibreOffice 渲染的 PDF 中间产物
    'docs/figures/ppt/png/',            # 各图渲染结果（报告已内嵌；约 5.6 MB，对复核无作用）
    'docs/figures/ppt/pptx/',           # 可编辑 PPT 源（约 1.7 MB；需要时由绘图脚本重建）
    'docs/figures/ppt/png/obsolete/',   # 被替换掉的旧版插图
    'docs/figures/icons/png2/',         # 彩色图标位图（9 色系 × 49 图标）：重绘插图的
                                        # 中间产物，可由 icons/*.svg 经 make_icons_multi.py
                                        # 完整重建；icons/png/（框架图实际依赖）保留
)

# bin/ 下只保留报告类产物，排除可执行文件与会话缓存
BIN_KEEP = {'bench_results.md', 'ct_cost.md', 'demo_scenes.log', 'power_results.md',
            'size_report.md', 'thirdparty_compare.md'}

# bin/ 必须与 results 快照逐字节一致的三份
BIN_SYNC = ('size_report.md', 'ct_cost.md', 'thirdparty_compare.md')

# ---------------------------------------------------------------- 匿名性门禁
# 注意：本脚本**不得**内置任何真实姓名/账号，否则它自己就成了泄漏源。
# 维护者把需要排查的具体身份词写进 scripts/anon_tokens.local.txt（本地文件，
# 不入版本库、不随包分发）；这里只保留与身份无关的通用规则。
LOCAL_TOKEN_FILE = 'scripts/anon_tokens.local.txt'

# 通用角色词 + 取值：标签后必须紧跟冒号与一个姓名/单位样式的取值才算命中，
# 避免把「队长账号在竞赛平台提交」这类流程描述误判为署名。
_NAMEISH = r'([一-龥]{2,4}(?:大学|学院|研究所|实验室|公司|中心|研究院)?)'
ROLE_VALUE_PATTERNS = {
    '指导教师': re.compile(r'(指导教师|指导老师|指导教师姓名|指导老师姓名)\s*[:：]\s*' + _NAMEISH),
    '参赛单位': re.compile(r'(参赛单位|所在单位|单位名称|所属单位)\s*[:：]\s*' + _NAMEISH),
    '人员署名': re.compile(r'(领队|队长|队员|参赛者姓名|学生姓名|作者|编写人|制作者)\s*[:：]\s*' + _NAMEISH),
    '版权': re.compile(r'(版权|Copyright|©)\s*[:：]\s*' + _NAMEISH),
    '联系方式': re.compile(r'(联系电话|手机号|手机号码|邮箱|Email|E-mail)\s*[:：]\s*([^\s|。，,；;]{2,32})'),
}

IDENT_PATTERNS = {
    '邮箱': re.compile(r'[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'),
    '代码托管 URL': re.compile(r'(?i)(github|gitlab|gitee|gitcode)\.com[/:][A-Za-z0-9_.-]+'),
    'Windows 用户路径': re.compile(r'[A-Za-z]:\\Users\\[A-Za-z0-9_.-]+'),
}
OFFICE_EXT = ('.docx', '.pptx', '.xlsx')
OFFICE_META_TAGS = ('dc:creator', 'cp:lastModifiedBy', 'Company', 'Manager',
                    'cp:lastPrinted', 'KSOTemplateDocerSaveRecord')

TEXT_EXT = ('.c', '.h', '.py', '.sh', '.md', '.csv', '.log', '.json', '.txt', '.ld',
            '.svg', '.yml', '.yaml', '.ini', '.cfg', '.gitignore', '.gitattributes', '')


def load_local_tokens():
    """读取本地身份词表（不存在则为空）。该文件不入版本库、不随包分发。"""
    path = os.path.join(ROOT, LOCAL_TOKEN_FILE.replace('/', os.sep))
    if not os.path.isfile(path):
        return []
    toks = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if line:
                toks.append(line)
    return toks



def use_utf8_stdout():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------- 收集文件
def collect(repo_root):
    """返回 [(相对路径, 绝对路径)]，按相对路径排序。"""
    out = []
    for entry in TOP_INCLUDE:
        src = os.path.join(repo_root, entry)
        if not os.path.exists(src):
            continue
        if os.path.isfile(src):
            if entry in DENY_FILES:
                continue
            out.append((entry, src))
            continue
        for dp, dn, fn in os.walk(src):
            rel_dir = os.path.relpath(dp, repo_root).replace('\\', '/')
            parts = set(rel_dir.split('/'))
            if parts & DENY_DIRS:
                dn[:] = []
                continue
            dn[:] = [d for d in dn if d not in DENY_DIRS]
            for f in sorted(fn):
                rel = (rel_dir + '/' + f) if rel_dir != '.' else f
                base = os.path.basename(rel)
                if base in DENY_FILES:
                    continue
                if rel.startswith(DENY_PATHS):
                    continue
                if rel_dir == 'bin' and base not in BIN_KEEP:
                    continue
                if rel_dir == 'docs' and DENY_DOCS_RE.match(base):
                    continue
                if rel_dir.startswith('docs/') and DENY_DOCS_RE.match(base):
                    continue
                if base.endswith(('.exe', '.pyc', '.o', '.su', '.bin')):
                    continue
                if base in ('sess_gw.bin', 'sess_sn.bin'):
                    continue
                out.append((rel, os.path.join(dp, f)))
    out.sort()
    return out


def sync_bin_with_snapshot(repo_root, files):
    """bin/ 的三份报告必须与 results 快照一致（就地同步工作区）。"""
    synced = []
    for name in BIN_SYNC:
        snap = os.path.join(repo_root, EVID, 'raw', 'host-local', name)
        dst = os.path.join(repo_root, 'bin', name)
        if not os.path.isfile(snap):
            continue
        if not os.path.isfile(dst) or sha256(snap) != sha256(dst):
            shutil.copyfile(snap, dst)
            synced.append(name)
    return synced


# ---------------------------------------------------------------- 门禁
def gate_identity(stage_root, files):
    problems = []
    local_tokens = load_local_tokens()
    for rel, full in files:
        low = rel.lower()
        if low.endswith(OFFICE_EXT):
            problems += check_office_meta(rel, full)
            continue
        ext = os.path.splitext(rel)[1].lower()
        if ext not in TEXT_EXT:
            continue
        try:
            raw = open(full, 'rb').read()
        except OSError as e:
            problems.append('%s: 读取失败 %s' % (rel, e))
            continue
        if b'\x00' in raw[:4096]:
            continue
        text = raw.decode('utf-8', 'replace')
        for t in local_tokens:
            if t in text:
                problems.append('%s: 含本地身份词 %r' % (rel, t))
        for name, rx in IDENT_PATTERNS.items():
            m = rx.search(text)
            if m:
                problems.append('%s: 含%s %r' % (rel, name, m.group(0)))
        for name, rx in ROLE_VALUE_PATTERNS.items():
            for m in rx.finditer(text):
                problems.append('%s: 可能带出%s信息 %r' % (rel, name, m.group(0)[:40]))
    return problems


def check_office_meta(rel, full):
    problems = []
    try:
        z = zipfile.ZipFile(full)
    except Exception as e:
        return ['%s: 无法打开（%s）' % (rel, e)]
    with z:
        for part in ('docProps/core.xml', 'docProps/app.xml', 'docProps/custom.xml'):
            if part not in z.namelist():
                continue
            data = z.read(part).decode('utf-8', 'replace')
            for tag in OFFICE_META_TAGS:
                m = re.search(r'<%s(?:\s[^>]*)?>(.*?)</%s>' % (tag, tag), data, re.S)
                if m and m.group(1).strip():
                    problems.append('%s::%s/%s 残留 %r'
                                    % (rel, part, tag, m.group(1).strip()[:60]))
            if 'hdid' in data or 'userId' in data:
                problems.append('%s::%s 含账号标识(hdid/userId)' % (rel, part))
            for prop in re.findall(r'<property\b.*?</property>', data, re.S):
                v = re.search(r'<vt:lpwstr>(.*?)</vt:lpwstr>', prop, re.S)
                if not v:
                    continue
                try:
                    dec = base64.b64decode(v.group(1)).decode('utf-8', 'replace')
                except Exception:
                    continue
                if 'hdid' in dec or 'userId' in dec:
                    problems.append('%s::%s 含 base64 账号标识' % (rel, part))
    return problems


def gate_consistency(files):
    """包内三份报告的数值必须与提交报告一致（关键数字抽样）。"""
    problems = []
    idx = {rel: full for rel, full in files}
    pre = REPO_DIR + '/'
    expect = {
        'bin/size_report.md': ['29824', '3216', '20624'],
        'bin/ct_cost.md': ['1.140', '1.099'],
        'bin/thirdparty_compare.md': ['5.00', '2.04', '3.29', '1.95'],
        'bin/bench_results.md': ['1547326', '173824', '534103', '290926'],
        'bin/power_results.md': ['18857', '5708', '3.30'],
    }
    for rel, keys in expect.items():
        full = idx.get(pre + rel)
        if not full:
            problems.append('缺少 %s' % (pre + rel))
            continue
        text = open(full, encoding='utf-8', errors='replace').read()
        for k in keys:
            if k not in text:
                problems.append('%s 未含报告数值 %r' % (pre + rel, k))
    # bin/ 与 results 快照逐字节一致
    for name in BIN_SYNC:
        a = idx.get(pre + 'bin/' + name)
        b = idx.get(pre + os.path.join(EVID, 'raw', 'host-local', name).replace('\\', '/'))
        if a and b and sha256(a) != sha256(b):
            problems.append('bin/%s 与 results 快照不一致' % name)
    return problems


# ---------------------------------------------------------------- 说明文件
README_TEMPLATE = """# 作品相关支撑材料（不含作品设计报告）

作品编号：**CACR2026XTP27G**
作品题目：资源受限物联网终端轻量级国密算法优化与 DTLCP 国密改造实现

本压缩包包含复赛作品的**仓库源码、构建/评测脚本、测试日志与实测数据结果**。
按提交要求，**作品设计报告本身不在本包内**，报告单独提交。

## 目录

```
01-完整仓库/                可直接构建、测试与复核的仓库快照
  ├─ crypto/ dtlcp/ apps/ tests/ embedded/   算法库、协议栈、示例、测试、嵌入式工程
  ├─ scripts/                                构建与评测脚本
  ├─ docs/                                   01–05、07–10 设计/测试/采购/操作文档（不含报告 06）
  ├─ docs/figures/                            插图绘图脚本、图标源与公式源（成品 PNG/可编辑 PPTX 未附带）
  ├─ .github/workflows/ci.yml                 持续集成定义（Ubuntu + Windows 双平台）
  ├─ results/report-evidence-2026-10-17/      报告引用的全部实测证据
  │    ├─ raw/board/*.json                    F103 DWT 原始结果块
  │    ├─ raw/host-local/                     主机同轮基准与 GmSSL 对照原始输出
  │    ├─ logs/                               CI、板测、W5500、内存门禁、固件尺寸日志
  │    └─ tables/*.csv                        报告用数据表（cycles/协议/功耗/内存/claims）
  ├─ bin/                                     报告引用的那一轮实测输出（Markdown 日志与功耗结果）
  └─ Makefile / README.md                   构建入口与项目说明

MANIFEST.csv                 包内全部文件的路径、大小、SHA-256
SHA256SUMS.txt               包内全部文件的 SHA-256（sha256sum 格式）
```

## 数据口径

- 算法周期数：F103 DWT CYCCNT 真机实测（`results/.../tables/board_cycles.csv`）。
- 协议时延/字节：F103 + W5500 真机实测，PC 日志与板端 SWD 结果块互证。
- 功耗：由 F103 DWT 实测周期数与板端供电参数计算
  （3.3 V、100 µA/MHz、8 MHz、非 SM2 固定开销 66 µJ），
  输出 `results/.../tables/power_measured.csv`：
  FULL 握手 18857 → 5708 µJ（3.30×），RESUME/PSK 各 66 µJ。
- 主机 cycles：同一进程、同一轮输出内可比；跨运行绝对值不可比，报告只使用同轮比值。
- 插图：`docs/figures/ppt/fig_*.py` 是绘图脚本（`figlib.py` 图形库、`render_figs.py` 渲染入口），
  `docs/figures/icons/` 是图标源（SVG + 黑版 PNG + 上色脚本），`docs/figures/formulas/` 是公式源图；
  各图渲染结果（`docs/figures/ppt/png/`）与可编辑 PowerPoint 源（`docs/figures/ppt/pptx/`）
  属报告附件性质、不影响构建与数据复核，合计约 7 MB，本包未附带；
  需要时按 `docs/figures/ppt/README.md` 用脚本 + LibreOffice 重新生成即可。
- `bin/` 下的体积、常量时间代价、第三方基线三份报告与
  `results/.../raw/host-local/` 下的同名快照**逐字节一致**，即提交报告引用的那一轮数据；
  其它 gcc 版本上重跑同一脚本，绝对字节数会有 <0.3% 的浮动，比值口径不变。

## 快速复核

```bash
cd 01-完整仓库

# 1) 由 DWT 实测周期数与板端参数重算功耗
python scripts/power_report.py --stdout

# 2) 正确性（需 bash + gcc；Windows 用 Git Bash）
bash scripts/run_tests.sh

# 3) 基线与外部对照（可选）
bash scripts/benchmark.sh
bash scripts/ct_cost.sh
bash scripts/thirdparty_bench.sh

# 4) 板级预检（宿主仿真 + 语法 + 32KB 链接证明）
bash scripts/check_embedded.sh

# 5) 数据一致性门禁（报告单独提交，未随包分发时相关项显示为 skip）
python scripts/check_report_data.py
```

说明：`scripts/check_report_data.py` 是维护期一致性门禁。报告单独提交、不在本包内，
因此“报告正文/报告 DOCX”相关检查项会显示为 `[skip]` 而非失败；
把提交用报告 DOCX 放到 `docs/` 下、并保留 `docs/06-作品设计报告.md` 后，
这些检查项会自动转为逐条 `[ok]` 核对。

## 完整性校验

```bash
# Linux / Git Bash
sha256sum -c SHA256SUMS.txt
# 或按 MANIFEST.csv 逐文件核对
python - <<'PY'
import csv, hashlib
for row in csv.DictReader(open('MANIFEST.csv', encoding='utf-8')):
    h = hashlib.sha256(open(row['path'], 'rb').read()).hexdigest()
    assert h == row['sha256'], row['path']
print('all files OK')
PY
```

## 匿名性

包内文本已扫描，不含单位、姓名、昵称、邮箱或代码托管账号；
Office 文档属性（作者/最后修改者/公司等）已清理。
打包过程由 `scripts/package_support.py` 自动执行以上检查，任一项不通过即中止打包。
"""


# ---------------------------------------------------------------- 主流程
def build(stage, repo_root, files):
    repo_stage = os.path.join(stage, REPO_DIR)
    for rel, full in files:
        dst = os.path.join(repo_stage, rel.replace('/', os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(full, dst)
    with open(os.path.join(stage, 'README-支撑材料说明.md'), 'w', encoding='utf-8',
              newline='\n') as f:
        f.write(README_TEMPLATE)


def write_manifest(stage):
    entries = []
    for dp, dn, fn in os.walk(stage):
        for f in fn:
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, stage).replace('\\', '/')
            if rel in ('MANIFEST.csv', 'SHA256SUMS.txt'):
                continue
            entries.append((rel, os.path.getsize(full), sha256(full)))
    entries.sort()
    with open(os.path.join(stage, 'MANIFEST.csv'), 'w', encoding='utf-8',
              newline='\n') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['path', 'bytes', 'sha256'])
        for rel, size, h in entries:
            w.writerow([rel, size, h])
    with open(os.path.join(stage, 'SHA256SUMS.txt'), 'w', encoding='utf-8',
              newline='\n') as f:
        for rel, size, h in entries:
            f.write('%s  %s\n' % (h, rel))
    return entries


def main():
    use_utf8_stdout()
    ap = argparse.ArgumentParser(description='打包复赛「作品相关支撑材料」')
    ap.add_argument('--out', default=os.path.join(ROOT, 'dist'), help='输出目录')
    ap.add_argument('--check', action='store_true', help='只做门禁检查，不打包')
    args = ap.parse_args()

    print('== 0. 同步 bin/ 与 results 快照 ==')
    synced = sync_bin_with_snapshot(ROOT, None)
    print('   同步 %d 个文件: %s' % (len(synced), ', '.join(synced) if synced else '（已一致）'))

    files = collect(ROOT)
    print('== 1. 收集文件 ==')
    print('   %d 个文件' % len(files))

    stage = tempfile.mkdtemp(prefix='pkg_support_')
    try:
        build(stage, ROOT, files)
        # 以暂存区为对象做门禁
        staged = []
        for dp, dn, fn in os.walk(stage):
            for f in fn:
                full = os.path.join(dp, f)
                rel = os.path.relpath(full, stage).replace('\\', '/')
                if rel in ('README-支撑材料说明.md',):
                    continue
                staged.append((rel, full))
        staged.sort()

        print('== 2. 匿名性门禁 ==')
        problems = gate_identity(stage, staged)
        if problems:
            print('   [FAIL] %d 项：' % len(problems))
            for p in problems[:40]:
                print('     - %s' % p)
            return 1
        print('   [ok] 无身份词 / 邮箱 / 托管账号 / 用户路径；Office 属性干净')

        print('== 3. 与提交报告的数值一致性 ==')
        problems = gate_consistency(staged)
        if problems:
            print('   [FAIL] %d 项：' % len(problems))
            for p in problems[:40]:
                print('     - %s' % p)
            return 1
        print('   [ok] bin/ 与 results 快照一致，关键数值与报告一致')

        if args.check:
            print('\n门禁全部通过（--check：未打包）')
            return 0

        print('== 4. 生成 MANIFEST / SHA256SUMS ==')
        entries = write_manifest(stage)
        print('   %d 条' % len(entries))

        print('== 5. 压缩 ==')
        os.makedirs(args.out, exist_ok=True)
        zpath = os.path.join(args.out, PKG_NAME + '.zip')
        if os.path.exists(zpath):
            os.remove(zpath)
        with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for rel, size, h in entries:
                z.write(os.path.join(stage, rel.replace('/', os.sep)),
                        PKG_NAME + '/' + rel)
            for extra in ('MANIFEST.csv', 'SHA256SUMS.txt'):
                z.write(os.path.join(stage, extra), PKG_NAME + '/' + extra)
        zh = sha256(zpath)
        with open(zpath + '.sha256', 'w', encoding='utf-8', newline='\n') as f:
            f.write('%s  %s.zip\n' % (zh, PKG_NAME))
        print('   %s  (%.2f MB)' % (zpath, os.path.getsize(zpath) / 1048576.0))
        print('   SHA-256 %s' % zh)
        return 0
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == '__main__':
    raise SystemExit(main())
