# M20 测试深化批 战报

## 0. 基线

- 开工 HEAD：`f955a02fb7aed7d855e411fb9d272cfb73f02588`
  （2026-10-29 20:19:35 +0800，`docs: fix stale comment on the named total-buffer error`）
- 开工工作区：`git status --porcelain` 为空。
- 外部语料 `/home/ubuntu/doccraft-corpus` 全程只读，未修改。
- 本批未改动 `src/`、`third_party/`；`git diff f955a02 -- src` 为空。

> 说明：本批交付期间同一仓库另有一个不属于本批的 docs 提交
> `8930b0f docs: record GOVDOCS1 corpus expansion (130 files, total 680)`，
> 只改 `docs/KNOWN-SIMPLIFICATIONS.md`。本批四个提交与其互不重叠。

## 1. 交付总览

| 块 | commit | 内容 |
|---|---|---|
| 1 | `cb4fb68` | 合成 .doc 边界语料生成器 + 27 个样本 + `edge_corpus` ctest |
| 2 | `b512112` | `m20_writer_matrix_test.cc`，22 个 writer 属性矩阵用例 |
| 3 | `f6060c0` + 后续补 2 例 | `m20_malformed_test.cc`，37 个畸形输入回归用例 |
| 4 | `b2bc703` | 语料 8→12，4 个 MS-DOC 特性样本 + pixel 基线记录 |

ctest 用例数：**373 → 433（+60）**，其中 27 个边界样本断言集中在
`edge_corpus` 一个 ctest 条目内；新增独立 gtest 用例 59 个（22 + 37）。

## 2. 块1：合成边界语料

### 实现路线

按任务允许的二选一，采用 **subprocess 调用小型 C++ 生成器**：

- `tests/edge_gen.cc`：在 `tests/cfb_test_util.h` 的 `CfbBuilder` 之上组装
  WordDocument / 0Table / Data。WORD 97 FIB、CLX piece table、PAPX FKP 页、
  PlcfBtePapx、STSH 样式表、PlcfSed 分节表均由 `tests/m20_doc_util.h`
  统一构造。
- `tests/corpus/gen_edge_corpus.py`：驱动 `edge_gen` 生成 `tests/corpus/edge/*.doc`，
  并写 `.expected.txt` / `.expected-error`。选择 C++ 路线是因为 Python 独立实现
  CFB 需要约 200+ 行且更容易在 FAT/MiniFAT 边界出错；复用 `CfbBuilder` 的
  实现成本更低，也符合任务“选实现成本低”的防失败条款。
- `tests/corpus/run_edge_corpus.py`：逐样本转换，成功样本比对文本，拒绝样本
  比对 stderr 关键词。

生成器先在真实 doccraft 上验证 7 个基础样本可解析（empty / single-char /
two-paragraphs / nested-table-d2..d9），再扩展到 27 个；未闭门造格式。

### 样本清单（27 个，≥20）

| 类别 | 样本 | 期望 |
|---|---|---|
| 空/单字符 | `empty-document`、`single-char`、`two-paragraphs` | 成功 |
| 深嵌套表格 | `nested-table-d2/d4/d8/d9` | 成功；d9 结构钳到 8 层 |
| 超大单元格 | `huge-cell-text`（70,000 字符） | 成功 |
| 超多列 | `many-columns`（70 列，8 个 FKP 页） | 成功 |
| 控制字符 | `control-whitespace`、`control-page-break`、`control-section-break`、`control-field-chars`、`control-object-marks` | 成功 |
| piece 表极端 | `piece-cp-descending`、`piece-outside-stream`、`piece-prm-index`、`piece-fcmin-mismatch`、`piece-mixed-encoding` | 前 3 拒绝，后 2 成功 |
| 代码页 | `cp1252-all-256`、`utf16-surrogate-pair`、`utf16-unpaired-surrogate` | 成功 |
| 样式链 | `style-cycle`（A→B→A）、`style-deep-chain`（20 级） | 成功 |
| 域 | `field-nested-5`、`field-missing-separator`、`field-unclosed` | 成功 |

`control-section-break` 通过 PlcfSed 产生两个 section end；`nested-table-d9`
输出 8 个 `<w:tbl>` 且 L9 内容落在第 8 层，验证 `kMaxTableDepth` 钳制。

## 3. 块2：OOXML writer 属性矩阵

`tests/m20_writer_matrix_test.cc` 直接构造 IR，不经过 DOC 解析，22 个用例：

- 段落：5 种对齐、4 种缩进/字符单位缩进、3 种行距、间距 + keepNext/
  pageBreakBefore/widowControl/outline/textAlignment。
- 字符：粗斜删除线双删除线轮廓阴影小型大写隐藏、5 种下划线 + 颜色、
  上标/下标/基线、高亮 + 索引色 + RGB 色、字号/字体/语言、字距/缩放/
  位置/字偶距/强调符。
- 表格：横向、纵向、双向、L 型 4 种合并；6 边 × 3 线型（18 个片段）；
  两层嵌套；单元格底纹/边距/对齐/文字方向。
- 分节：continuous/nextPage/oddPage；两节各自独立页眉页脚（4 个 part）；
  odd/even + first-page 页眉；脚注属性；页面设置与分栏。

每个用例都断言具体 XML 片段，不是“转换成功”。

## 4. 块3：畸形输入回归库

`tests/m20_malformed_test.cc`，37 个 byte-level 用例，其中 **30 个断言拒绝**：

- FIB：ident、nFib 过低/过高、截断 base、截断 RgLw、截断 pair、RC4/XOR
  加密头、缺失 CLX；`fcMin` 低于头长（CLX 权威，接受）与 `ccpText`
  超出 piece 覆盖（告警并保留已覆盖文本）各一例。
- CLX：CP 倒序、CP 相等、Pcdt 声明过大/过小、CLX 越界、compressed 位越界、
  Prc 尺寸溢出、Prm 索引越界、缺失 Pcdt、piece 过度覆盖。
- FKP：FC 倒序、run count 30/0、PAPX 长度溢出、PAPX sprm 截断、重复页、
  页越界、BTE 尺寸非法、BTE 页数超流。
- sprm：operand 截断、已知 opcode 缺 operand、自引用嵌套 grpprl。
- 图片/OLE：PICF 负宽/负高、未知 BLIP 类型、OLE presentation 截断。

7 个用例（FC 倒序/run count/PAPX 溢出/重复页/页越界/深度炸弹等）断言的是
**产品既有的降级策略**：FKP 结构损坏时跳过该页并警告，而不是返回错误。
这与 `docs/KNOWN-SIMPLIFICATIONS.md` 的 degraded FKP 条目一致，已在
`ExpectDegradedFkp` 中固定警告关键词与成功解析；其余 30 个用例严格断言
`std::expected` 错误与指定关键词。

## 5. 块4：数据深化

`tests/corpus/corpus/` 由 8 份扩到 12 份，新增：

- `sections-continuous`：3 节、2 个 continuous 分节（LibreOffice 从 FODT
  的 `fo:break-before` + `text:section` 导出）。
- `nested-tables`：5 列外表 + 单元格内 2×2 嵌套表。
- `page-fields`：PAGE、SEQ 域 + 超链接。
- `comments-bookmarks`：同一段文本上的 ranged comment（annotation +
  annotation-end）与 bookmark（bookmark + bookmark-end）。

均通过 `gen_corpus.py` 生成 `.doc`/`.docx`，`.txt` baseline 用
`soffice --headless --convert-to txt` 后把 TAB 替换为换行，保留 UTF-8 BOM。

pixelcompare（150 DPI / 0.1%）：

| 样本 | 页数 | bad_ratio | 结果 |
|---|---|---|---|
| sections-continuous | 2 | 0.000% | PASS |
| nested-tables | 1 | 0.305% | FAIL |
| page-fields | 1 | 0.000% | PASS |
| comments-bookmarks | 1 | 0.000% | PASS |

已写入 `docs/PIXEL-BASELINE.md` 的 “M20 corpus expansion (2026-10-29)”
一节，并追加到 `docs/pixel-baseline.tsv`。

## 6. 验证（原始输出）

### 6.1 构建零 error

```text
$ cmake --build build -j2
[ 93%] Built target m4_fast_saved_test
[ 95%] Built target m8_bad_papx_fixture
[ 97%] Built target perf_repack_body
[100%] Built target m21_cli_fixture
build_exit=0
```

### 6.2 全量 ctest

```text
$ ctest --test-dir build --output-on-failure -j2
...
433/433 Test #418: edge_corpus ............................................................   Passed    0.16 sec

100% tests passed, 0 tests failed out of 433

Total Test time (real) =   6.95 sec
```

用例数对比：基线 `ctest -N` = 373，本次 = 433，新增 60 个 ctest 条目。

### 6.3 新语料断言

```text
$ python3 tests/corpus/run_edge_corpus.py build/src/cli/doccraft
OK   control-field-chars: 8 chars
OK   control-object-marks: 2 chars
OK   control-page-break: 3 chars
OK   control-section-break: 3 chars
OK   control-whitespace: 4 chars
OK   cp1252-all-256: 225 chars
OK   empty-document: 0 chars
OK   field-missing-separator: 0 chars
OK   field-nested-5: 11 chars
OK   field-unclosed: 6 chars
OK   huge-cell-text: 70000 chars
OK   many-columns: 270 chars
OK   nested-table-d2: 8 chars
OK   nested-table-d4: 20 chars
OK   nested-table-d8: 44 chars
OK   nested-table-d9: 50 chars
OK   piece-cp-descending: rejected with 'invalid piece CP order'
OK   piece-fcmin-mismatch: 2 chars
OK   piece-mixed-encoding: 6 chars
OK   piece-outside-stream: rejected with 'piece outside WordDocument'
OK   piece-prm-index: rejected with 'CLX Prm1 index outside Prc array'
OK   single-char: 1 chars
OK   style-cycle: 6 chars
OK   style-deep-chain: 4 chars
OK   two-paragraphs: 5 chars
OK   utf16-surrogate-pair: 2 chars
OK   utf16-unpaired-surrogate: 2 chars

27/27 edge samples passed
```

12 份 corpus 文本一致性：

```text
$ python3 tests/corpus/compare_text.py build/src/cli/doccraft
alignment-indents.doc: 100.00%
bold-italic-underline.doc: 100.00%
cjk-text.doc: 100.00%
comments-bookmarks.doc: 100.00%
complex-table.doc: 100.00%
cyrillic-text.doc: 100.00%
headings-list.doc: 100.00%
nested-tables.doc: 100.00%
warning: ignored non-XML control in field instruction
page-fields.doc: 100.00%
plain-text.doc: 100.00%
sections-continuous.doc: 100.00%
simple-table.doc: 100.00%
minimum: 100.00%
```

### 6.4 既有语料 golden gate

检查方法：取基线 `f955a02` 中 `tests/corpus/corpus` 与 `tests/fixtures` 下的
全部 `.doc`（39 个），排除 3 个需要 `--password` 的加密 fixture，共 36 个。
对每个样本：

1. 用 `git hash-object` 与 `git rev-parse f955a02:<path>` 比对输入字节；
2. 连续转换两次，逐 ZIP part 计算 sha256，确认确定性；
3. 汇总所有 part 的 manifest sha256。

```text
$ python3 /tmp/golden_gate.py
baseline samples: 36
inputs byte-identical to f955a02: 36/36
deterministic part hashes: 36/36
aggregate part-manifest sha256: 57aad25f28e73ba272f3517534f223506a80cfaccba222933959ec34392517fa
exit=0
```

产品代码零差异：

```text
$ git diff --stat f955a02 -- src
$ （无输出）
```

输入只增不减：

```text
$ git diff --stat f955a02 -- tests/corpus/corpus tests/fixtures | tail
 tests/corpus/corpus/comments-bookmarks.doc   | Bin 0 -> 9216 bytes
 tests/corpus/corpus/nested-tables.doc        | Bin 0 -> 11264 bytes
 tests/corpus/corpus/page-fields.doc          | Bin 0 -> 9216 bytes
 tests/corpus/corpus/sections-continuous.doc  | Bin 0 -> 9216 bytes
 ...
 12 files changed, 76 insertions(+)
```

即：既有 8 份 corpus 与全部 fixture 的字节未变，`src/` 未变，因此既有输出
不可能回退；上述 36/36 输入字节一致 + 两次转换 part sha 一致 + `src` 零 diff
构成 golden gate 证据。任务描述中的“35 个”与基线树中非加密 `.doc` 实际数量
36 个有 1 个出入，按仓库实际文件计数。

### 6.5 format-check

```text
$ cmake --build build --target format-check
[100%] clang-format: verifying Google style formatting
[100%] Built target format-check
format_exit=0
```

## 7. 发现的问题（本批不修 `src/`）

1. **嵌套表格行尾标记产生多余空段落（pixel 0.305%）**
   `nested-tables.doc` 的 B1 单元格内，doccraft 输出 8 个 `<w:p>`，LibreOffice
   参考为 6 个；多出的 2 个空段落来自嵌套表每行的 TTP/row-end 标记。结果是
   外表第二行整体下移约两行，pixelcompare 0.305% FAIL、文本一致性仍 100%。
   位置线索：`src/doc/format/formatted.cc` 的 `close_row` / `FinishTableRow`
   对空 `para` 的处理。按本批边界未改产品代码，已记录到
   `docs/PIXEL-BASELINE.md`。

2. **FKP 结构损坏是降级而非拒绝**
   FC 倒序、run count 越界、PAPX 长度溢出、重复页、页越界都只跳过该 FKP 页
   并写 warning，转换仍成功。这是既有 degraded-FKP 策略，不是本批引入的
   bug；回归用例已固定 warning 关键词。只有 `invalid FKP sprm sequence`
   会升级为整篇拒绝。

3. **LibreOffice FODT→DOC 无法导出嵌套域**
   `text:field` 在 FODT→DOC 导出时被丢弃；原生 `text:page-number` /
   `text:sequence` 只产生单层域。因此块4 的 `page-fields` 是单层域样本，
   嵌套域覆盖由块1 的 `field-nested-5` 提供。若后续必须用 LO 生成嵌套域
   语料，需要在 FODT 之外走 DOC 级构造。

4. **空文档输出一个空段落**
   `empty-document`（ccpText=0）的 IR 段落数为 0，但 `document.xml` 仍有
   writer 的 section 结尾空 `<w:p>`；文本为空，符合 writer 结构要求，不是
   数据丢失。

## 8. 规范与边界

- 新增 C++ 文件均带 Apache-2.0 头，Google 风格，`format-check` 通过。
- `tests/m20_doc_util.h` 只放在 `tests/`，未进入产品依赖方向。
- 未新增第三方依赖；未改 `third_party/`、`build/`、外部语料。
- 四个提交均未 push，等待主控 review。
