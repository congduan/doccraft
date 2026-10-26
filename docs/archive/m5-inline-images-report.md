# M5 内联图片战报

## 目标

从 `.doc` 的 PICF 与 Escher BLIP 提取内联图片，在 `.docx` 中保留原始媒体字节与版面尺寸，并以 golden 样本验证。

## 假设验证（2026-10-26）

工作基线：`git fetch origin` 后 HEAD 与 `origin/main` 均为 `6fcb1e0`，无上游新 diff。未跟踪的 `build-asan/` 为既有目录。

用 `tests/fixtures/inline-image.fodt` 内嵌 64×40 PNG，经 LibreOffice 导出 `.doc` 和 `.docx`。临时探针用仓库 CFB 与格式解析器读出流及 run；原始输出：

```text
PNG bytes 170 SHA256 4d2153555c8d130fa2be5c9ad62d48fc410fa65b47438c0e888c25e42cbed785
CFB version 3, sector size 512
Root Entry/Data (417 bytes)
Word=3631 Table=1661 Data=417
run 0:0 text=4265666f726520 offset=-1 binary=0 ole=0 embedded=0
run 0:1 text=01 offset=0 binary=0 ole=0 embedded=0
run 0:2 text=204166746572 offset=-1 binary=0 ole=0 embedded=0
0000: a1 01 00 00 44 00 64 00 9d 06 22 04 00 00 00 00
0040: 00 00 00 00 0f 00 04 f0 66 00 00 00 b2 04 0a f0
00b0: 00 80 62 00 07 f0 e7 00 00 00 06 06 aa c4 18 cc
00e0: 1e f0 bb 00 00 00 aa c4 18 cc 00 00 00 00 04 51
00f0: 17 77 28 00 00 10 ff 89 50 4e 47 0d 0a 1a 0a
```

LibreOffice `.docx` 中 `word/media/image1.png` 为 170 字节，SHA256 与源 PNG 完全相同。故此样本确实走 Data 流偏移 0 的 PICF（68 字节）+ OfficeArtInlineSpContainer + FBSE + PNG BLIP 路径；未发现 ObjectPool 载荷。

同法导出 `inline-jpeg.fodt` 的原始输出：

```text
JPEG 1127 c59bdd857eb3b8e4ba1317c74a36eee1576e6acd3c9df734ede71b71c0da2fe3
Word=3631 Table=1661 Data=1374
run 0:1 text=01 offset=0 binary=0 ole=0 embedded=0
word/media/image1.jpeg 1127 c59bdd857eb3b8e4ba1317c74a36eee1576e6acd3c9df734ede71b71c0da2fe3
```

PNG 首轮像素对拍 `FAIL page 1: rms=6.87 max=255 bad_px=3743 (0.178%)`。逐个替换 DrawingML 节点后，定位到 `wp:inline` 缺少 `distT/distB/distL/distR="0"` 会使 LibreOffice 把内联图右移；补齐后 `bad_px=0`。

规范依据：[MS-DOC PICFAndOfficeArtData](https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-doc/dedb7505-8cc4-4053-aa2d-431b18d4bd97) 明确图片在 Data Stream、偏移来自 sprmCPicLocation；[MS-DOC PICF](https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-doc/d81b079b-e28a-4d58-a7d3-e10c813a7098) 规定 68 字节头；[MS-ODRAW OfficeArtBlipPNG](https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-odraw/7af7d17e-6ae1-4c43-a3d6-691e6b3b4a45) 规定 UID/tag 后为原始 PNG。

## 改动文件

新增 `src/doc/format/pictures.cc`，由 `formatted.cc` 在正文图片 run 处读取 Data 偏移并把媒体和 twip 尺寸放入 `model::Document::images`；`src/docx/writer.cc` 与 `writer_04_paragraph_body.inc` 写出媒体部件、关系、内容类型及 `wp:inline`。`src/doc/format/internal.h` 收纳 PICF/Escher 限值和记录常量，`src/doc/CMakeLists.txt` 链接已有 vendored miniz 以解压 WMF/EMF。`tests/m5_inline_images_test.cc` 与 `tests/CMakeLists.txt` 加合成及 golden 断言；新增 `tests/fixtures/inline-image.*` 和 `inline-jpeg.*`。DESIGN、SPRM-COVERAGE、KNOWN-SIMPLIFICATIONS 同步记录状态与限制。

## 测试清单

- 先写合成 PICF→IR→XML 测试，首次构建按预期因 `ReadInlinePicture` 未定义失败。
- `ctest --test-dir build -R M5InlineImages --output-on-failure`：5/5 PASS（PNG 合成、PNG/JPEG golden、WMF/EMF 压缩与非压缩、截断拒绝）。
- `python3 tests/pixelcompare/compare.py tests/fixtures/inline-image.doc`：`OK page 1: rms=0.00 max=0 bad_px=0 (0.000%)`。
- `python3 tests/pixelcompare/compare.py tests/fixtures/inline-jpeg.doc`：`OK page 1: rms=0.00 max=1 bad_px=0 (0.000%)`。
- `cmake --build build -j4`：PASS，`-Werror` 无告警。
- `ctest --test-dir build --output-on-failure`：150/150 PASS。
- `cmake --build build --target format-check`：PASS。

## 已知限制

正文内联且嵌入的 BLIP 已覆盖。浮动、非嵌入或延迟 BLIP、ObjectPool/OLE、裁剪/边框/旋转、其他故事里的图片未纳入本刀。WMF/EMF 有合成位流提取和压缩解码测试，但没有 LibreOffice 导出的真实元文件 golden；这一验证空缺在 `KNOWN-SIMPLIFICATIONS.md` 登记。
