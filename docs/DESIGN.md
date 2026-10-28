# doccraft 设计文档

**doccraft** — 高保真 .doc → .docx 转换器（C++23，自研解析，零运行时依赖单二进制）

- 目标：整体完成度与还原度超越现有开源实现（LibreOffice/ONLYOFFICE 的 doc 导入路径、antiword、wvWare 等）
- 性能目标：99% 文档端到端秒级转换（单文档 < 1s，100MB 内大文档 < 5s）
- 质量目标：TDD，特性清单逐项验收；Google C++ Style Guide；C++20 全量特性

---

## 1. 总体架构

```
┌─────────────┐   ┌──────────────┐   ┌─────────────┐   ┌──────────────┐
│  CFB/OLE    │ → │  Word Binary │ → │  Document   │ → │  OOXML       │
│  Container  │   │  Format      │   │  Model (IR) │   │  Writer      │
│  (含解密)    │   │  (FIB/PIECE/ │   │  (格式无关)  │   │  (docx zip)  │
│             │   │  PAP/CHP...) │   │             │   │              │
└─────────────┘   └──────────────┘   └─────────────┘   └──────────────┘
     layer1            layer2            layer3            layer4
```

核心原则：**四层单向数据流，IR 为唯一枢纽**。解析层不懂 docx，写出层不懂 doc；
两端的复杂度被 IR 隔离，测试可在每一层独立进行（层间 golden data）。

公共底座 `src/base/` 不是数据流层，被 `cfb` 与 `doc` 共享：`binary.h` 提供
`Bytes = std::span<const uint8_t>`（只读、带长度，切片不丢边界）与
`U16`/`U32`/`U64`/`S16` 四个小端读取原语；`utf8.h` 提供 `AppendUtf8`。CFB 与
MS-DOC 都是小端格式、文本面都是 UTF-16LE，字节序解码与 UTF-8 编码因此各只实现
一次，`base` 不包含任何上层头文件。读取原语本身不做边界检查，调用方先用
`subspan` 或范围校验切出视图（有状态校验游标见 KNOWN-SIMPLIFICATIONS M3）。

### 1.1 layer1: CFB/OLE 容器层 (`src/cfb/`)

Compound File Binary Format（[MS-CFB]），自研实现：

- Header 解析（扇区大小 512/4096、MiniFAT/FAT/DIFAT 链）
- 目录树（storage/stream 层级、红黑树 sibling 遍历）
- Mini-stream（< 4096B 的小流经 root entry 的 mini stream 间接寻址）
- 读写 API：`CfbReader::open(bytes)` → `stream(name)` / `visit(tree)`
- **解密**（见 §4）

### 1.2 layer2: Word Binary 格式层 (`src/doc/`)

Word 97-2003 Binary File Format（[MS-DOC]）：

- **FIB**（File Information Block）：版本探测（旧版 0x0065/0x0068 的文本分支；0x00C1→Word97 … 0x011A→Word2007 的完整分支）、
  flag 位解析（fComplex、fEncrypted、fWhichTblStrm）、各表偏移
- **Table Stream**（0Table/1Table 选择）: STSH、PLCF 系列、修订信息
- **文本获取**：-piece table（PLCF of PCD），支持 fcCompressed（按 `FibBase.lid` 的 ANSI 代码页压缩，经 iconv 解码）与
  UTF-16 双编码、fExtChar；fast-saved（fComplex）文档的Revision/原位置重建
- **字符/段落/节属性**：CHPX/PAPX 二叉桶（grpprl 解包）、SEPX、
  sprm 操作符全量分派（sprmP/sprmT/sprmCR/sprmPH…）
- **对象**：书签（BKL/STTBFM）、脚注尾注（PLCFfootnoteRef）、批注（annotation PLCF）、
  域（FieldTable：fldChar/fldInstr 分词）、表格（TAP + row/cell shading grpprl）、
  图片（PICF + Escher 容器扫描 + BLIP 提取）、页眉页脚（hdd 流 + GrpXstHeaders）、
  列表（LSTF/LVL + numbering override）、OLE 对象（Ole10Native、嵌入流透传）

### 1.3 layer3: 文档模型 IR (`src/model/`)

格式无关的中间表示：

```cpp
Document { styles, sections[], body }
Section { properties, blocks[] }
Block  = Paragraph | Table | TxbxContent | SdtBlock
Paragraph { runs[], properties }        // 对齐/缩进/间距/边框/编号引用
Run { text, properties }                // 字体/字号/加粗斜体/下划线/颜色/高亮/字距
Inline = Picture | FieldBegin..Sep..End | FootnoteRef | Break | Sym | Object
Style { based-on chain, 直接格式叠加解析在加载时完成 flatten }
```

- C++20：`std::variant` + `overloaded` pattern、`std::span` 零拷贝访问、concepts
  约束属性访问器、`std::expected<T, Error>` 贯穿全链路（无异常路径）
- 单位统一：twips（内部），导出时按 OOXML 规则换算

### 1.4 layer4: OOXML 写出层 (`src/docx/`)

- [MS-OI29500]/ECMA-376 **transitional** 方言（兼容性最大化的 docx 方言）
- ZIP 容器：自研 minimal zip writer（store + deflate，zlib 依赖可选拨插）
- 部件生成：document.xml / styles.xml / numbering.xml / footnotes.xml /
  endnotes.xml / comments.xml / headers&footers / media/* / rels / [Content_Types]
- 命名空间、rsid 可选；样式表 from STSH 映射 + 直接格式内联
- 属性映射表 `docs/SPRM-TO-OOXML.md`（逐 sprm → w:xxx 元素对照，随实现增长）

### 1.5 CLI (`src/cli/`)

```
doccraft convert in.doc [-o out.docx] [--format auto|doc|docx]
doccraft inspect in.doc [--stream] [--fib] [--piece-table]   # 诊断输出
doccraft diff in.doc ref.docx                                 # 结构对比（QA 用）
```

## 2. 代码风格与工程规范

- **Google C++ Style Guide**，由 `.clang-format` / `.clang-tidy` / `CPPLINT.cfg`
  强制：2 空格缩进、80 列上限、`kConstantName`、`.h/.cc` 后缀、include 分组
  （本文件 → C 系统 → C++ 标准库 → third_party → 项目头）
- header guard 为仓库根相对路径（`SRC_DOCX_WRITER_H_`、`TESTS_CFB_TEST_UTIL_H_`），
  `#endif` 必须带保护宏注释；源文件首行为 Apache-2.0 版权头
- include 一律相对仓库根书写（`"docx/writer.h"`、`"third_party/miniz/miniz.h"`）
- C++23，`-Wall -Wextra -Wpedantic -Werror`；CI 用 `format-check` 目标把关，
  本地 `scripts/lint.sh [--fix|--cpplint|--tidy]`
- 测试用 googletest（FetchContent）
- 命名：`doccraft::cfb`、`doccraft::doc`、`doccraft::model`、`doccraft::docx`

### 2.1 依赖策略（2026-10-23 定案）

**领域核心自研，通用基础设施用业界顶尖开源库。**

- **自研**（项目差异化，无现成库可对标）：DOC/CFB 语义解析、sprm→OOXML
  映射、文档 IR、还原度逻辑
- **用库**（成熟基础设施，不重复造轮子）：
  - ZIP 容器：**miniz**（M3 起，替换 M2 手写 store+CRC32；deflate 支持顺带获得）
  - XML 生成：**pugixml**（M3 起，样式表生成与转义）
  - 加密原语（M6 RC4/MD5/SHA-1）：按 RFC 6229、RFC 1321、FIPS 180-4
    实现并以标准向量验证；不引入破坏单二进制零运行时依赖的库
  - 字符集解码：**iconv**（libc 设施：glibc 内置，macOS/Linux 走 libiconv；
    不新增传递依赖，未找到时编译期退化到内置 CP1252 表）。见
    `src/base/charset.cc`。
  - 测试：googletest、libFuzzer
- 选库标准：业界广泛使用、license 宽松（public domain/MIT/Apache）、
  可 FetchContent 单一来源引入、不引入传递依赖

## 3. 测试策略（TDD）

1. **单元**：每层独立 gtest；CFB 层用「程序化生成的合成容器」+ 真实样本双向验证
2. **Golden 测试**：`tests/golden/` 每个特性一个最小 .doc → 期望 .docx，
   diff 是 XML 结构对比（规范化属性顺序），不是字节 diff
3. **语料回归**：真实世界 .doc 集合（见 §5），指标 = 转换成功率 + 文本抽取一致性
4. **模糊测试**：CFB/doc 解析器 fuzz（libFuzzer harness，防崩溃/越界/死循环）
5. **性能基准**：Google benchmark，跟踪每层耗时；CI 记录趋势

## 4. 加密文档方案

- RC4 Encryption：[MS-DOC] §2.2.6.2/2.2.6.3、[MS-OFFCRYPTO] §2.3.5/2.3.6
  - 传统 RC4 版本 1.1：UTF-16LE 口令 → MD5 盐混合 → 每 512 字节派生密钥。
  - RC4 CryptoAPI 版本 2–4.2：UTF-16LE 口令与盐作 SHA-1，按块号再散列；
    40-bit 密钥按规范补零。两种模式均先验证加密 verifier，再解密
    WordDocument（前 68 字节明文）、Table（头部明文）与 Data 流。
- XOR obfuscation：[MS-DOC] §2.2.6.1、[MS-OFFCRYPTO] §2.3.7.2–2.3.7.6；
  校验 FibBase.lKey 的 32 位验证值，按 16 字节遮罩解密 WordDocument（前
  68 字节保留明文）、Table 与 Data 流。当前口令输入限 ASCII 1–15 字节。
- ECMA-376 agile encryption 的 doc-in-docx 混合容器：识别并提示
- 验证语料：一组已知口令的真实加密 .doc（Word 2003/2007 生成）

## 5. 测试语料计划

| 类别 | 来源 | 数量 | 用途 |
|---|---|---|---|
| 合成 CFB 容器 | 程序生成（各扇区/MiniFAT 边界） | 30+ | layer1 单元 |
| 特性最小样本 | Word 2003/2007 生成 + 现有开源语料 | 60+ | golden per-feature |
| 真实杂集 | 开源仓库/互联网公开样本（antiword/wvWare 测试集等，查 license） | 100+ | 回归 |
| 加密样本 | 已知口令的 RC4 文档 | 5+ | 解密链路 |
| 基准 .docx | LibreOffice roundtrip + 手工 | 配对 | diff 验证 |

## 6. DOC 特性清单（Feature Checklist）

验收口径：每项 = golden 样本 + roundtrip diff 通过。状态：☐ 未开始 ◐ 部分 ✅ 完成

### 6.1 文本与编码
- ✅ 基本文本抽取（piece table、文档 ANSI 代码页/UTF-16 双模式）
- ◐ Word 6/95 文本：实测 nFib 0x0065/0x0068 的旧 FIB、同流 CLX 与简单 fcMin 文本可读；CP1251 大文档与 Word 6 俄文样本已对拍，CP950 合成字节有断言。M12 起读取旧版 CHPX/PAPX FKP（1 字节 opcode 归一化为现代 sprm），M13 起旧版 STSH 样式表（pair 1：8 字节 STD 基座、Pascal 名、偶对齐 UPX、based-on/next 链）与 FFN 字体表（pair 15：cbFfnM1 条目、文档代码页 ANSI 名）进入同一条样式继承链，`sprmCFtc` 字体索引解析为字体名；M14 增加 STSHI 默认字体索引到 Normal 的回填与记录级畸形警告；旧版 STSHI 无字号字段，Bug50955/Bug51944 的 Normal 字号仍与 LibreOffice 合成值不同。M8 续批增加 WordDocument 中旧 PICF 的直存 WMF 图片，位图和其他 MFPF 类型显式拒绝；旧版页眉脚注、完整表格属性与其他故事尚未支持。
- ◐ fast-saved 文档（fComplex）：最终 CLX piece 表重建正文，Prm0/Prm1/Prc 修饰覆盖正文和页眉/脚注/批注/文本框故事；piece 上的 sprmPItap 表格深度有合成断言。正文表格状态按段落结束符 PAPX 选取，Bug33519 的 60 个碎表收敛为 3 个且行单元格对齐；旧、新 DOCX 的 LO 文本导出均为 691/691 词；版式 golden 未通过
- ◐ 特殊字符：tab/CR/VT/非断行空格/连字符/SYM 字符（图片/脚注/批注/绘图等特殊字符随 M4/M5）
- ◐ 域代码文本与结果（fldChar 层次）：通用复杂域输出 `w:fldChar` begin/separate/end + `w:instrText`，顶层未锁定 HYPERLINK 映射为 `w:hyperlink`、顶层页码类域映射为 `w:fldSimple`，其余（含嵌套/锁定）一律 fldChar 层次；正文 `PlcfFldMom`、页眉/脚注/尾注/批注故事 `PlcfFldHdr/Ftn/Edn/Atn`、正文及页眉文本框故事 `PlcfFldTxbx/PlcffldHdrTxbx` 的 `fldLock`/`dirty` 标志均已接入；`M11TextboxFields.FieldFlagsReachBodyAndHeaderTextboxXml` 合成断言覆盖两个文本框 XML 输出路径。真实 Word 文本框域 golden 尚缺，故保持 ◐；`fcPlcfFldMcr` 规范定义为 undefined 不读；正文 EMBED 对象按 ObjectPool 保存
- ✅ 多语言/复杂文种（RTL 标记、bidi、复杂文种颜色与字号）

### 6.2 字符格式（CHP）
- ✅ 字体族/字号/加粗/斜体/下划线（多种线型）/删除线/双删
- ✅ 上标/下标/字距/字符缩放/隐藏文字/着重号/字符单位适配（`w:fitText`、`w:eastAsianLayout`）
- ✅ 颜色/高亮/边框底纹/阴影/浮雕
- ✅ 超链接（HYPERLINK 域）

### 6.3 段落格式（PAP）
- ✅ 对齐（左/中/右/两端/分散）/缩进（左右/首行/悬挂，twips 与字符单位）/行距/段距（twips 与行单位）
- ✅ 段落边框与底纹/项目符号与多级列表（LSTF/LVL/override）
- ✅ 制表位（自定义/toc 风格）/keepNext/keepLines/pageBreakBefore/东亚排版开关
- ✅ 大纲级别/段落阴影/首字下沉（`w:framePr` dropCap）/纵向文字流

### 6.4 节与页面（SEP）
- ✅ 纸张大小/方向/页边距/装订线/纸张来源（`w:paperSrc`）
- ✅ 页眉页脚（首页/奇偶不同、逐节独立故事）/页码域
- ✅ 分节符类型（连续/ nextPage /evenPage/oddPage）/行号/脚注位置/页面边框与保护

### 6.5 表格
- ✅ 基本网格（TAP/rgtc）/单元格合并（横向 fVertMerge、纵向）/拆分
- ◐ 单元格边框/底纹/边距/对齐/文字方向/浮动表格（sprmTPc 锚点、sprmTTlp 表样式选项）、单元格内多段落与嵌套表格（按 sprmPItap 深度递归）；缺 sprmPItap 的样本仍按单层输出

### 6.6 对象与图形
- ✅ 内联图片（PICF + Escher BLIP：png/jpg/wmf/emf 提取与格式保留；正文图片，其他故事与非嵌入 BLIP 限制见 KNOWN-SIMPLIFICATIONS）
- ◐ 浮动图片与文字环绕（Escher anchor + 环绕类型映射）：FSPA anchor 定位（bx/by → margin/page/text，复用浮动表格语义）与主流环绕（topAndBottom/around=Square/none/rough=Tight→Through 合成覆盖）已通，LibreOffice golden 对拍达其内部基线下限；轮廓级 Tight/Through 区分需真实 Word 样本（LibreOffice 导出两者同为 wr=4），已知限制登记 KNOWN-SIMPLIFICATIONS
- ◐ 浮动文本框：正文与页眉专用 FTXBXS/Tbkd 故事范围按 shape id 连接各自的 PlcfSpa 锚点，写出 `w:pict/w:txbxContent`；PICF 中无 BLIP 的单个 OfficeArtFSP 可输出内联 VML 形状占位；内联文本框内容和链中各形状的独立排版仍待补齐
- ✅ 基本形状/艺术字占位：OfficeArtFSP 形状类型、FSPA 几何和 GeoText 原文进入 IR；矩形/椭圆/线条输出带形状类型注释的 VML 浮动占位，带 BLIP 的形状仍走图片路径。`M5FloatingImages.GeometryShapePlaceholderKeepsAnchor`、`LibreOfficeBasicShapesKeepGeometry`、`LineAndWordArtTypesKeepIdentity`；外观和环绕限制见 KNOWN-SIMPLIFICATIONS
- ◐ OLE 嵌入对象：字段分隔符上的 `sprmCPicLocation` 定位 ObjectPool 子存储，完整流族重新封装为 CFB 嵌入部件；字段结果图片优先作预览，缺失时解码 `\1OlePres000`/`\2OlePres000` 的 EMF/WMF 元文件并输出 `w:object/o:OLEObject`。链接语义和其他预览格式仍待补齐
- ◐ 脚注/尾注/批注/书签/交叉引用域/目录域（脚注、尾注、批注按各自 PLC 输出部件；标准书签的 `SttbfBkmk`/`PlcfBkf`/`PlcfBkl` 覆盖正文、页眉页脚、脚注、尾注、批注及正文/页眉文本框故事，并输出 `w:bookmarkStart`/`w:bookmarkEnd`；正文整域书签在 `ccpText` 闭合；非正文书签仅在起止位于同一故事时输出，结束位于故事守卫 CP 时仍会闭合，跨故事配对跳过并警告；文本框链中各分段的精确书签排版尚未对拍；交叉引用与目录域的指令与结果文本按通用复杂域输出）

### 6.7 样式系统
- ✅ STSH 全量解析（基于样式继承链展开；Word 6/95 旧版 STSH 自 M13 起接入同一链路：名称/基于/下一样式与 UPX 的段落、字符修饰经归一化后同规则叠加；M14 对 STSHI 默认字体和损坏记录给出有界处理与警告，旧版默认字号尚未还原）
- ✅ 样式映射到 docx styles.xml（段落/字符/表格/编号样式，含表样式条件格式 `w:tblStylePr`）
- ◐ 直接格式 vs 样式格式的优先级正确叠加（表样式条件格式已按 [MS-DOC] 2.4.6.6 的条带、列、行、角顺序输出；段落/字符/编号链仍未完整按规范求值）

### 6.8 加密
- ◐ XOR obfuscation 解密（`--password`）：ASCII 1–15 字节口令的合成 `.doc`
  fixture 与明文 `document.xml` 逐字节一致（`m16_xor_cli`）；非 ASCII 口令及
  真实 Word 生成样本仍待覆盖。
- ✅ RC4 加密文档解密（`--password`）：传统 RC4/MD5 与 RC4 CryptoAPI/SHA-1；`m6_rc4_cli` 与 `M6Rc4.*`。符合性经规范原文与 LibreOffice 参考实现复核；真实 Word 加密样本互通待补
- ✅ 加密类型检测报告：XOR、传统 RC4、RC4 CryptoAPI 分别报告；缺口令与错误口令均明确报错（`m6_rc4_cli`）

### 6.9 健壮性
- 性能实测基线见 [perf-baseline.md](perf-baseline.md)；100 MiB 结构化样本已低于 5 s，后续优化以分阶段计时和 RSS 打点为依据。
- ◐ 损坏文档的优雅降级：正文可读时跳过局部坏 CHPX FKP 页/坏 sprm 序列；PAPX FKP 页的局部页结构损坏按 BTE FC 范围丢弃，连同未闭合及后续连续表格一起省略；坏页眉、脚注、尾注、批注故事 PLC（批注作者表同故事处理）亦可跳过。`convert` 向 stderr 输出警告且退出码为 0；PAPX sprm/表格定义损坏及 CFB/CLX 截断仍拒绝，文本框等故事及更多格式路径待覆盖
- ◐ 超大文档内存控制：按需释放无 ObjectPool 文档的 CFB 原始输入，正文解码字符缩至 16 字节/字符；100 MiB 样本峰值低于 1000 MiB。分块流式处理与通用内存上限仍待实现。
- ◐ 确定性变异套件（`tests/fuzz_test.cc`）：全管线种子变异（5 种子 × 2000 轮/种子，ASan 下 300 轮，受 8s 墙钟阀门保护，见 KNOWN-SIMPLIFICATIONS「M6 变异加固」）、sprm/FKP 与 CLX/piece 表定向变异、全 65536 opcode dispatch，断言零崩溃且结果只能是转换成功或 `std::expected` 错误；代表性损坏断言可区分的错误消息。首轮即发现并修复 sprmPItap 深度未钳制导致的 writer 栈溢出；`sprmPItap`/`sprmPDtap` 深度操作数现统一经 `ClampTableDepth` 在解析层与格式帧栈两层钳制到 `kMaxTableDepth=8`（超深嵌套压平为第 8 层，合法深度输出逐字节不变）。注意这是固定 seed 的合成变异，不等同于持续 fuzzing：真实语料库长期变异、覆盖率导引与 CI 常驻入口仍是缺口（libFuzzer 驱动已可经 `DOCCRAFT_ENABLE_LIBFUZZER` 选配构建，默认关闭且不入 ctest）

## 7. 里程碑

| 里程碑 | 状态 | 内容 | 当前验收与剩余工作 |
|---|---|---|---|
| **M1** | ✅ | layer1 CFB 容器层（含 MiniFAT/DIFAT）+ CLI inspect | 合成与真实容器 golden；持续 fuzz 仍在 M6 范围 |
| **M2** | ✅ | FIB/piece table/文本抽取 + 纯文本 → docx 最小通路 | 基础文本路径与语料一致性验收 |
| **M3** | ✅ | CHP/PAP/SEP sprm + 样式系统 | 适用 sprm 见覆盖表；部分高级优先级与真实样本限制见 §6 |
| **M4** | ✅ | 表格 + 列表 + 域 + 脚注批注书签 | 主路径已交付；§6 标记 ◐ 的边界仍在迭代 |
| **M5** | ✅ | 图片/Escher/文本框 + OLE 对象 | 主路径已交付；复杂形状与预览限制见 §6 |
| **M6** | ◐ | RC4 解密 + 性能调优 + fuzz 加固 | 解密已交付（§6.8）；确定性变异套件已入 ctest（§6.9），100 MiB 样本低于 5 s、峰值低于 1000 MiB；持续 fuzzing 与流式内存上限待完成 |
| **M7** | ◐ | 发布工程：README/CI/示例画廊/发布元数据 | v0.9.0-rc1 准备中；公开发布与仓库元数据待确认 |

## 8. 竞品基线

| 现有实现 | 弱点（我们要超越的） |
|---|---|
| LibreOffice (ww8 import) | 过滤选项繁多但还原度不稳定；依赖巨大 |
| ONLYOFFICE doc 转换 | 部分特性降级明显；服务化部署重 |
| antiword / wvWare | 只出文本/HTML，早已停维护，fast-save 支持差 |
| 在线转换服务 | 隐私风险、格式丢失、不可批量 |

我们的差异化：**IR 枢纽的干净分层 + sprm 级逐项验收 + 秒级性能 + 零依赖单二进制 CLI**。

## 9. 风险

- fast-saved 文档的重建是最大复杂度来源（M2 重点验证）
- Escher 图形语义庞大，M5 按「内联图完整、浮动图主流环绕类型完整、其余降级」策略推进
- 加密文档仅在拿到足够真实加密语料后才能闭环（M6 提前收集）
