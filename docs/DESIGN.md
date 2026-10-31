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

`src/api/` 同样不是数据流的一层，而是四层的第二个入口：`doccraft_convert_buffer`
把整份 .doc 作为内存缓冲读入、把 .docx 作为缓冲写出，供 WebAssembly、Android
（NDK）与 iOS 这类没有文件系统和命令行的目标使用；它只依赖四层与 `base`，不做
任何文件 IO。CLI（`src/cli/`）是带文件 IO 的第一个入口。两者的流程一致，产物
逐字节相同（`tests/api_cli_parity.py` 断言）。

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
- 关系按源部件写入 `word/_rels/<源部件名>.rels`；页眉、页脚、脚注、尾注和批注各自拥有独立的 rId 空间，无关系时省略该文件。
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
  - 字符集解码：**不引入第三方库**，按编译目标选后端——POSIX（Linux/macOS，
    以及带 libSystem 的 iOS 与提供 iconv 的 Android NDK 目标）用 **iconv**
    （glibc/libSystem 内置；只有 GNU libiconv 等独立库存在时才链接），Windows 用
    系统 API `MultiByteToWideChar` + UTF-16→UTF-8，两者都没有的目标（WebAssembly）
    退化到内置 CP1252 表。CP1252 在任何平台都走内置表，保证默认代码页的输出逐字节
    一致；`base::SupportsCodepage()` 如实反映当前后端，调用方据此降级。见
    `src/base/charset.cc`。
- **平台设施**（同样不算依赖，都是标准库或系统 API）：
  - 文件系统探测：`std::filesystem`（C++23 标准库，六个目标一致），不再用
    `stat(2)`/`access(2)`；文件 IO 只存在于 `src/cli/`。
  - 诊断输出：Windows 上把 stdout/stderr 切到二进制模式，避免 CRT 把 `\n`
    改写成 `\r\n`，保证诊断逐字节跨平台一致。
  - 线程：`std::thread` 与 `thread_local`（字符解码描述符缓存），无平台分支。
- 构建开关：`DOCCRAFT_BUILD_CLI`（默认 ON；嵌入式目标关掉）、
  `DOCCRAFT_BUILD_TESTS`（默认 ON，需网络拉 googletest，且要求 CLI）、
  `DOCCRAFT_WARNINGS_AS_ERRORS`（默认 ON；MSVC 警告集不同，CI 的 Windows job
  暂时 downgrade 为 OFF）。
  - 测试：googletest、libFuzzer
  - **仅测试期**的 OOXML schema 校验：`src/ooxml/` 用
    `tools/schema-gen` 离线导出的元素粒子表，在 `ctest` 中检查 writer 产出的
    WordprocessingML 部件是否符合 ECMA-376 的元素顺序与出现次数。表由
    `DocumentFormat.OpenXml` 3.3.0（MIT）的运行时元数据生成并已入库，构建和
    运行不需要 .NET；校验器不链接进 CLI 产物。
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
- ◐ Word 6/95 文本：实测 nFib 0x0065/0x0068 的旧 FIB、同流 CLX 与简单 fcMin 文本可读；CP1251 大文档与 Word 6 俄文样本已对拍，CP950 合成字节有断言。M12 起读取旧版 CHPX/PAPX FKP（1 字节 opcode 归一化为现代 sprm；r30 起旧版制表位 `sprmPChgTabsPapx`(0x0F) 与 `sprmPChgTabs`(0x17) 分别映射为现代 0xC60D/0xC615 并进入 `Paragraph::tabs`，输出 `w:tabs`（`LegacyWord.Word6ChgTabsPapxReachesDocx`、`LegacyWord.Word6ChgTabsFlatFormReachesDocx`），cb=0xFF 扩展形态仍只消费不翻译（r36 起两 opcode 的删除项同样进入 `Paragraph::tabs` 并输出 `w:tab w:val="clear"`，`LegacyWord.Word6ChgTabsClearOnlyReachesDocx`）；r16 起按 LO `GetWW6SprmSearcher` 定长表跳过表内未映射项，仅表外 opcode 或操作数截断停止，后续修饰不再被株连；r19 补映射 sprmPWr(0x25) 与 sprmCLid(0x61→sprmCRgLid0 + sprmCRgLid1)；r31 补 0x35–0x3A 东亚段落开关（按 [MS-DOC] 2.9.215 的 Prm0 isprm 同编号映射到现代 0x2433–0x2438）并把 0xBF（sprmTDefTableShd）按 spra=6「cb 表示其后剩余字节数（操作数共 cb+1 字节）」测宽，`Bug51944.doc` 与 `Bug50955.doc` 的 unsupported-sprms 停扫均已消除），M13 起旧版 STSH 样式表（pair 1：8 字节 STD 基座、Pascal 名、偶对齐 UPX、based-on/next 链）与 FFN 字体表（pair 15：cbFfnM1 条目、文档代码页 ANSI 名）进入同一条样式继承链，`sprmCFtc` 字体索引解析为字体名；M14 增加 STSHI 默认字体索引到 Normal 的回填与记录级畸形警告；旧版 STSHI 无字号字段，Bug50955/Bug51944 的 Normal 字号仍与 LibreOffice 合成值不同。M8 续批增加 WordDocument 中旧 PICF 的直存 WMF 图片，位图和其他 MFPF 类型在转换时警告并跳过（不再拒绝整篇；图片大小上限仍为致命错误）；旧版页眉页脚自 r23 起接入：按 DOP.grpfIhdt 跳过文档级保留故事，再按各节 SEPX 的 `sprmSGprfIhdt`（0x99）位序消费 PlcfHdd 中实际存在的故事，奇偶故事仅在 DOP.fFacingPages 置位时挂接、首页故事仅在 `sprmSFTitlePage`（0x8F）置位时挂接；旧版脚注自 r23-fix 起读取 WordDocument 中的 pair 2/3（PlcffndRef/PlcffndTxt），布局与现代同构，真实样本 `080_080742.doc` 输出 63 条脚注（`LegacyWord.Word6FootnoteBodyReachesDocx`、`LegacyWord.Word95FootnoteBodyReachesDocx`），旧版尾注自 r27 起读取 `ccpEdn` 与 WordDocument 中的 pair 46/47（`PlcfendRef`/`PlcfendTxt`；旧版 pair 38 起的槽位较 Word 97 后移 10 字节），由 `LegacyWord.Word6EndnoteBodyReachesDocx`、`LegacyWord.Word95EndnoteBodyReachesDocx` 断言；非 complex 文档的单个 fcMin 文本片段扩展到页眉区；旧版 SEPX 的节属性自 r28 起接入：纸张/页边距/分栏/页码/分页方式/纸张来源/页面方向/表单保护/行号/注位置等经 `WalkLegacySepx` + `ApplyLegacySectionSprm` 映射到同一 `ApplySection`（`LegacyWord.Word6SepxSectionPropertiesReachDocx`、`LegacyWord.Word6PerSectionSepxPropertiesFollowSection`），`sprmSFTitlePage`（0x8F）同时驱动 `w:titlePg` 与首页故事；表内未映射的 0x85（SOlstAnm）、0x92（SFAutoPgn）、0x94/0x95（SDyaPgn/SDxaPgn）、0xA3（SBCustomize）、0xAB（SDMPaperReq）按宽度跳过，0x99（SGprfIhdt）仍只用于页眉故事选择；旧版表格标量属性自 r32 起映射六项（0xB6/0xB7/0xB8/0xB9/0xBA/0xBD → 现代 sprmTJc/sprmTDxaLeft/sprmTDxaGapHalf/sprmTFCantSplit/sprmTTableHeader/sprmTDyaRowHeight，`LegacyWord.Word6TablePropertiesReachDocx`），边框/网格/底纹自 r33 起映射：0xBB(6×BRC6→sprmTTableBorders)、0xBC/0xBE(cb+itcMac+XAS+TC6→sprmTDefTable 的 TC80；短 TC 退化为网格-only 的 sprmTDefTableObsolete)、0xBF(cb+SHD→sprmTDefTableShd80)，真实样本 Bug49933/word95err/Bug60942/Bug50955 的网格与底纹进入 `w:tblGrid`/`w:shd`，TC6 的 BRC10 单元格边框仍未转换（`LegacyWord.Word6TableBordersGridAndShadingReachDocx` 等）；编辑类 0xC2–0xC6 自 r38 起映射为现代 sprmTInsert/sprmTDelete/sprmTDxaCol/sprmTMerge/sprmTSplit（操作数布局与 Word 97+ 相同，按 4/2/4/2/2 字节直通，`LegacyWord.Word6TableWidthAndInsertReachDocx`、`LegacyWord.Word6TableDeleteReachesDocx`、`LegacyWord.Word6TableMergeAndSplitReachDocx`），0xC0/0xC1/0xC7/0xC8（TTlp/TSetBrc/TSetBrc10/TSetShd）仍按宽度跳过且不翻译。其他故事尚未支持。
- ◐ fast-saved 文档（fComplex）：最终 CLX piece 表重建正文，Prm0/Prm1/Prc 修饰覆盖正文和页眉/脚注/批注/文本框故事；piece 上的 sprmPItap 表格深度有合成断言。正文表格状态按段落结束符 PAPX 选取，Bug33519 的 60 个碎表收敛为 3 个且行单元格对齐；旧、新 DOCX 的 LO 文本导出均为 691/691 词；版式 golden 未通过
- ◐ 特殊字符：tab/CR/VT/非断行空格/连字符/SYM 字符（图片/脚注/批注/绘图等特殊字符随 M4/M5）
- ◐ 域代码文本与结果（fldChar 层次）：通用复杂域输出 `w:fldChar` begin/separate/end + `w:instrText`，顶层未锁定 HYPERLINK 映射为 `w:hyperlink`、顶层页码类域映射为 `w:fldSimple`，其余（含嵌套/锁定）一律 fldChar 层次；正文 `PlcfFldMom`、页眉/脚注/尾注/批注故事 `PlcfFldHdr/Ftn/Edn/Atn`、正文及页眉文本框故事 `PlcfFldTxbx/PlcffldHdrTxbx` 的 `fldLock`/`dirty` 标志均已接入；`M11TextboxFields.FieldFlagsReachBodyAndHeaderTextboxXml` 合成断言覆盖两个文本框 XML 输出路径。真实 Word 文本框域 golden 尚缺，故保持 ◐；`fcPlcfFldMcr` 规范定义为 undefined 不读；正文 EMBED 对象按 ObjectPool 保存
- ✅ 多语言/复杂文种（RTL 标记、bidi、复杂文种颜色与字号）

### 6.2 字符格式（CHP）
- ✅ 字体族/字号/加粗/斜体/下划线（多种线型）/删除线/双删
- ✅ 上标/下标/字距/字符缩放/隐藏文字/着重号/字符单位适配（`w:fitText`、`w:eastAsianLayout`）
- ✅ 颜色/高亮/边框底纹/阴影/浮雕
- ✅ 超链接（HYPERLINK 域）
- ✅ 修订标记作者：`sprmCIbstRMark`/`sprmCIbstRMarkDel`/`sprmCPropRMark` 的 ibst 查 `SttbfRMark`（FIB pair 51，[MS-DOC] §2.5.6 FibRgFcLcb97 第 52 行 fcSttbfRMark，紧邻 fcDggInfo=pair(50)；pair(24) 是 fcCmds——初版误读 pair 24 且「经 fcClx 锚点关系验证」的表述不实，按该关系（pair(i)=表格第 i+1 行）正确推得的正是 pair 51，经真实 Word97 语料字节验证勘误，见 M19Revisions.FibReadsSttbfRMarkFromPair51、M19Revisions.RealWord97SttbfRMarkParsesThroughFib）写入 `w:ins`/`w:del`/`w:rPrChange` 的 `@w:author`；无 ibst 用规范默认 0 号 "Unknown"；表缺失/结构非法/索引越界回退占位作者 `doccraft` 并警告；插入/删除时间戳由 `sprmCDttmRMark`/`sprmCDttmRMarkDel` 的 DTTM 写入 `w:ins`/`w:del` 的 `@w:date`（ISO-8601 UTC，M19Revisions.InsertDateFromDttm、M19Revisions.DeleteDateFromDttm；day 或 month 为 0、minute > 59、hour > 23、month > 12 的 DTTM 按 [MS-DOC] 2.9.65 忽略、不输出，M19Revisions.ZeroDttmIgnored、M19Revisions.ZeroDayDttmIgnored、M19Revisions.ZeroMonthDttmIgnored、M19Revisions.MinuteOutOfRangeDttmIgnored、M19Revisions.HourOutOfRangeDttmIgnored、M19Revisions.MonthOutOfRangeDttmIgnored、M19Revisions.DeleteZeroDayDttmIgnored、M19Revisions.DeleteOutOfRangeDttmIgnored）

### 6.3 段落格式（PAP）
- ✅ 对齐（左/中/右/两端/分散）/缩进（左右/首行/悬挂，twips 与字符单位）/行距/段距（twips 与行单位）
- ✅ 段落边框与底纹/项目符号与多级列表（LSTF/LVL/override）
- ✅ `sprmPIncLvl` 的样式级别增量：istd 1–9 增量并夹紧，轮廓级别为最终 istd−1；其他样式未显式指定轮廓级别时沿最终样式的 based-on 链求值，再应用轮廓级别增量；无该增量的段落保留样式继承表达
- ✅ 制表位（自定义/toc 风格）/keepNext/keepLines/pageBreakBefore/东亚排版开关；ChgTabs 的删除项自 r36 起输出 `w:tab w:val="clear"`；r37 起样式 based-on 链与段落直接 PAPX 的 tabs 按 ChgTabs 语义合并（派生/段落 add 覆盖同位置、匹配的 clear 删除继承项、未匹配的 clear 丢弃），段落 clear 仅在样式确有该位置时输出，输出按 position 升序且 clear 在前；无样式可查的段落保持操作数原序
- ✅ 大纲级别/段落阴影/首字下沉（`w:framePr` dropCap）/纵向文字流

### 6.4 节与页面（SEP）
- ✅ 纸张大小/方向/页边距/装订线/纸张来源（`w:paperSrc`）
- ✅ 页眉页脚（首页/奇偶不同、逐节独立故事）/页码域
- ✅ 分节符类型（连续/ nextPage /evenPage/oddPage）/行号/脚注位置/页面边框与保护
- ◐ 非等宽分栏（`sprmSFEvenlySpaced=0` + `sprmSDxaColWidth`/`sprmSDxaColSpacing`）：逐列输出 `w:col`；违反「每列 MUST 带宽度」的列按 LibreOffice `.doc` 导入兜底 1440 twips 补宽、仅间距列保留 `w:space`，不再静默丢弃（`M19Columns`）；兜底为常量不按剩余空间均分，且仅缺间距的列与 LO `.doc` 路径因其数组压缩缺陷不对拍，限制见 KNOWN-SIMPLIFICATIONS「M3 当前范围」

### 6.5 表格
- ✅ 基本网格（TAP/rgtc）/单元格合并（横向 fVertMerge、纵向）/拆分
- ◐ 单元格边框/底纹/边距/对齐/文字方向/浮动表格（sprmTPc 锚点、sprmTTlp 表样式选项）、单元格内多段落与嵌套表格（按 sprmPItap 深度递归，内层 CR 标记按 sprmPFInnerTableCell/sprmPFInnerTtp 分割行列）；缺 sprmPItap 的样本仍按单层输出。r34：sprmTCellPadding（0xD632）的空范围与超出行的范围按 LibreOffice 语义静默忽略/截断，不再拒绝整篇（`M3Batch3.TableCellPaddingIgnoresEmptyCellRange`、`M3Batch3.TableCellPaddingClampsRangeToRow`）

### 6.6 对象与图形
- ✅ 内联图片（PICF + Escher BLIP：png/jpg/wmf/emf 提取与格式保留；正文图片，其他故事与非嵌入 BLIP 限制见 KNOWN-SIMPLIFICATIONS）
- ◐ 浮动图片与文字环绕（Escher anchor + 环绕类型映射）：FSPA anchor 定位（bx/by → margin/page/text，复用浮动表格语义）与主流环绕（topAndBottom/around=Square/none/rough=Tight→Through 合成覆盖）已通，LibreOffice golden 对拍达其内部基线下限；轮廓级 Tight/Through 区分需真实 Word 样本（LibreOffice 导出两者同为 wr=4），已知限制登记 KNOWN-SIMPLIFICATIONS
- ◐ 浮动文本框：正文与页眉专用 FTXBXS/Tbkd 故事范围按 shape id 连接各自的 PlcfSpa 锚点，写出 `w:pict/w:txbxContent`；PICF 内联 OfficeArtClientTextbox 指针可连接文本框故事并输出内联 `w:txbxContent`，缺失范围保留空框并警告；链中每个 Tbkd 分段分别按 OfficeArtClientTextbox 链位置连接自己的 FSPA 锚点并写出独立文本框。页眉文本框锚点不属于任何页眉 PLC 故事区间时逐框跳过并警告，锚点越出 `ccp_hdd` 仍按整表结构错误跳过（`M6TextboxDegradation.HeaderAnchorOutsideStoriesKeepsOtherBox`）。无指针的旧形状依 FSPA 顺序推断，真实 Word 链式布局 golden 仍待验证
- ✅ 基本形状/艺术字占位：OfficeArtFSP 形状类型、FSPA 几何和 GeoText 原文进入 IR；矩形/椭圆/线条输出带形状类型注释的 VML 浮动占位，带 BLIP 的形状仍走图片路径。OfficeArt `fillColor`/`fillType`（msoFillSolid）与 `lineColor`/`lineWidth` 映射为 `v:fill`/`v:stroke`，flag 字节非 0 的索引色不映射并退回 VML 默认。`M5FloatingImages.GeometryShapePlaceholderKeepsAnchor`、`ShapeFillAndLineComeFromOfficeArt`、`IndexedOfficeArtColorKeepsDefaultPaint`、`LibreOfficeBasicShapesKeepGeometry`、`LineAndWordArtTypesKeepIdentity`；图案/渐变填充、旋转与环绕的限制见 KNOWN-SIMPLIFICATIONS
- ◐ OLE 嵌入对象：字段分隔符上的 `sprmCPicLocation` 定位 ObjectPool 子存储，完整流族重新封装为 CFB 嵌入部件；字段结果图片优先作预览，缺失时解码 `\1OlePres000`/`\2OlePres000` 的 EMF/WMF 元文件或 CF_DIB 位图（转 BMP 部件）并输出 `w:object/o:OLEObject`。`sprmCFObj` 缺 OLE 标记或 ObjectPool 子存储时警告并输出 `[Embedded object]`；链接语义及 `\3EPRINT` 等其余预览格式仍待补齐
- ◐ 脚注/尾注/批注/书签/交叉引用域/目录域（脚注、尾注、批注按各自 PLC 输出部件；Word 6/95 脚注体自 r23-fix 起按 pair 2/3 接入，尾注自 r27 起按 pair 46/47 与 `ccpEdn` 接入；脚注/尾注/批注的文本 PLC 尾 CP 自 r29 起在现代路径同样允许超出故事至多 2 CP 并按故事长度 clamp（`kNoteTextPlcGuardCps`，`M15NoteTextGuard.FootnoteTailCpWithinGuardClampsNote`、`M15NoteTextGuard.EndnoteTailCpWithinGuardClampsNote`、`M15NoteTextGuard.CommentTailCpWithinGuardClampsComment`），超出 2 CP 或中间 CP 越界仍整故事拒绝（`M15NoteTextGuard.FootnoteMiddleCpBeyondStorySkipsStory`）；批注范围按 PlcfAtnBkf/PlcfAtnBkl 与 ATRDPre10 书签 tag 映射，缺少范围书签时保持点锚（`M3Comments.RangeBookmarkSurroundsAnnotatedText`、`M15StoryPlc.AdjacentCommentRangesCrossRunsAndControl`、`M15StoryPlc.CommentRangeMayExtendPastReference`、`M15StoryPlc.InvalidCommentRangeKeepsOtherRange`），ATRDPre10 initials 按 LPXCharBuffer9 的 2 字节 cch（最多 9）与 18 字节 UTF-16 缓冲读取（`M15StoryPlc.CommentInitialsUseTwoByteCountAndUtf16Payload`、`M15StoryPlc.CommentInitialsFillNineCharacterBuffer`、`M15StoryPlc.CommentInitialsKeepSupplementaryCharacter`、`M15StoryPlc.CommentInitialsDropTrailingHighSurrogate`、`M15StoryPlc.CommentInitialsCheckCodeUnitAfterHighSurrogate`、`M15StoryPlc.OversizedCommentInitialsWarnAndKeepComment`），注释首段的 `w:footnoteRef`/`w:endnoteRef` 标记、340 twip 制表位与按名称复用的注释文本/字符样式由 writer 合成；标准书签的 `SttbfBkmk`/`PlcfBkf`/`PlcfBkl` 覆盖正文、页眉页脚、脚注、尾注、批注及正文/页眉文本框故事，并输出 `w:bookmarkStart`/`w:bookmarkEnd`；正文整域书签在 `ccpText` 闭合；非正文书签仅在起止位于同一故事时输出，结束位于故事守卫 CP 时仍会闭合，跨故事配对跳过并警告；文本框链的内部 Tbkd 断点 CP 标记由下一分段输出，仅故事末 CP 按守卫附着；各分段的精确书签排版尚未对拍；交叉引用与目录域的指令与结果文本按通用复杂域输出）

### 6.7 样式系统
- ✅ STSH 全量解析（基于样式继承链展开；Word 6/95 旧版 STSH 自 M13 起接入同一链路：名称/基于/下一样式与 UPX 的段落、字符修饰经归一化后同规则叠加；M14 对 STSHI 默认字体和损坏记录给出有界处理与警告，旧版默认字号尚未还原）
- ✅ 样式映射到 docx styles.xml（段落/字符/表格/编号样式，含表样式条件格式 `w:tblStylePr`）
- ◐ 直接格式 vs 样式格式的优先级正确叠加（表样式条件格式已按 [MS-DOC] 2.4.6.6 的条带、列、行、角顺序输出；r37 起段落制表位在样式 based-on 链与直接 PAPX 之间按 [MS-DOC] 2.9.183 求值，表样式条件块（`w:tblStylePr`）的 tabs 同样相对所属表样式的 resolved 集求值，`M3Styles.TabsMergeWithBaseInsteadOfReplacing`、`M3Styles.StyleOutputUsesResolvedTabsAndClearsDeletedBaseStop`、`M3Styles.ParagraphTab*`、`M3Styles.TableConditionalTabs*`；其余段落/字符/编号属性仍未完整按规范求值）

### 6.8 加密
- ◐ XOR obfuscation 解密（`--password`）：ASCII 1–15 字节口令的合成 `.doc`
  fixture 与明文 `document.xml` 逐字节一致（`m16_xor_cli`）；非 ASCII 口令及
  真实 Word 生成样本仍待覆盖。
- ✅ RC4 加密文档解密（`--password`）：传统 RC4/MD5 与 RC4 CryptoAPI/SHA-1；`m6_rc4_cli` 与 `M6Rc4.*`。符合性经规范原文与 LibreOffice 参考实现复核；真实 Word 加密样本互通待补
- ✅ 加密类型检测报告：XOR、传统 RC4、RC4 CryptoAPI 分别报告；缺口令与错误口令均明确报错（`m6_rc4_cli`）

### 6.9 健壮性
- 性能实测基线见 [perf-baseline.md](perf-baseline.md)；100 MiB 结构化样本已低于 5 s，后续优化以分阶段计时和 RSS 打点为依据。
- ◐ 损坏文档的优雅降级：正文可读时跳过局部坏 CHPX FKP 页/坏 sprm 序列；PAPX FKP 页的局部页结构损坏按 BTE FC 范围丢弃，连同未闭合及后续连续表格一起省略；坏页眉、脚注、尾注、批注故事 PLC（批注作者表同故事处理）亦可跳过（仅限槽位有效、PLC 内容损坏；fc/lcb 越出 WordDocument 属 FIB 结构损坏，仍整篇拒绝）。批注范围 PLC 的单条 CP 范围损坏只跳过该范围并警告，initials 长度越界时仅清空该条 initials 并警告。正文/页眉文本框的损坏 FTXBXS/Tbkd 范围按故事跳过（`M6TextboxDegradation.BadSecondStoryKeepsFirstTextbox`、`InvalidBodyPlcKeepsMainText`、`InvalidHeaderPlcKeepsHeaderText`、`m6_bad_textbox_story_cli`）；整表 PLC 范围无效时跳过对应文本框集合（`m6_bad_textbox_cli`），页眉文本框锚点不属于任何页眉故事区间时仅跳过该框，锚点越出 `ccp_hdd` 时仍按整表结构错误跳过（`M6TextboxDegradation.HeaderAnchorOutsideStoriesKeepsOtherBox`）。`convert` 向 stderr 输出警告且退出码为 0；PAPX sprm/表格定义损坏及 CFB/CLX 截断仍拒绝，更多格式路径待覆盖。r34 起 CFB 只在输入不足一个完整扇区时按截断拒绝，尾部不足一个扇区的残片被忽略后再解析（`CfbReaderTest.ToleratesTrailingPartialSector`、`CfbReaderTest.RejectsFileShorterThanOneSector`）
- ◐ 旧版 SEPX 节属性（r28）：`ReadSections` 与页眉读取共用 `ReadLegacySepxPayloads` 提取 SEPX（含 fcSepx 高 1 字节回退），再由 `WalkLegacySepx` 按一字节表翻译到 `ApplySection`；表内未映射项按宽度跳过，表外 opcode 或操作数截断沿用 `unsupported Word 6/95 SEPX sprm` / `truncated Word 6/95 SEPX` 错误并整篇拒绝（`LegacyWord.Word6UnsupportedSectionSprmRejectsDocument`、`LegacyWord.Word6TruncatedSectionSprmRejectsDocument`），无 SEPX 的节保持默认 `Section`（`LegacyWord.Word6SectionWithoutSepxKeepsDefaultProperties`）；剩余跳过项见 KNOWN-SIMPLIFICATIONS「r23 页眉脚注边界，r28 节属性接入」
- ◐ 超大文档内存控制：按需释放无 ObjectPool 文档的 CFB 原始输入，正文解码字符缩至 16 字节/字符；100 MiB 样本峰值低于 1000 MiB。writer 序列化部件总缓冲及单部件上限均为 512 MiB，单部件与总缓冲超限均返回具名的 `std::expected` 错误；总缓冲错误含触发部件名、字节数与累计总和/上限（`M6WriterBuffer.SerializedPartsLimitReturnsError`、`M6WriterBuffer.SinglePartLimitReturnsNamedSize`、`M6WriterBuffer.TotalLimitErrorNamesOffendingPart`）；分块流式处理与进程峰值内存上限仍待实现。
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
