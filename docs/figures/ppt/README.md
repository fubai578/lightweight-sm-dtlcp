# 报告插图流水线（docs/figures/ppt/）

作品设计报告的 13 张插图，**全部为手工图形对象**（非截图、非外部素材），每张图一个 PPT 源文件。

## 文件约定

| 路径 | 内容 |
|---|---|
| `figlib.py` | 绘图库：多色系调色板、版式骨架、小图原语（报文条/条形图/环形槽位/滑动窗口/方格阵）、版式自检、元数据清理 |
| `fig_*.py` | 每张图一个脚本，一个脚本只画一图；改图改脚本，不直接编辑 PPT/PNG |
| `pptx/` | 生成物：每图一个 `.pptx`（可在 PowerPoint/WPS 里继续手工微调） |
| `png/` | 生成物：由 `render_figs.py` 渲染的 2400×1350 PNG，供 Word 插图使用 |
| `render_figs.py` | 渲染：LibreOffice → PDF（矢量）→ PyMuPDF 栅格化（约 240 dpi） |

## 用到的图

| 图号 | 脚本 | 说明 |
|---|---|---|
| 图 1-1 | `fig_A1_bottleneck.py` | 三重资源瓶颈与三条对策 |
| 图 1-2 | `fig_B2_modules.py` | 作品功能构成（六个部分） |
| 图 2-1 | —（**飞书正式稿原图**） | 系统总体架构图，见 `png/图2-1-系统总体架构图（飞书原图）.png` |
| 图 2-2 | `fig_B3_kernels.py` | 多档内核与差分验证 |
| 图 2-3 | `fig_B4_sm3_sm4.py` | SM3 压栈 / SM4 换查表 |
| 图 2-4 | `fig_B5_sm2_route.py` | SM2 域运算与标量乘分流 |
| 图 2-5 | `fig_B8_comb.py` | comb 预计算表结构与生成 |
| 图 2-6 | `fig_B6_ct.py` | 常量时间与预计算表共存 |
| 图 2-7 | `fig_B7_record.py` | 记录层与密钥调度 |
| 图 2-8 | —（**飞书正式稿原图**） | FULL 握手报文时序，见 `png/图2-8-FULL握手时序（飞书原图）.png` |
| 图 2-9 | `fig_A2_workflow.py` | 软件工作流程与移植面 |
| 图 2-10 | `fig_D2_perf.py` | 优化效果与外部基线对照 |
| 图 4-1 | `fig_E1_scenarios.py` | 应用场景与配置选配 |

## 图标

小图标取自 [Lucide](https://lucide.dev)（`lucide-static`，ISC 许可），
按 9 种色系上色（`docs/figures/icons/png2/<色>/<图标>.png`），
生成步骤见 `docs/figures/icons/README.md` 与 `make_icons_multi.py`。

## 重新生成

```bash
cd docs/figures/ppt
for f in fig_*.py; do python "$f"; done      # 画图（含版式自检，0 问题才退出 0）
python render_figs.py                        # 渲染成 PNG
python ../../../scripts/build_report_docx.py  # 重新生成报告 DOCX 与 docs/06
```
