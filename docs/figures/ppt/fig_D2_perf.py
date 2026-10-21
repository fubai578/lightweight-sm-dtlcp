#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2-10　优化效果与外部基线对照（报告 2.3）"""
from figlib import *

prs = new_deck()
s = new_slide(prs, '优化效果与外部基线对照',
              '左：本库优化内核相对教科书基线；右：相对 GmSSL 3.1.1（交错测量）',
              huename='blue')

PW, GAP = 6.05, 0.28
X1, X2 = M, M + PW + GAP
Y0, PH = 1.34, 4.10

# ── 左
panel(s, X1, Y0, PW, PH, 'blue')
icon(s, 'chart-column', X1 + 0.20, Y0 + 0.18, 0.52, 'blue')
block(s, X1 + 0.86, Y0 + 0.18, PW - 1.1, 0.52,
      [('本库内：基线 → 优化', 18, hue('blue')[0], True)], MSO_ANCHOR.MIDDLE)
named_bars(s, X1 + 0.22, Y0 + 0.98, PW - 0.44,
           [('SM2 密钥生成', 8.90, 'blue', True),
            ('SM2 签名', 6.22, 'blue', True),
            ('SM2 验签', 5.59, 'blue', True),
            ('SM2 ECDH', 3.63, 'blue', False),
            ('SM4 单块', 1.55, 'purple', True),
            ('SM4-CBC', 1.57, 'purple', False),
            ('SM3', 1.08, 'amber', False)],
           9.6, name_w=1.90, hgt=0.30, gap=0.16, baseline=1.0, name_size=12.5,
           val_size=13)

# ── 右
panel(s, X2, Y0, PW, PH, 'teal')
icon(s, 'target', X2 + 0.20, Y0 + 0.18, 0.52, 'teal')
block(s, X2 + 0.86, Y0 + 0.18, PW - 1.1, 0.52,
      [('外部锚点：本库 ÷ GmSSL', 18, hue('teal')[0], True)], MSO_ANCHOR.MIDDLE)
named_bars(s, X2 + 0.22, Y0 + 0.98, PW - 0.44,
           [('基点乘 k·G', 5.00, 'blue', True),
            ('SM2 签名', 4.44, 'blue', True),
            ('SM2 验签', 3.29, 'blue', True),
            ('变基点乘 k·P', 2.04, 'teal', True),
            ('域乘法', 1.95, 'teal', False),
            ('SM4 单块', 1.07, 'purple', False),
            ('SM3 1 KB', 0.90, 'rose', True)],
           5.5, name_w=1.95, hgt=0.30, gap=0.14, baseline=1.0, name_size=12.5,
           val_size=13)

# ── 口径带
card(s, M, 5.56, BODY_W, 0.68, '口径：比值只取同一轮基准输出',
     None, huename='amber', icon_name='eye', icon_size=0.52, tsize=16,
     tone='white')

footer(s, '图 2-10　对 GmSSL 的主要指标已全面领先（SM2 2.0–5.0×、域运算约 1.95×），'
          '仅 SM3 落后约 10%。',
       '数据来源：bin/bench_results.md、bin/thirdparty_compare.md。', fs=0.95)

save(prs, '图2-10-性能对比.pptx')
sys.exit(1 if qa(prs) else 0)
