# doccraft 渲染对拍管线（pixel-compare）

## 原理

同一 .doc 源，双路径渲染后逐页像素 diff：

```
路径A（基准）：source.doc ──LibreOffice──> PDF ──pdftoppm──> PNG
路径B（被测）：source.doc ──doccraft──> out.docx ──LibreOffice──> PDF ──pdftoppm──> PNG
比较：逐页灰度差分 → rms / max / bad_px（灰度差>16）/ bad_ratio → 差异热图
```

依赖：libreoffice-writer、poppler-utils（pdftoppm）、PIL、numpy

## 用法

```bash
# M2 起（doccraft 真实转换路径，缺省）
python3 tests/pixelcompare/compare.py tests/corpus/corpus/plain-text.doc

# M2 前管线自检（跳过 doccraft，用现成 docx）
python3 tests/pixelcompare/compare.py x.doc --docx y.docx

# 可调参数：--dpi 150  --threshold 0.001（单页 bad_ratio 阈值 0.1%）
# 人工接受已知页数分歧：--allow-page-count-mismatch
```

退出码：0=PASS，1=超阈值，2=管线错误。产物在 `build/pixelcompare/`：
`diff-NN.png`（差异热图，红=差异）、`report.tsv`。

## 判定口径

- 页数不一致 → FAIL。若同名 `.allow` 文件精确写有
  `page-count-mismatch A=1 B=2` 等实测页数，则标记 KNOWN，并输出重叠页指标。
  命令行 `--allow-page-count-mismatch` 可作一次性人工放行。
- 尺寸不一致（版面几何漂移）→ 该页硬 FAIL
- bad_ratio ≥ 0.1% → FAIL（0.5% 在 A4/150dpi 下太宽松，已实测校准）
- 热图定位差异区域后，人工确认是「可接受的字体光栅化抖动」还是「真实还原缺陷」

## 验证记录（2026-10-23）

- 同源对拍（LibreOffice doc vs LibreOffice docx）5/5 全 PASS，rms=0.00 max=0
  —— 双路径渲染本身是无损的，任何非零差异都来自转换器
- 反向验证：错配对拍（表格.doc vs 纯文本.docx）bad_px=6557 (0.312%)，
  0.1% 阈值下正确 FAIL，热图准确定位到文字行区域
- 管线有效性成立： rms=0 是「干净」的充分判据

## 第二渲染引擎（交叉验证）

abiword（独立 doc 解析实现）可作第二引擎暴露 LibreOffice 自身的解析盲区；
ONLYOFFICE（docker 服务）作为第三引擎后置。接入方式：路径A 的 soffice_convert
替换为对应引擎的转换+渲染命令，diff 逻辑复用。

## CI 集成

M2 起将 compare.py 挂入 golden 测试（每个特性样本一条），FAIL 时上传热图作 artifact。
