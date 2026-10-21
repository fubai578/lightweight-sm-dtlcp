#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_report_data.py — 校验仓库实测数据与《作品设计报告》一致（一键核对）

检查内容：
  1. 功耗按 F103 DWT 实测周期数与板端实测动态电流换算，结果 == power_measured.csv
     （FULL 18857/5708 µJ、3.30×；RESUME/PSK 66 µJ）。
  2. 报告、docs/06、docs/04、README 中出现的关键实测数值与报告一致。
  3. 仓库中不再保留“模型估算/非实测/power_model/power_estimate”等预估数据痕迹。
  4. 复现入口存在：scripts/power_report.py、Makefile 的 power / check-data 目标、
     scripts/benchmark.sh 不再输出功耗模型表。

用法：
    python scripts/check_report_data.py
    make check-data
退出码：0 = 全部通过；1 = 有不一致项。
"""
import csv
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SNAPSHOT = os.path.join(ROOT, 'results', 'report-evidence-2026-10-17')
TABLES = os.path.join(SNAPSHOT, 'tables')
REPORT_CANDIDATES = (
    'docs/作品设计报告_修订13.docx',
    'docs/作品设计报告_修订12.docx',
    'docs/作品设计报告_修订11.docx',
    'docs/作品设计报告_修订10.docx',
    'docs/作品设计报告_修订9.docx',
)

# 报告单独提交，不随支撑材料包分发；包内缺报告时相关检查应跳过而非判失败
OPTIONAL_DOC_FILES = ('docs/06-作品设计报告.md',)

# 报告中的关键实测数值（与提交报告 3.4.6 / 表 3-4 / 表 3-6 / 表 3-7 一致）
REPORT_TOKENS = (
    ('功耗 REF', '18857'), ('功耗 MCU', '5708'), ('功耗倍数', '3.30×'),
    ('功耗条件', '3.3 V'), ('功耗电流', '100 µA/MHz'),
    ('FULL 握手', '2274.97'), ('RESUME 握手', '58.04'),
    ('PSK 握手', '74.77'), ('0-RTT 握手', '92.65'),
    ('RAM 上下文', '5248'), ('RAM 静态区', '4104'), ('最长栈', '4872'),
    ('RAM 合计', '14224'), ('RAM 余量', '6256'), ('32KiB text', '29720'),
    ('全功能 text', '29824'), ('全功能 RAM', '4128'), ('最小裁剪 text', '20624'),
    ('差分指纹', '64a5385d17ad44be141970496e5b70dca47226fd387485cc4e2ae3bdf5b49eaf'),
    ('F103 keygen', '5.44'), ('F103 sign', '4.07'),
    ('F103 verify', '3.24'), ('F103 ECDH', '2.15'),
    ('主机 keygen', '8.90'), ('comb 净收益', '2.57'),
    ('对端表验签', '1.84'), ('CT 基点代价', '1.140'),
)

# 关键文档（与报告 / 数据一致性检查的落点）
DOC_FILES = (
    'docs/06-作品设计报告.md',
    'docs/04-测试与性能对比报告.md',
    'README.md',
    'embedded/README.md',
    'docs/01-实现路径规划.md',
    'docs/02-系统设计说明.md',
    'docs/03-固件安全改造规范.md',
    'docs/05-硬件采购清单.md',
    'docs/08-硬件测试操作指南.md',
    'docs/09-F103上板操作指南.md',
    'results/report-evidence-2026-10-17/README.md',
    'results/report-evidence-2026-10-17/CURRENT_RESULTS.md',
    'results/report-evidence-2026-10-17/ENVIRONMENT.md',
)

# 已删除的预估数据痕迹（任何一处出现都视为失败）
BANNED = (
    '估算', '预估', '模型估算', '能量模型', '非实测',
    'power_model', 'power_estimate', 'replace_with_measured',
    '0.30 nJ', '26 MHz', '2856', '534 µJ', '60 µJ',
    '待板上验证', '待实测', '假想参数',
)

FIX = '修复：按提示修改对应文件；功耗数值请运行 python scripts/power_report.py 重算。'


def read_text(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def read_docx_text(path):
    with zipfile.ZipFile(path) as z:
        xml = z.read('word/document.xml').decode('utf-8', 'ignore')
    xml = re.sub(r'<w:tab[^>]*/>', '\t', xml)
    xml = re.sub(r'<w:br[^>]*/>', '\n', xml)
    text = re.sub(r'<[^>]+>', '', xml)
    text = (text.replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>')
                .replace('&quot;', '"').replace('&apos;', "'"))
    return text


def load_power():
    with open(os.path.join(TABLES, 'power_measured.csv'), encoding='utf-8') as f:
        return {row['scenario']: row for row in csv.DictReader(f)}



def load_board_cycles():
    data = {}
    with open(os.path.join(TABLES, 'board_cycles.csv'), encoding='utf-8') as f:
        for row in csv.DictReader(f):
            data.setdefault(row['variant'], {})[row['operation']] = int(row['cycles_per_op'])
    return data


class Checker:
    def __init__(self):
        self.failures = []
        self.passes = 0
        self.skips = 0

    def check(self, cond, name, detail=''):
        if cond:
            self.passes += 1
            print('  [ok]   %s' % name)
        else:
            self.failures.append((name, detail))
            print('  [FAIL] %s%s' % (name, (' — ' + detail) if detail else ''))

    def skip(self, name, reason=''):
        """被检查对象不在本包内（如单独提交的报告）——不计入失败。"""
        self.skips += 1
        print('  [skip] %s%s' % (name, (' — ' + reason) if reason else ''))

    def summary(self):
        print()
        if self.failures:
            print('结果：%d 项通过，%d 项失败%s' % (
                self.passes, len(self.failures),
                ('，%d 项跳过' % self.skips) if self.skips else ''))
            for name, detail in self.failures:
                print('  - %s%s' % (name, ('：' + detail) if detail else ''))
            print(FIX)
            return 1
        print('结果：全部 %d 项通过%s，数据与报告一致。' % (
            self.passes, ('，%d 项跳过' % self.skips) if self.skips else ''))
        return 0


def _use_utf8_stdout():
    """Windows GBK 控制台打印 µ 等非 GBK 字符会抛 UnicodeEncodeError，
    这里把 stdout/stderr 统一切到 UTF-8（Linux 下为 no-op）。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass


def main():
    _use_utf8_stdout()
    c = Checker()

    print('== 1. 功耗数据 ==')
    power = load_power()
    cycles = load_board_cycles()
    ops = ('sm2_keygen', 'sm2_sign', 'sm2_ecdh')
    ref_cyc = sum(cycles['REF'][op] for op in ops)
    mcu_cyc = sum(cycles['MCU'][op] for op in ops)
    full = power['FULL_handshake']
    c.check(int(full['ref_cycles']) == ref_cyc == 56942495,
            'FULL 板端 REF 周期数 = 56942495', str(ref_cyc))
    c.check(int(full['mcu_cycles']) == mcu_cyc == 17095517,
            'FULL 板端 MCU 周期数 = 17095517', str(mcu_cyc))
    v = float(full['voltage_v'])
    f_hz = float(full['f_clk_mhz']) * 1e6
    iref = float(full['current_ref_ua'])
    imcu = float(full['current_mcu_ua'])
    c.check(abs(iref - 802.8) < 0.05 and abs(imcu - 809.4) < 0.05,
            '板端实测电流 = 802.8 / 809.4 µA', '%.1f / %.1f' % (iref, imcu))
    c.check(full['evidence'].startswith('measured_f103'),
            'FULL 能耗证据为板端实测', full['evidence'])
    ref_uj = v * (iref / 1e6) * (ref_cyc / f_hz) * 1e6
    mcu_uj = v * (imcu / 1e6) * (mcu_cyc / f_hz) * 1e6
    c.check(full['ref_energy_uj'] == '%.0f' % ref_uj,
            'FULL REF 能耗 = 18857 µJ', full['ref_energy_uj'])
    c.check(full['opt_energy_uj'] == '%.0f' % mcu_uj,
            'FULL MCU 能耗 = 5708 µJ', full['opt_energy_uj'])
    c.check(full['reduction'] == '%.2f' % (ref_uj / mcu_uj),
            'FULL 降低倍数 = 3.30×', full['reduction'])
    for key in ('RESUME_handshake', 'PSK_handshake'):
        r = power[key]
        c.check(r['ref_energy_uj'] == '66' and r['opt_energy_uj'] == '66',
                '%s 固定开销 = 66 µJ' % key, str(r))

    print('== 2. 报告中的实测数值 ==')
    report_path = None
    for name in REPORT_CANDIDATES:
        p = os.path.join(ROOT, name)
        if os.path.isfile(p):
            report_path = p
            break
    if report_path is None:
        print('  [warn] 未找到报告 DOCX，跳过报告正文核对（仅仓库内文件仍需一致）')
    else:
        report_text = read_docx_text(report_path)
        print('  报告：%s' % os.path.relpath(report_path, ROOT))
        for label, token in REPORT_TOKENS:
            c.check(token in report_text, '报告含 %s（%s）' % (label, token),
                    '报告未找到该数值')

    print('== 3. 文档中的功耗口径 ==')
    docs = {}
    for rel in DOC_FILES:
        p = os.path.join(ROOT, rel)
        if not os.path.isfile(p):
            if rel in OPTIONAL_DOC_FILES:
                c.skip('存在 %s' % rel, '报告单独提交，不随支撑材料包分发')
            else:
                c.check(False, '存在 %s' % rel, '文件缺失')
            continue
        docs[rel] = read_text(p)
    for rel in ('docs/06-作品设计报告.md', 'docs/04-测试与性能对比报告.md', 'README.md'):
        if rel not in docs:
            continue
        t = docs[rel]
        c.check('18857' in t and '5708' in t, '%s 含 18857/5708 µJ' % rel)

    print('== 4. 预估数据痕迹 ==')
    for rel, t in docs.items():
        hits = [b for b in BANNED if b in t]
        c.check(not hits, '%s 无预估数据痕迹' % rel, '仍含：%s' % '、'.join(hits))
    for rel in ('scripts/benchmark.sh', 'scripts/power_report.py'):
        p = os.path.join(ROOT, rel)
        t = read_text(p) if os.path.isfile(p) else ''
        hits = [b for b in BANNED if b in t]
        c.check(not hits, '%s 无预估数据痕迹' % rel, '仍含：%s' % '、'.join(hits))

    print('== 5. 复现与检查入口 ==')
    for rel in ('scripts/power_report.py', 'results/report-evidence-2026-10-17/tables/power_measured.csv',
                'bin/power_results.md'):
        c.check(os.path.isfile(os.path.join(ROOT, rel)), '存在 %s' % rel)
    for rel in ('scripts/power_model.py',
                'results/report-evidence-2026-10-17/tables/power_estimate.csv'):
        c.check(not os.path.isfile(os.path.join(ROOT, rel)), '已删除 %s' % rel)
    makefile = read_text(os.path.join(ROOT, 'Makefile'))
    c.check('power_report.py' in makefile and 'check-data' in makefile,
            'Makefile 提供 power / check-data 目标')
    bench = read_text(os.path.join(ROOT, 'scripts', 'benchmark.sh'))
    c.check('power_model' not in bench and '功耗估算' not in bench,
            'benchmark.sh 不再输出功耗模型表')
    if os.path.isfile(os.path.join(ROOT, 'bin', 'power_results.md')):
        gen = read_text(os.path.join(ROOT, 'bin', 'power_results.md'))
        c.check('18857' in gen and '5708' in gen and '3.30×' in gen,
                'bin/power_results.md 为最新实测值')

    return c.summary()


if __name__ == '__main__':
    raise SystemExit(main())
