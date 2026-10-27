# doccraft 设计文档

**doccraft** — 高保真 .doc → .docx 转换器（C++20，自研解析，零依赖核心）

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

- **FIB**（File Information Block）：版本探测（nFib 0x00C1→Word97 … 0x011A→Word2007）、
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
  - 加密原语（M6 RC4 等）：用成熟实现，密码学不自造
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

- RC4 Encryption（Word 97-2003 主流）：[MS-DOC] 2.4.2 / [MS-OFFCRYPTO] 二进制加密
  - 流程：Password → SHA-1 迭代 → RC4 密钥；解密 FIB 确认（验证加密标识），
    解密 Table Stream 与 Text/Char/Fld/Parit 段
- XOR obfuscation（Word 95 遗留）仅检测报告，不做转换（明示不支持原因）
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
- ◐ fast-saved 文档（fComplex）：按最终 CLX piece 表重建正文，应用 Prm0 单条及 Prm1/Prc 字符与段落修饰；真实增量保存 golden、其他故事中的 Prc 格式及表格结构修饰仍待验证
- ◐ 特殊字符：tab/CR/VT/非断行空格/连字符/SYM 字符（图片/脚注/批注/绘图等特殊字符随 M4/M5）
- ◐ 域代码文本与结果（fldChar 层次）：通用复杂域输出 `w:fldChar` begin/separate/end + `w:instrText`，`HYPERLINK` 仍映射为 `w:hyperlink`、页码类域映射为 `w:fldSimple`；`PlcfFldMom` 的 `fldLock`/`dirty` 标志已保留，域结果中的图片/对象仍未保留
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
- ◐ 正文浮动文本框：PlcftxbxTxt/PlcfTxbxBkd 故事范围、FTXBXS shape id 与 PlcfSpaMom 锚点连接，按 Tbkd 顺序展开并写出 `w:pict/w:txbxContent`；内联形状、链中各形状的独立排版、页眉文本框及复杂形状外观仍待补齐
- ☐ 基本形状/艺术字占位（可降级为占位图，文档化降级策略）
- ◐ OLE 嵌入对象：字段分隔符上的 `sprmCPicLocation` 定位 ObjectPool 子存储，完整流族重新封装为 CFB 嵌入部件，字段结果图片作预览并输出 `w:object/o:OLEObject`；仅有 `\1OlePres000` 而无字段结果图的预览尚未解码
- ◐ 脚注/尾注/批注/书签/交叉引用域/目录域（脚注、尾注、批注与书签已按 PLC 解析并输出 `footnotes.xml`/`endnotes.xml`/`comments.xml`/`w:bookmarkStart`-`End`；交叉引用与目录域的指令与结果文本按通用复杂域输出）

### 6.7 样式系统
- ✅ STSH 全量解析（基于样式继承链展开）
- ✅ 样式映射到 docx styles.xml（段落/字符/表格/编号样式，含表样式条件格式 `w:tblStylePr`）
- ◐ 直接格式 vs 样式格式的优先级正确叠加（条件格式按出现顺序合并，未按 2.4.6 排序）

### 6.8 加密
- ☐ RC4 加密文档解密（口令参数）
- ◐ 加密类型检测报告：FIB 可区分 XOR 与 RC4 家族并明确拒绝；RC4/RC4 CryptoAPI 子类型与错误口令报告待 M6 解密入口

### 6.9 健壮性
- 性能实测基线见 [perf-baseline.md](perf-baseline.md)；当前 50 MiB 样本超过 5 s 目标，后续优化以分阶段计时为依据。
- ☐ 损坏文档的优雅降级（跳过坏块 + 警告列表，不崩溃）
- ☐ 超大文档流式处理（分块、内存上限）
- ☐ fuzz 测试套件零崩溃

## 7. 里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M1** | layer1 CFB 容器层（含 MiniFAT/DIFAT）+ CLI inspect | 合成+真实容器 golden 全过；fuzz 无崩溃 |
| **M2** | FIB/piece table/文本抽取 + 纯文本 → docx 最小通路 | 语料集文本一致率 ≥ 99% |
| **M3** | CHP/PAP/SEP sprm 全量 + 样式系统 | 特性清单 6.2/6.3/6.4/6.7 全 ✅ |
| **M4** | 表格 + 列表 + 域 + 脚注批注书签 | 6.5 + 6.6 后半 ✅ |
| **M5** | 图片/Escher/文本框 + OLE 对象 | 6.6 前半 ✅ |
| **M6** | RC4 解密 + 性能调优 + fuzz 加固 | 6.8/6.9 ✅；p99 < 1s |
| **M7** | 发布工程：README/GIF/CI/badge/示例画廊 | v1.0 公开发布 |

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
