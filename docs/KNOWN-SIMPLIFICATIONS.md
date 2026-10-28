# 已知简化与后续工作

以下是 M1 CFB review 建议项中未纳入本轮修复的部分。

1. 测试仍以结构化回归用例和 1000 次确定性单字节变异为主；尚无真实语料变异或持续运行的 libFuzzer 入口。三项已知崩溃输入均已加入结构化测试。
2. Header 偏移 40 的目录扇区数尚未核验：v3 应为 0，v4 应等于实际目录链长度。
3. `ReadStream` 的 `/` 路径分隔符与流名本身含 `/` 时存在匹配歧义；当前仍允许按完整路径、相对路径或 entry 名称查找。`ReadChain` 要求最后一个扇区的 FAT 项恰为 `ENDOFCHAIN`，尚未兼容多余链尾扇区。这两点是当前读取策略，后续需结合真实文件确定是否放宽。

## M2

- `fComplex=1`（增量保存）文档按最终 CLX piece 表读取正文，Prm0 与 Prm1/Prc 字符及段落修饰现也应用于页眉、脚注、批注和文本框故事；piece 上的 `sprmPItap` 已参与正文表格深度判定。Prm0 中仅修订元数据用途的 `sprmPFNumRMIns` 不映射。真实样本的版式 golden 尚未通过，§6.1 保持 ◐。
- 真实 fast-saved 批量对拍（LibreOffice UTF-8 文本，源 `.doc` vs 转换 `.docx`）：Apache POI 的 `Bug33519.doc` 表格结构现按段落结束符 PAPX 判定，避免旧 FC 文字片段误关表；`fastsavedmix.doc`、`rasp.doc`、`o_kurs.doc`、`ob_is.doc`、`samplefastsaved.doc` 的既有文本差异与版式 golden 限制仍需逐项复核。`Bug50955.doc`、`Bug51944.doc` 已由 Word 6/95 分支读取；`Bug60942.doc` 的 CFB 链长超限，`word_with_embeded.doc` 缺 CLX Pcdt，仍拒绝。`o_kurs.doc` 复制到 fixtures 用于可重用文本框 PLC 尾项及 XML 控制字符回归。
- IR 仅包含段落、文本 run 和特殊控制码位置。`0x07` 单元格/行结束与 `0x0B` 换行在最小 DOCX 中写为换行，不重建表格；列表编号、样式、页眉脚注等留待后续里程碑。
- 当前最小写出为达到语料 `plain-text.doc` 的像素验收，使用其默认字体（Liberation Serif，12pt）及 Letter 页面 1134 twip 页边距。尚未从 DOC 属性读取字体、字号和页面设置，因此其他语料的版式不保证一致。
- 文本一致率测试把连续表格控制码折叠为单个换行，并剥除 LibreOffice TXT 导出中根据编号属性生成的 `•` 列表标记。这些标记不在 M2 抽取的主文本中；不计此差异时五份语料内容均为 100%。
- 8-bit（compressed）piece 按 `FibBase.lid` 推导出的 ANSI 代码页解码（`src/base/charset.cc`）。两条残留限制：其一，解码依赖 iconv（glibc 内置 / libiconv），编译期未找到时退化为 CP1252，仅 cp1252 能正确解码；其二，LID 表中未列出的语言一律按 1252 处理（例如 cp1253 之外的希腊文变体、OEM 代码页）。DBCS 文本按规范总是存为 UTF-16 piece，所以压缩 piece 只需要代码页的单字节映射；若遇到违规的 DBCS 双字节序列，能解出时按两字节消费，否则该字节记为 U+FFFD。
- 语料已加入 `cjk-text`（中/日）与 `cyrillic-text`（俄文）两份真实 `.doc`，但它们的正文按规范存为 UTF-16 piece，验证的是 Unicode 文本链路而不是代码页路径；8-bit 代码页路径仍只有单测（`CharsetTest.*`、`PieceTableTest.DecodesCompressedPiecesInDocumentCodepage`）覆盖。
- `0x0C`（分页符）及其他未列于已处理集合的 <0x20 控制符被静默丢弃（分页边界信息不保留）。
- 非 complex 文档若 CLX piece 末 CP 小于 `ccpText`，保留已覆盖正文并向 CLI stderr 警告期望与实际 CP 数；尚未校验 piece 内部空洞。现有 35 份 corpus/fixtures `.doc` 均未触发此警告。
- U+FFFE/U+FFFF 不在 XML 1.0 Char 产生式内，替换为 U+FFFD 而非报错（防止损坏文件产出 not-well-formed 的 document.xml）。
- 孤立 UTF-16 代理项（未配对的 high/low surrogate）同样替换为 U+FFFD 而不报错。文本抽取路径原先是报错，现在与格式化路径共用唯一的 CLX 读取器 `doc::detail::ReadChars`，因此单字符损坏不再使整篇转换失败。

## M4 当前范围

- Apache POI 的 `Bug33519.doc`（FIB `fComplex=1`）在压力语料中，旧版本将表格拆成 60 个；按段落结束符 PAPX 选择表格状态后为 3 个。以同一 LibreOffice TXT 导出流程对照源文件，旧、新 DOCX 均为 691/691 词且词序一致率 1.000；压力目录中旧 `.our.txt` 的 0.862 是单元格边界被抽取脚本直接拼接造成的口径差异。`M4FastSaved.ParagraphMarkPapKeepsFastSavedCellsTogether` 覆盖文字和结束符落在不同 FC 的情况。该修复尚不能保证 fast-saved 的版式 golden，§6.1 保持 ◐。
- FIB 的 `fObfuscated` 可区分 XOR obfuscation 与 RC4 家族；Table 流头部进一步区分传统 RC4 与 RC4 CryptoAPI。XOR obfuscation 已支持 ASCII 1–15 字节口令，按规范校验 FibBase.lKey 并解密 WordDocument、Table、Data 流；非 ASCII 口令暂明确拒绝，尚无真实 Word 生成的 XOR 样本互通验证。

## Word 6/95 读取边界

- 实测 Apache POI 的 `Bug50955.doc`、`Bug51944.doc` 为 nFib `0x0068`，`Word6.doc`、`52117.doc` 为 `0x0065`；签名分别出现 `0xA5DC`、`0xA699`，并非先前推测的 `0x0052`。旧 FIB 的字符数在 `0x34`，38 个 fc/lcb 对从 `0x58` 起，CLX 及文本都位于 WordDocument。直接 FC 的 ANSI/UTF-16 PCD 与简单文档的 `fcMin` 已接入；损坏的范围、CLX 和 PCD 显式拒绝。
- 四份旧版真实样本的 WordDocument 流均带 BTE/FKP：`Word6.doc`、`52117.doc` 为 nFib `0x0065`，`Bug50955.doc`、`Bug51944.doc` 为 `0x0068`；pair 12/13 均非空，页号为 2 字节。现读取旧版 CHPX/PAPX 页及常用标量 sprm（粗体、斜体、字号、下划线、段落对齐/缩进/间距、表内标记、禁用自动连字、字符间距、字体索引等）。M13 起旧版 STSH（fc/lcb pair 1，与 pair 0 同指）与 FFN 字体表（pair 15）接入：8 字节 STD 基座 + Pascal 名 + 偶对齐 UPX，样式名与字体名按文档代码页解码（LO 对 STSH 亦用文档级 GetFIBCharset 而非条目级 charset），`sprmCFtc` 字体索引映射为字体名，based-on/next 链复用现代 ResolveStyle 语义；对 4 份真实样本与 LibreOffice 输出的 styles.xml 逐字段核对一致（`LegacyStyles.*` 合成断言）。实测旧版 `sprmCHps` 操作数为 2 字节（wvWare spra 位 + LO aSprms 表双证；hps<256 时与"1 字节+高字节当 opcode-0 跳过"的走读等价），M13 修正了 M12 的 1 字节读取（hps≤255 输出不变）。剩余边界：旧版制表位 `sprmPChgTabsPapx` 消费长度后静默丢弃；`sprmCLid`(0x61；IR 有语言字段，但现代 `sprmCRgLid0` 是 6 字节 rgLid 操作数，宽度不符不能直译)、`sprmPWr`(0x25，十进制 37；本语料未遇到) 等仍按未识别处理（警告并跳过该组剩余修饰）；FFN 条目级 chs 差异未用（统一文档代码页）；STD 的 bchUpe 字段不校验；LibreOffice 会为被引用的空槽合成内置样式，本转换器保留空槽为默认样式；旧版完整表格属性、页眉脚注仍未映射；图片仅接入直存 WMF，位图及其他 MFPF 类型未支持，转换版式会退化。`52117.doc` 的 lid 为 en-US，但 FFN 字库携带俄文 charset，按 CP1251 解码；该修正只覆盖可在 FFN 中识别的俄文，其他 lid 与字符集冲突仍可能误解码。`Bug51944.doc` 的 lid 同为 en-US，CLI 从有界 `\005SummaryInformation` CodePage 属性读出 950 作为旧版 ANSI 回退代码页；提供的 LO 文本实际无汉字。CP950 繁体解码由 `LegacyWord.Word95AnsiPieceUsesTraditionalChineseCodepage` 的 Big5 字节合成断言验证，尚缺带真实繁体汉字的 Word 95 golden。

- M14 实测四份 Word 6/95 STSHI 的 cbStshi 仅为 14/14/14/18 字节，没有所假设的尾部 212 字节 CHP。固定字段后可读取标准字体索引：`Bug50955.doc` 为 0（FFN 中的 Times New Roman Cyr），`Bug51944.doc` 为 1/4/1（ASCII/东亚/其他），前者的 Normal 字体已回填为 Times New Roman Cyr，后者的 ASCII 字体已回填为 Times New Roman。当前 IR 的 Run 只有单一字体名，不能保留东亚字体 `新細明體`；这些 STSHI 中也没有默认字号，LibreOffice 为这两份文件合成的 Normal 20 半点字号仍不输出。未硬编码内置字号；待取得可靠的默认字号来源及多字体槽模型后处理。

## M6 设计取舍

- §2.1 原定「加密原语用成熟实现、密码学不自造」已反转为按 RFC 6229、RFC 1321、FIPS 180-4 自实现并以标准向量钉死：RC4/MD5/SHA-1 各 <150 行，引 OpenSSL 会破坏单二进制零运行时依赖的发布承诺。

## M6 性能

- 性能第四批把正文解码后的 `Char` 缓冲由 20 字节/字符缩至 16 字节/字符，并在没有 ObjectPool 的文档中于流提取后释放原始 CFB 输入。100 MiB 样本约 5170 万字符，仍需一次性保留约 789 MiB 解码缓冲直到格式解析结束；这不是通用内存上限，超大正文仍需分块流式解析。ANSI/iconv 若把单个字符展开成超过 4 字节 UTF-8，当前显式报错，未为这种罕见映射保存旁路字节。
- 性能门禁默认在临时 worktree 以 Release 构建基线提交 `ee2df0e`，与当前二进制同期交替双跑；构建失败时告警并降级为 JSON 对比。JSON 基线于 2026-10-28 重录（`a170874`、空载 Release、每档 5 次取墙钟中位数与峰值 RSS 最大值）；RSS 超过同期基线 30% 仅告警，先观察两轮。

## M6 加密

- RC4 回归样本由 `m6_fixture_gen` 从仓库明文样本反向加密，分别覆盖传统 RC4/MD5 与 RC4 CryptoAPI/SHA-1；verifier、错误口令、跨块解密和 `document.xml` 与明文逐字节一致已验证。40-bit CryptoAPI（含 KeySize=0 缺省）由 `M6Rc4.DecryptsCryptoApi40BitKeyVariants` 合成用例覆盖。尚未获得 Word 实际加密导出的 `.doc` 作外部互通验证。
- 仅转换 WordDocument、Table、Data 主流。RC4 CryptoAPI 的独立加密属性流（按 [MS-DOC] 2.2.6.3 在 `fDocProps` 置位时出现；[MS-OFFCRYPTO] 2.3.5.1 措辞与其相悖，通行实现按 [MS-DOC] 极性）不迁移到 OOXML 元数据；嵌入对象在加密文档中的独立存储兼容性尚未验证。
- 正文 `PlcfFldMom` 的 `fLocked`/`fResultsDirty` 已保存到复杂域和 PAGE 简单域；状态化 HYPERLINK 会输出通用复杂域以保留位标志。页眉/脚注/尾注/批注故事的字段 PLC 自 M8 起接入（见「M8 当前范围」），正文及页眉文本框的字段 PLC 自 M11 起接入。M8 续批扫描 `/tmp/doccraft-m4-real` 的 151 个 `.doc`：仅 `64132.doc`、`Bug50936_1.doc`、`Bug53380_1.doc` 同时有文本框故事字符数和字段 PLC 长度；LibreOffice 转出的 DOCX 分别是「有文本框无域」「有域无文本框」「两者都无」。网上搜索未找到可验证的真实组合样本，因此 §6.1 域项保持 ◐。

## M6 变异加固

- 已交付 `tests/fuzz_test.cc`（入 ctest，双构建全绿；Release 全套 ~2.7s，ASan ~13s）。覆盖：全管线种子变异（plain-text、complex-table、fast-saved、inline-image、RC4 加密 5 种子 × 2000 轮/种子，ASan 下 300 轮/种子）、sprm/FKP 定向变异（PAPX/CHPX 字节、opcode 高位轰炸、`sprmPHugePapx`/`sprmPTableProps` 嵌套 grpprl + Data 流）、CLX/piece 表变异（fc/lcbClx 极值、CP 倒序、compressed 位、ccpText 不一致）、全 65536 opcode 的 dispatch 直调，以及代表性损坏的确定性错误断言。首轮运行即发现 `sprmPItap` 深度未钳制导致的 writer 递归栈溢出（现按下条统一钳制，附 `Fuzz.SprmItapDepthOperandIsClamped`/`Fuzz.SprmDtapSummedDepthIsClamped` 回归测试）。
- 嵌套表格深度按 `kMaxTableDepth=8` 压平（Word 自身到 4 层）：`sprmPItap`/`sprmPDtap` 是 spra 3 的 4 字节有符号操作数，解析层（`src/doc/format/sprm.cc` 的 `ApplyPara`）与格式帧栈层（`src/doc/format/formatted.cc` 两处读取点）统一经 `internal.h` 的 `ClampTableDepth` 钳到 [0,8]，两层策略一致，IR 的 `Paragraph::table_depth` 与实际构建的嵌套结构恒相同（统一前解析层对越界操作数是整条丢弃，字段与结构可互相矛盾）。深度增量先在 64 位中累加，再钳到 [0,8]，避免有符号溢出 UB；例如 3 加 `INT32_MAX` 得到 8，3 加 `INT32_MIN` 得到 0。症状：深度 >8 的超深嵌套不报错也不丢弃，最内层内容会出现在第 8 层单元格里；钳到 0 的深度按既有的 fInTable 规则回退为一层。合法深度 ≤8 的语料输出逐字节不变（35 个 corpus/fixtures 样本 part 级 sha256 对比为零差异）。
- 全管线种子变异的 ASan 轮数（300 轮/种子 × 5 种子）依赖 8s 墙钟阀门：预算耗尽后每个种子在 16 轮粒度的检查点提前收尾（每种子保底 64 轮），慢机器少跑轮次而不是把 ctest 拖到超时。本机实测 ASan 下 5 种子 300 轮全部跑满共用时 6.79s，阀门余量约 1.2s；更慢的机器上轮次会被削减（覆盖度下降，但不会 flaky）。旋钮是 `tests/fuzz_test.cc` 中 `FullPipelineSeedMutationNeverCrashes` 上方的命名常量：`kSeedMutationRoundsAsan`（ASan 轮数）、`kSeedMutationRounds`（常规轮数）、`kSeedMutationMinRounds`（保底）与 `kSeedMutationBudget`（墙钟预算）。
- 残余缺口（合成变异 ≠ 持续 fuzzing）：固定 seed 有限轮次，无覆盖率导引；种子池未含 legacy RC4/MD5 加密样本与 >7 MiB DIFAT 扩展语料；libFuzzer 驱动（`fuzz_driver.cc`）需 clang + `DOCCRAFT_ENABLE_LIBFUZZER=ON` 选配构建，默认关闭、不入 ctest，CI 无常驻 fuzz 入口；页眉/脚注/文本框等故事内 sprm 变异与 OLE/Escher 字节变异未单列策略（当前仅经全管线种子变异间接触达）。

## M3 当前范围

- 大于约 7 MiB 的 LibreOffice 导出 `.doc` 使用 DIFAT 扩展，最后一个 DIFAT 扇区的 next 字段写 `FREESECT`；[MS-CFB] §2.5 要求 `ENDOFCHAIN`。读取器仅在声明的 DIFAT 扇区数已走完且 FAT 扇区数匹配时兼容这个终止符。合成测试 `CfbReaderTest.ReadsLibreOfficeFreeTerminatedDifat` 与 20/50 MiB 语料覆盖此兼容路径。

- `footnote-endnote.doc` 曾出现 A=1、B=2 的页数分歧（尾注被放到独立文末页）。根因是注释正文只写了段落、没有注释标记也没有注释文本样式，渲染端只能按默认 12pt 正文排注释。writer 现在为每条注释补 `w:footnoteRef`/`w:endnoteRef` + 制表符（停位 340 twip），并按 STSH 名称套用 `footnote text`/`endnote text` 段落样式与 `footnote characters`/`endnote characters` 字符样式，注释正文回到 10pt 与 340 twip 悬挂缩进。页数分歧消失，`tests/fixtures/footnote-endnote.allow` 已删除；110 dpi 下与源 `.doc` 的差异为 64 bad_px（0.005%），与 LibreOffice 自身 `.doc`→`.docx` 的 64 bad_px 完全同量级。
- 对拍的非零下限受 LibreOffice 自身 `.doc`/`.docx` 导入差异影响。同一 `.doc` 由 LibreOffice 另存 `.docx` 后回读，`cjk-text` 仍为 1470 bad_px（0.068%）、`table-merge-direction` 为 785（0.037%）、`complex-table` 为 38（0.002%）；`table-borders-shading` 则为 0。热力图显示前三者的差异集中在文字笔画或位置，并非推断中的表格边框宽度错误。doccraft 的单元格左内边距补偿使 `table-borders-shading` 降至 0，`table-merge-direction` 降至 282（0.013%）；其余差异不按像素零差目标盲目改写规范属性。

- CHP/PAP/SEP 的常用 sprm、STSH 样式链、LSTF/LVL 列表、首节页眉页脚和基础表格网格已读写。表格单元格合并、复杂边框底纹、浮雕/阴影等效果仍简化；复杂格式需要独立样本验证。
- 多节文档逐节写出属性切换、行号与脚注位置，并为每节生成独立的页眉页脚部件（PlcfHdd 每节 6 个故事，首个故事前有 6 个保留 CP）。两条限制：其一，节内页眉里的图片与表格只保留文本与段落格式，不含 drawing 部件（域自 M8 起按正文语义输出）；其二，故事末段落是守卫标记，会被丢弃，因此「仅有一个空段落」的页眉在输出中不存在。
- 样式表保留段落、字符、表格和编号样式的条目及继承链接；表格样式条件格式（`sprmTCnf`/`sprmPCnf`/`sprmCCnf`）按 `cnfc` 聚合并依 [MS-DOC] 2.4.6.6 的水平条带、垂直条带、首末列、首末行、角单元格顺序输出。同一条件的 TAPX/PAPX/CHPX 三段仍按出现顺序合并；段落/字符/编号链没有完整按 2.4.6.6 求值，保留 §6.7 的 ◐。
- `sprmPHugePapx` 和 `sprmPTableProps` 会按 Data 流偏移读取嵌套 PrcData；超出流边界则忽略该引用。其复杂表格语义未全部映射。
- 浮动段落框映射 `w:framePr` 的位置/尺寸/间距/锁定、首字下沉（`sprmPDcs`）与纵向文字流（`sprmPFrameTextFlow`，仅宽高非默认时才输出）；`w:drawing` 级锚点与绕排多边形仍缺。
- 浮动表格用 `w:tblpPr` 表达，锚点由 `sprmTPc` 的 `pcVert`/`pcHorz` 映射为 `margin`/`page`/`text`（缺省为 `margin`）；表格文本环绕距离已映射。`sprmTWidthBefore`/`sprmTWidthAfter` 的行级前后缩进仍无对应属性。
- 非等宽分栏（`sprmSFEvenlySpaced` + `sprmSDxaColWidth`/`sprmSDxaColSpacing`）：[MS-DOC] §2.6.4 要求非等宽节的每列 MUST 带 `sprmSDxaColWidth`。违反该约束的列不再丢弃——`w:cols` 为每列输出 `w:col`，缺宽度的列（含仅指定间距的列）按 LibreOffice `.doc` 导入的兜底值 1440 twips 补宽（`sw/source/filter/ww8/ww8par6.cxx` 中缺失宽度 sprm 时取 1440），仅间距列保留 `w:space`（`M19Columns.SpacingOnlyColumnKeepsWColWithFallbackWidth`）。等宽节（未写 `sprmSFEvenlySpaced=0`）仍忽略列宽度/间距 sprm：规范规定两者仅在非等宽时生效。剩余限制：兜底宽度为常量，不按页面剩余空间均分，Word 对该违规输入的行为未验证；仅缺间距（宽度齐全）的列在 LibreOffice `.doc` 导入路径会因其 `rgdxaColumnWidthSpacing` 数组压缩而错位（LO 自身缺陷），doccraft 的 docx 路径按无 `w:space`（间距 0）输出，两条路径在该畸形输入下不对拍。
- SHD80 系列（`sprmCShd80`/`sprmPShd80`/`sprmTDefTableShd80`）走 16 色调色板，索引 0（自动色）不产生 `w:shd`；现代 SHD 操作数仍以 COLORREF 优先。
- `sprmPIncLvl` 对 `istd` 1–9 按有符号操作数增量并夹到 1–9；最终轮廓级别为 `istd−1`，忽略样式 UPX 的 `sprmPOutLvl`。生成的段落样式 ID 可在 XML 中观察，目标样式的编号仍由现有样式表路径决定。`istd` 不在该范围时，显式轮廓级别增量仅当 `sprmPOutLvl` 在同一 PAPX 中先于 `sprmPIncLvl` 出现时才可见；规范 ispmd 序（2 < 64）下，`sprmPOutLvl` 后应用并覆盖增量。仅有增量而无显式级别时按最终样式的 based-on 链求值；没有增量的段落由样式表达继承属性；继承级别为 9 时增量按规范忽略。既有直接编号 `ilfo` 路径继续累加 `ilvl`。LibreOffice WW8 导入分派表对该 sprm 设空处理器，无法用其导入结果作此分支的行为基准。
- `sprmCFObj` 对缺 `sprmCFOle2` 的对象标记输出 `[Embedded object]` 并警告；OLE 字段的 ObjectPool 子存储缺失时也降级为同一占位。对象外观和缺失载荷无法恢复；图片标记缺载荷仍沿用显式错误路径。
- `w:framePr` 与表格浮动定位用 `-1` 作「未设置」哨兵，`-1` twip 的真实偏移不可表达。
- 域处理按「顶层特例 + 其余通用」分层：只有顶层未锁定、未标脏的 `HYPERLINK` 改写为 `w:hyperlink`、含 `PAGE` 的顶层域改写为 `w:fldSimple`；其余（含全部嵌套域）输出 `w:fldChar` begin/separate/end 与 `w:instrText`。`\x13` 后未见 `\x14` 就遇到 `\x15` 的畸形域按 begin + instrText + end 输出，指令不再被静默丢弃。`PlcfFldMom`/`PlcfFldHdr`/`PlcfFldFtn`/`PlcfFldEdn`/`PlcfFldAtn` 及正文、页眉文本框故事字段 PLC 的结束记录 `fLocked`/`fResultsDirty` 已映射为 `w:fldLock`/`w:dirty`；真实 Word 文本框域 golden 尚缺。
- 域结果里的图片、对象、脚注引用等仍是 M5 载荷；嵌套在超链接结果中的域会把外层链接降级为普通文本。
- 嵌套表格按 `sprmPItap`（绝对深度）建立递归结构：`Cell::blocks` 与正文一样是 Block 序列，深度下降时把内层表格挂到外层单元格上。两条限制：其一，深度信息只来自 `sprmPItap`/`sprmPDtap`，若生产者不写这两个 sprm（缺失时按「在表格内 = 一层」推断），嵌套仍会被摊平；其二，LibreOffice 导出 `.doc` 时会把嵌套表格摊平成段落，所以本级只有合成位流样本（`tests/m3_nested_table_test.cc`），没有真实 Word 样本。
- `sprmPFInnerTableCell` 仍未使用：结构与深度已由 `sprmPItap` 决定，该标记不参与判定。
- 脚注/尾注已按 `PlcffndRef`/`PlcffndTxt`（FIB pair 2/3）与 `PlcfendRef`/`PlcfendTxt`（pair 46/47，注意不是 44/45——那两个是 fcPms/fcFormFldSttbs）解析，正文里的 `0x02` 变成引用 run，`Document::notes` 保存正文并按 `w:footnoteReference`/`w:endnoteReference` + `footnotes.xml`/`endnotes.xml`（含 separator/continuationSeparator）输出。限制：脚注内容只按 CHPX/PAPX 还原文本与段落格式，脚注内的图片、表格不处理（域自 M8 起处理，含 `PlcfFldFtn`/`PlcfFldEdn` 标志）；脚注编号始终自动编号（不还原自定义标记）；`PlcfendTxt` 末尾可能多一个守卫 CP，实际条数以引用 PLC 为准。注释正文的标记由 writer 合成：DOC 文本平面不含编号，因此每条注释首段写入 `w:footnoteRef`/`w:endnoteRef` + 制表符与 340 twip 停位，并按名称复用 STSH 的 `footnote text`/`endnote text`（段落）与 `footnote characters`/`endnote characters`（字符）样式（`M3Notes.NoteBodyCarriesReferenceMarkAndTextStyle`）；若文档 STSH 没有这些名称，退化为不写样式、只保留标记与制表符，编号字号/缩进随之由阅读器默认值决定。
- FIB 里脚注或尾注的引用/文本 PLC 若越界、CP 顺序错误、条目数不匹配（包括引用 PLC 缺失但文本 PLC 存在）或 CP 超出故事范围，会跳过该故事的两张 PLC、引用及输出部件，并向 stderr 警告；另一种 note 故事与正文继续转换。note text PLC 的相邻等值 CP 视为畸形并跳过该故事。故事内字段 PLC 损坏仍只丢失字段标志。尚未验证所有真实损坏文档的特殊守卫 CP 变体。
- 批注按 `PlcfandRef`（FIB pair 4）+ `PlcfandTxt`（pair 5）+ `GrpXstAtnOwners`（pair 36）解析：正文的 `0x05` 变成锚点 run，`Document::comments` 保存正文与作者，输出 `comments.xml` 与 `w:commentRangeStart`/`w:commentRangeEnd`/`w:commentReference`。相邻批注锚点允许同一 CP；作者数组末尾允许单字节垫料，完整的作者名前缀仍用于批注。限制：锚点是**零长度区间**（真实 Word 里批注可覆盖一段文字，本实现只在引用点开合）；作者名依赖 ATRDPre10 第 20 字节的作者索引，索引越界时退化为姓名缩写（该偏移是从 LibreOffice 样本反推的，未经 Word 样本验证）；批注时间戳未保留（ATRDPre10 无日期字段，`w:date` 省略）。
- 标准书签按 `SttbfBkmk`（FIB pair 21）+ `PlcfBkf`（pair 22）+ `PlcfBkl`（pair 23）解析，绝对 CP 落在正文、页眉页脚、脚注、尾注、批注及正文/页眉文本框故事时输出 `w:bookmarkStart`/`w:bookmarkEnd`。规范没有独立的非正文书签 FIB pair；文本框文本 PLC 的 CP 从各自文本框文档起点计，读取时加上故事绝对 CP 基址与标准书签配对。正文整域书签允许在 `ccpText` 排他终点闭合，结束标记附着最后段落。页眉、脚注、尾注、批注及文本框书签可在同一故事末尾的守卫 CP 闭合：结束标记附着到最后一个段落，不输出守卫字符；守卫 CP 上的起点不输出。跨故事或范围异常的配对整条跳过并给出警告，不影响同区其他有效书签与文本；没有可读取的目标故事时，落在该故事的书签不输出。文本框链按 FTXBXS 故事范围过滤；内部 Tbkd 断点 CP 上的书签标记由下一分段在首字符前输出，只有故事末 CP 才按守卫附着。链中各 Tbkd 分段的精确书签排版尚无真实样本对拍。限制：FBKF 的 BKC 字段忽略；其中 fNative 的规范措辞为保存成 RTF/HTML/XML 时「not expected to be included」，并非禁止输出，Word 保存 DOCX 也保留 `_Hlt`/`_Toc`/OLE 书签；名字缺失时按 `Bookmark<序号>` 兜底；正文 PLC 尺寸错误会失败，但未全面验证名称数、CP 顺序和配对，`ibkl` 越界的书签整条丢弃。
- 表样式（`sprmTIstd`、条件格式 `sprmTCnf`/`sprmPCnf`/`sprmCCnf`）没有真实 `.doc` 语料：LibreOffice 26.8 导出 `.doc` 时会把表样式摊平成单元格直接格式，`tests/fixtures` 与 corpus 中因此不存在带 TAPX 的表样式。这条链路由 `tests/m3_styles_test.cc` 直接构造 STSH 位流断言（`M3Styles.TableStyleDefaultsAndConditionals`），一旦拿到 Word 生成的样本应替换为 golden 样本。
- 修订标记（`sprmCFRMarkDel`/`sprmCFRMarkIns`）输出为 `w:del`/`w:ins`，作者按 `sprmCIbstRMark`/`sprmCIbstRMarkDel`/`sprmCPropRMark` 的 ibst 查 `SttbfRMark`（FIB pair 51；初版误读 pair 24=fcCmds 且「经锚点关系验证」表述不实，按 pair(i)=表格第 i+1 行的关系正确推得的正是 pair 51，经真实 Word97 语料字节验证勘误）写入 `@w:author`，未给 ibst 的标记用规范默认 0 号 "Unknown"；表缺失、结构非法或索引越界回退占位作者 `doccraft` 并警告（M19）。仍省略：DTTM 时间戳（`@w:date`）；批注作者走独立的 `GrpXstAtnOwners`（pair 36），不经 SttbfRMark；`w:id` 仅保证文件内自增唯一。
- `sprmTTlp` 只把 `grfatl` 的 bit5–10 映射为 `w:tblLook`：首行/末行/首列/末列直接对应，band 位按「置位=启用带状」取反为 `noHBand`/`noVBand`；`itl`（自动格式索引）丢弃。
- `sprmCKcd` 以外的新增字符属性只写单向推导：`sprmCFELayout` 的 `iWarichuBracket` 仅在 `fWarichu` 置位时输出，`sprmCFitText` 的 0 宽度视为未设置。

## M5 当前范围

- M8 续批接入 Word 6/95 的单字节 `sprmCPicLocation`(0x44)：只解码 WordDocument 中 66 字节 PICF 直存、MFPF=8 的 WMF，字节原样写入媒体部件；MFPF 位图(99) 明确报不支持，其余类型也明确拒绝。合成样本覆盖 IR 与媒体部件，尚无真实旧版图片 golden。`Bug50955.doc` 的本转换输出无媒体部件，不能用它证明旧版图片验收。

- 正文内联图片解码后保存到 IR 的字节总量上限为 256MiB（`kMaxTotalImageSize`）；重复引用也累计，恰好达到上限允许，超限以 `total image size exceeds limit` 使整篇转换失败，不降级或丢图。检查发生在当前图片解码后，因此该上限不是进程峰值内存上限；元文件仍受单图 64MiB 限制。

- 正文内联 `U+0001` 图片按 `sprmCPicLocation` 从 Data 流读取 PICF，扫描嵌入 Escher BLIP 并输出 DrawingML 与原格式媒体部件。PNG/JPEG 有 LibreOffice golden 媒体字节对比及像素对拍；WMF/EMF 的压缩和非压缩 BLIP 有合成位流断言，但尚无 LibreOffice 导出的真实元文件 golden。非嵌入或延迟 BLIP、ObjectPool/OLE、裁剪/边框/旋转，以及页眉、脚注等其他故事中的图片留待后续；遇到正文内联图片的缺失或不支持载荷会显式返回错误。
- 正文及页眉浮动文本框通过各自 FTXBXS.lid 连接 PlcfSpaMom/PlcfSpaHdr，并以 VML `w:pict/w:txbxContent` 输出。PICF 内联 OfficeArtClientTextbox 指针可按 FTXBXS 索引与链位置连接故事；对应故事范围缺失时输出空框并警告。链中的 Tbkd 分段分别写入各形状的 FSPA 锚点；若生产者未写 OfficeArtClientTextbox，则按首形状后连续 FSPA 的顺序推断并警告，可能误配夹在其中的非文本框形状；缺少对应锚点时拒绝转换。形状填充与边框仍未映射，链式容量和溢出布局缺真实 Word golden。页眉文本框在所属故事的首段锚定；同故事内多个段落的精确插入位置尚未恢复。LibreOffice 的透明无边框样本 `textbox.doc` 对拍通过（bad_ratio 0.093%）。
- 正文文本框故事跳过或无效时，若对应 FSPA 带可读取的 OfficeArt 形状，形状仍走几何占位路径，输出无文本的 VML 占位形状（如 `v:rect`）；文本框故事的文字不附着到该形状。该行为由 `ParseFormatted` 的浮动形状回退路径与 `WriteShape` 的空文本分支确认。
- PICF 的 OfficeArt 子容器若没有可提取 BLIP、但恰有一个 FSP 形状，可用 PICF 目标尺寸写出内联 VML 几何占位。形状填充、边框、旋转和多形状组合未还原；真实内联形状 golden 尚缺，当前只以合成 PICF 验证。
- 基本浮动形状读取 OfficeArtFSP 的 msospt、FSPA 坐标与尺寸。带 BLIP 的形状走图片输出；纯几何形状用带类型注释的 VML `v:rect`/`v:oval`/`v:line` 占位。填充与边框按 OfficeArt `fillColor`/`fillType`（仅 msoFillSolid）与 `lineColor`/`lineWidth` 映射为 `v:fill`/`v:stroke`；OfficeArtCOLORREF 的 flag 字节非 0（调色板/系统色/方案色索引）时不映射，保留 VML 默认（无填充、黑色默认线）。艺术字的 GeoText 或文本框故事原文放入占位框；曲线字形、图案/渐变填充、旋转不还原。LibreOffice `basic-shapes.doc` 对拍中矩形/椭圆的线色由黑变蓝后 bad_ratio 从 0.148% 降到 0.111%（110 dpi 下 1906→1407 bad_px），仍略高于通用 0.1% 阈值：剩余差异是形状垂直锚点与正文绕排（LO 把这两个形状按“随字符”锚定并让正文排在右侧，doccraft 输出绝对定位 VML，正文排在下一行），按本批降级验收仅认可占位位置与线色/填充，完整外观和绕排留待后续。`M5FloatingImages.ShapeFillAndLineComeFromOfficeArt`、`IndexedOfficeArtColorKeepsDefaultPaint`、`LibreOfficeBasicShapesKeepGeometry` 覆盖解析与写出。
- OLE EMBED/LINK/CONTROL 字段按分隔符上的 ObjectPool id 定位子存储，并将所有子流重新封装为独立 CFB 嵌入部件。字段结果 PICF/BLIP 图片优先作 `w:object` 的 VML 预览；没有字段结果图时从 `\1OlePres000`/`\2OlePres000` 读取预览：CF_METAFILEPICT/CF_ENHMETAFILE 原格式透传，CF_DIB（格式 8）的 packed DIB 加 14 字节 `BITMAPFILEHEADER` 后作为 `media/imageN.bmp` 输出（`image/bmp`，`M4Ole.OlePresentationBitmapBecomesBmpPart`）。三种都严格校验流边界；不支持的剪贴板格式（含 `\3EPRINT`）仍返回无预览，链接对象的外部链接语义尚未恢复。真实样本来自 Apache POI `word_with_embeded_ooxml.doc`（原样复制为 `tests/fixtures/ole-embedded.doc`）；OlePres 的元文件与位图路径目前只有合成流测试，无真实 Word golden。
- 损坏文档现有局部降级范围为 CHPX FKP 页（含坏 sprm 序列）、PAPX FKP 页的局部页结构损坏、页眉故事 PLC，脚注、尾注、批注故事的引用/文本 PLC（批注还含作者表），以及正文/页眉文本框的 FTXBXS/Tbkd 范围。文本框范围错误按 FTXBXS 故事跳过，PLC 整表结构错误跳过对应的正文或页眉文本框集合；页眉文本框锚点不属于任何页眉 PLC 故事时，仅跳过该框并警告 `skipped header textbox: anchor outside header stories`，其他文本框及正文文本保留（`M6TextboxDegradation.HeaderAnchorOutsideStoriesKeepsOtherBox`）；锚点越出 `ccp_hdd` 属于 PLC 前置校验范围，仍按整表跳过对应文本框集合。PAPX sprm 序列和表格定义损坏，以及 CFB/CLX/FIB 的全局损坏继续拒绝。更多格式路径待细化，因此 §6.9 保持 ◐。
- writer 对含页眉/页脚在内的每个 OPC 部件序列化后，在 `AddPart` 中先检查单部件大小，再检查已序列化载荷总和，通过后移入 ZIP 部件列表；两个生产上限均为 512 MiB。单部件超限错误含 ZIP 完整路径名和实际字节数（`M6WriterBuffer.SinglePartLimitReturnsNamedSize`、`M6WriterBuffer.LastSectionHeaderPartLimitReturnsError`），总和超限返回 `writer buffer limit exceeded`（`M6WriterBuffer.SerializedPartsLimitReturnsError`）。序列化后检查不限制构造期峰值；该口径也不包括 pugixml 树、输入 IR、ZIP 压缩缓冲和结果副本，不能保证进程 RSS 上限；分块流式写出与通用内存上限仍待实现。
- M8 续批对局部损坏的 PAPX FKP 页按其 BTE FC 区间标记并跳过；当前表格帧直接丢弃，区间后若 PAPX 仍指示表内则继续跳过，直至正常段落恢复。损坏页若同时覆盖普通段落，这些段落也会随页省略；跨越多个 PAPX 页的同一表格可能丢失完整表格。`m8_bad_papx_cli` 对真实表格样本注入坏 FKP，确认 CLI 成功、stderr 警告且损坏表格不输出；CLX 越界继续拒绝。
- 浮动图片按 PlcfSpa→FSPA（anchor 定位 bx/by 复用浮动表格 margin/page/text 语义）+ BStore BLIP 输出 `wp:anchor` 与 `wp:wrap*`。环绕映射：wr=1→TopAndBottom、wr=2→Square、wr=3→None、wr=4→Tight（输出为 wrapTight 而非 wrapThrough）。限制：其一，LibreOffice 导出不区分轮廓 Tight 与 Through（两者同为 wr=4），真实 Word 样本获取前该区分以合成流覆盖；其二，绕排多边形（wrapTight 的 msoWrapPolygon）未还原，LibreOffice 基线亦不含；其三，floating-image/floating-tight 的像素对拍与 LibreOffice 自身 `.doc`/`.docx` 双路径基线下限完全一致（rms=18.59/bad_px=27724），该差异为 LO 导入不对称，非 doccraft 缺陷。
- 字符单位缩进（`sprmPDxc*`/`sprmPDyl*`）只写 `w:ind/@*Chars` 与 `w:spacing/@*Lines`，不重复写 twips 绝对值；阅读器若忽略 *Chars 会退回样式默认缩进。
- `sprmSPgbProp` 的枚举按 0=页面边缘/1=文字、0=全部页/1=首页/2=非首页解释；`reserved` 字节非 0 时整条 sprm 视为损坏。
- `sprmSFProtected` 只在 `sprmSFProtected` 出现时输出 `w:formProt`（值为「未被保护」的反相），文档级 `DOP.fProtEnabled` 尚未解析，未出现该 sprm 的节不写保护节点。
- M3 仅保留越界不崩溃底线。针对 sprm 攻击面的专项分析、定向 fuzz 和防御补强移至 M6。
- `src/base/binary.h` 的 `U16`/`U32`/`U64`/`S16` 统一了字节序解码，但不做边界检查：越界安全仍由调用方的前置 `subspan`/范围判断保证。带游标、越界即 `std::expected` 失败的读取器需要重写全部调用点，随 M6 的 fuzz 加固一起做。

## M8 当前范围

> M11 后续：正文/页眉文本框字段 PLC 已接入；下文的 M8 规则同样用于这两个故事。`M11TextboxFields.InvalidPlcWarnsAndKeepsHierarchy` 断言损坏 PLC 整表跳过并警告，域层次保留而锁/脏标志丢失。

- 非正文故事的字段接入：页眉/页脚（`PlcfFldHdr`，FIB pair 17 0 基）、脚注（`PlcfFldFtn`，pair 18）、尾注（`PlcfFldEdn`，pair 48）、批注（`PlcfFldAtn`，pair 19）各自读到「字段开始绝对 CP → grffldEnd 字节」映射，页眉用一张全区域映射按故事区间自动归属各节。M11 再接入正文文本框（`fcPlcfFldTxbx`，pair 57）与页眉内文本框（`fcPlcffldHdrTxbx`，pair 59），各自 PLC 的 CP 从各自文本框文档起点计。域层次来自文本中的 `0x13`/`0x14`/`0x15`，PLC 只补充 `fldLock`/`dirty` 标志。`fcPlcfFldMcr`（pair 20）按规范「undefined and MUST be ignored」不读。
- 故事字段 PLC 的降级策略：条目 CP 非严格递增、越过本故事 ccp、fldch 种类非法或尺寸不满足 `(size-4) mod 6 == 0` 时，整张 PLC 跳过并输出警告（`skipped <故事> field PLC: …`），该故事退化为「有域层次、无锁/脏标志」，正文与故事正文不受影响；正文 `PlcfFldMom` 维持严格失败策略不变。
- 锁定域的输出与正文逐字节同构：`w:fldLock` 写在 begin/separate/end 三个标记上；锁定 HYPERLINK 的结果 run 仍包在 `w:hyperlink` 里（沿用正文既有行为）。页眉/页脚/脚注/尾注/批注部件复用 `WriteParagraph`，无独立的域输出逻辑。
- 仅含空结果域（例如 `\x13 PAGE \x14\x15`）的页眉没有文本 run；既有的 has-text 策略会丢弃该故事部件，若文档只靠此类页眉使用奇偶页设置，`settings.xml` 中的 `evenAndOddHeaders` 也不会输出。
- 页眉/页脚、脚注/尾注和批注中的图片、OLE 对象及超链接关系现按源部件写入各自的 `_rels/<部件名>.rels`，各部件的 rId 独立编号。没有这些关系的部件不生成空关系文件。

## OOXML schema 校验器（`src/ooxml/`）

- 覆盖范围只有 WordprocessingML 2006 main 命名空间的**元素**：子元素顺序、
  出现次数、未知元素。属性（必填性、枚举、数值范围）与 part/关系引用尚未
  校验，`OpenXmlValidator` 能报的属性类与关系类错误目前检不出来。
- 校验器按「命名空间 + 本地名」匹配，而 SDK 按 CLR 类型区分实例。同一 QName
  在不同上下文有不同内容模型时（`w:customXml` 作为 block/run/row/cell、
  `w:align`/`w:bottom` 等），生成器把多份粒子合并成一个 choice，任一形态通过
  即可，因此「只在另一形态下非法」的顺序错误会漏报。
- 表内已知的跨命名空间元素（`wp:inline`/`wp:anchor`、`a:*`、`m:*`、`v:*`）
  参与父元素的序列匹配，但其自身内容不递归校验。早期版本把非 WML 元素一律
  过滤，导致 `w:drawing` 被误判为缺少必填的 `wp:inline`/`wp:anchor`。
- 表内没有的扩展元素（`mc:AlternateContent`、`w14:*` 等）不参与序列匹配，
  可出现在任意位置（MCE 语义），放错位置不会被发现。
- 单个元素的子节点达到 65535 个时放弃该元素的序列校验（表用 16 位索引），
  实际文档不会触及。
- 批注范围退化为点锚点：`commentRangeStart`/`commentRangeEnd` 只包住
  `commentReference` 所在的 run，被批注文本本身不高亮。IR 只有单个
  `comment_ref` 标记，没有范围的起止位置。
- `xs:all`（WordprocessingML 中共 3 处）按 `xs:sequence` 处理。
- SDK 元数据用 0 表示 `xsd:maxOccurs="unbounded"`，生成器按此解释；升级 SDK
  后需要人工核对这一编码假设。
- 表由 `tools/schema-gen` 离线生成并入库（`src/ooxml/generated/`），构建和
  运行不需要 .NET。升级 SDK 或修正生成器后要重跑：
  `dotnet run --project tools/schema-gen/DumpSdkMetadata -- tools/schema-gen/wml-metadata.json`
  然后 `python3 tools/schema-gen/gen_wml_schema.py tools/schema-gen/wml-metadata.json src/ooxml/generated`。
