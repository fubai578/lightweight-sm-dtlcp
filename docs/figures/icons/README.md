# icons/ — 框架图使用的图标（第三方资源说明）

## 来源与许可

图标取自 **[Lucide](https://lucide.dev)**（`lucide-static` v0.544.0），
许可为 **ISC**（宽松许可，允许商用与再分发；要求保留版权与许可声明）。

- 本目录下 `*.svg` 为**原始文件**（保留 `stroke="currentColor"`，便于溯源与重新上色）。
- `png/*.png` 为**已上色成品**：按本图配色渲染为单色描边 + 透明底，供
  `docs/figures/build_framework_pptx.py` 直接插入 PPTX。
  - 无后缀：青绿 `#0F4C5C`（结构性元素）
  - `_accent` 后缀：赭橙 `#C2410C`（仅用于承载两项新结论的 SM2 卡）

## 图标与内容的对应

| 卡片 | 图标文件 | 含义 |
|---|---|---|
| 传感终端（client） | `radio` | 无线传感节点 |
| 网关（server） | `server` | 网关/服务端 |
| demo_run.sh | `square-terminal` | 一键脚本 |
| 握手状态机（SM2） | `handshake` | 握手协商 |
| 记录层 | `layers` | 数据报记录栈 |
| 密钥调度 | `key-round` | 密钥派生 |
| 紧凑证书 | `file-badge` | 证书/印记 |
| SM3 | `hash` | 散列 |
| SM4 | `grid-2x2` | 分组密码 |
| SM2 | `spline` | 椭圆曲线 |
| HMAC · KDF · DRBG | `shield-check` | 认证/派生/随机数 |
| 平台钩子（6 个） | `plug` | 平台接口 |
| 目标平台 | `cpu` | MCU |
| 上板前预检 | `clipboard-check` | 预检清单 |

## 重新生成 PNG（改色或换图标时）

需要在 PATH 中有 LibreOffice（`soffice`；Windows 上用 `soffice.com`）：

```bash
cd docs/figures/icons
mkdir -p /tmp/blk && for f in *.svg; do
  sed -e 's/stroke="currentColor"/stroke="#000000"/' \
      -e 's/stroke-width="2"/stroke-width="1.7"/' "$f" > "/tmp/blk/$f"
done
soffice --headless --convert-to 'png:draw_png_Export:{"PixelWidth":{"type":"long","value":512},"PixelHeight":{"type":"long","value":512}}' \
        --outdir /tmp/blkpng /tmp/blk/*.svg
python make_icons.py            # 按亮度生成 alpha 并上色到 png/
```

`make_icons.py` 的做法：把渲染结果当**灰度遮罩**（笔画黑、底白），
`alpha = 255 − 灰度`、RGB 填目标色 —— 这是单色图标上色的标准做法，
能保留抗锯齿、避免"白底方块"压在暖色卡片上。
