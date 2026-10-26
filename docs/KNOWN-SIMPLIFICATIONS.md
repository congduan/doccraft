# 已知简化与后续工作

以下是 M1 CFB review 建议项中未纳入本轮修复的部分。

1. 测试仍以结构化回归用例和 1000 次确定性单字节变异为主；尚无真实语料变异或持续运行的 libFuzzer 入口。三项已知崩溃输入均已加入结构化测试。
2. Header 偏移 40 的目录扇区数尚未核验：v3 应为 0，v4 应等于实际目录链长度。
3. `ReadStream` 的 `/` 路径分隔符与流名本身含 `/` 时存在匹配歧义；当前仍允许按完整路径、相对路径或 entry 名称查找。`ReadChain` 要求最后一个扇区的 FAT 项恰为 `ENDOFCHAIN`，尚未兼容多余链尾扇区。这两点是当前读取策略，后续需结合真实文件确定是否放宽。

## M2

- `fComplex=1`（增量保存）文档会明确返回 `unsupported (M2 known gap)`；M2 只解读非增量保存的 CLX。加密文档返回 `unsupported (M6)`。
- IR 仅包含段落、文本 run 和特殊控制码位置。`0x07` 单元格/行结束与 `0x0B` 换行在最小 DOCX 中写为换行，不重建表格；列表编号、样式、页眉脚注等留待后续里程碑。
- 当前最小写出为达到语料 `plain-text.doc` 的像素验收，使用其默认字体（Liberation Serif，12pt）及 Letter 页面 1134 twip 页边距。尚未从 DOC 属性读取字体、字号和页面设置，因此其他语料的版式不保证一致。
- 文本一致率测试把连续表格控制码折叠为单个换行，并剥除 LibreOffice TXT 导出中根据编号属性生成的 `•` 列表标记。这些标记不在 M2 抽取的主文本中；不计此差异时五份语料内容均为 100%。
- 8-bit（compressed）piece 按 `FibBase.lid` 推导出的 ANSI 代码页解码（`src/base/charset.cc`）。两条残留限制：其一，解码依赖 iconv（glibc 内置 / libiconv），编译期未找到时退化为 CP1252，仅 cp1252 能正确解码；其二，LID 表中未列出的语言一律按 1252 处理（例如 cp1253 之外的希腊文变体、OEM 代码页）。DBCS 文本按规范总是存为 UTF-16 piece，所以压缩 piece 只需要代码页的单字节映射；若遇到违规的 DBCS 双字节序列，能解出时按两字节消费，否则该字节记为 U+FFFD。
- 语料已加入 `cjk-text`（中/日）与 `cyrillic-text`（俄文）两份真实 `.doc`，但它们的正文按规范存为 UTF-16 piece，验证的是 Unicode 文本链路而不是代码页路径；8-bit 代码页路径仍只有单测（`CharsetTest.*`、`PieceTableTest.DecodesCompressedPiecesInDocumentCodepage`）覆盖。
- `0x0C`（分页符）及其他未列于已处理集合的 <0x20 控制符被静默丢弃（分页边界信息不保留）。
- 非 complex 文档若 piece 覆盖不足 `ccpText`，静默截断、无覆盖数校验。
- U+FFFE/U+FFFF 不在 XML 1.0 Char 产生式内，替换为 U+FFFD 而非报错（防止损坏文件产出 not-well-formed 的 document.xml）。
- 孤立 UTF-16 代理项（未配对的 high/low surrogate）同样替换为 U+FFFD 而不报错。文本抽取路径原先是报错，现在与格式化路径共用唯一的 CLX 读取器 `doc::detail::ReadChars`，因此单字符损坏不再使整篇转换失败。

## M3 当前范围

- 大于约 7 MiB 的 LibreOffice 导出 `.doc` 使用 DIFAT 扩展，最后一个 DIFAT 扇区的 next 字段写 `FREESECT`；[MS-CFB] §2.5 要求 `ENDOFCHAIN`。读取器仅在声明的 DIFAT 扇区数已走完且 FAT 扇区数匹配时兼容这个终止符。合成测试 `CfbReaderTest.ReadsLibreOfficeFreeTerminatedDifat` 与 20/50 MiB 语料覆盖此兼容路径。

- `footnote-endnote.doc` 的 `.doc` 经 LibreOffice 渲染为 1 页，而 doccraft 输出的 OOXML 经同一 LibreOffice 渲染为 2 页：尾注被放到独立的文末区域。已核对尾注正文及编号；把尾注改成脚注的对照实验仍为 1 页，说明这是 LibreOffice 对两种容器的尾注布局解释差异。像素对拍用 `tests/fixtures/footnote-endnote.allow` 精确限定 A=1、B=2 的已知例外，并保留重叠页差异统计（0.164%）。
- 对拍的非零下限受 LibreOffice 自身 `.doc`/`.docx` 导入差异影响。同一 `.doc` 由 LibreOffice 另存 `.docx` 后回读，`cjk-text` 仍为 1470 bad_px（0.068%）、`table-merge-direction` 为 785（0.037%）、`complex-table` 为 38（0.002%）；`table-borders-shading` 则为 0。热力图显示前三者的差异集中在文字笔画或位置，并非推断中的表格边框宽度错误。doccraft 的单元格左内边距补偿使 `table-borders-shading` 降至 0，`table-merge-direction` 降至 282（0.013%）；其余差异不按像素零差目标盲目改写规范属性。

- CHP/PAP/SEP 的常用 sprm、STSH 样式链、LSTF/LVL 列表、首节页眉页脚和基础表格网格已读写。表格单元格合并、复杂边框底纹、浮雕/阴影等效果仍简化；复杂格式需要独立样本验证。
- 多节文档逐节写出属性切换、行号与脚注位置，并为每节生成独立的页眉页脚部件（PlcfHdd 每节 6 个故事，首个故事前有 6 个保留 CP）。两条限制：其一，节内页眉里的图片、域代码与表格只保留文本与段落格式，不含 drawing 部件；其二，故事末段落是守卫标记，会被丢弃，因此「仅有一个空段落」的页眉在输出中不存在。
- 样式表保留段落、字符、表格和编号样式的条目及继承链接；表格样式的条件格式（`sprmTCnf`/`sprmPCnf`/`sprmCCnf`）按 `cnfc` 聚合成 `w:tblStylePr`，但同一条件的 TAPX/PAPX/CHPX 三段只按出现顺序合并，尚未按 2.4.6 的属性优先级重排。
- `sprmPHugePapx` 和 `sprmPTableProps` 会按 Data 流偏移读取嵌套 PrcData；超出流边界则忽略该引用。其复杂表格语义未全部映射。
- 浮动段落框映射 `w:framePr` 的位置/尺寸/间距/锁定、首字下沉（`sprmPDcs`）与纵向文字流（`sprmPFrameTextFlow`，仅宽高非默认时才输出）；`w:drawing` 级锚点与绕排多边形仍缺。
- 浮动表格用 `w:tblpPr` 表达，锚点由 `sprmTPc` 的 `pcVert`/`pcHorz` 映射为 `margin`/`page`/`text`（缺省为 `margin`）；表格文本环绕距离已映射。`sprmTWidthBefore`/`sprmTWidthAfter` 的行级前后缩进仍无对应属性。
- 非等宽分栏（`sprmSFEvenlySpaced` + `sprmSDxaColWidth`/`sprmSDxaColSpacing`）只输出已有宽度的列；仅指定间距的列被丢弃。
- SHD80 系列（`sprmCShd80`/`sprmPShd80`/`sprmTDefTableShd80`）走 16 色调色板，索引 0（自动色）不产生 `w:shd`；现代 SHD 操作数仍以 COLORREF 优先。
- `sprmPIncLvl` 只在 `sprmPIlfo != 0` 时累加 `ilvl`，按规范应增量 `istd` 的段落样式分支未实现。
- `w:framePr` 与表格浮动定位用 `-1` 作「未设置」哨兵，`-1` twip 的真实偏移不可表达。
- 域处理按「顶层特例 + 其余通用」分层：只有顶层 `HYPERLINK` 改写为 `w:hyperlink`、含 `PAGE` 的顶层域改写为 `w:fldSimple`；其余（含全部嵌套域）输出 `w:fldChar` begin/separate/end 与 `w:instrText`。`\x13` 后未见 `\x14` 就遇到 `\x15` 的畸形域按 begin + instrText + end 输出，指令不再被静默丢弃。未保留 `fldLock`/`fldDirty`/`w:dirty`：域状态的脏标志与锁定语义在转换后丢失，Word 重新分页时会重算。
- 域结果里的图片、对象、脚注引用等仍是 M5 载荷；嵌套在超链接结果中的域会把外层链接降级为普通文本。
- 嵌套表格按 `sprmPItap`（绝对深度）建立递归结构：`Cell::blocks` 与正文一样是 Block 序列，深度下降时把内层表格挂到外层单元格上。两条限制：其一，深度信息只来自 `sprmPItap`/`sprmPDtap`，若生产者不写这两个 sprm（缺失时按「在表格内 = 一层」推断），嵌套仍会被摊平；其二，LibreOffice 导出 `.doc` 时会把嵌套表格摊平成段落，所以本级只有合成位流样本（`tests/m3_nested_table_test.cc`），没有真实 Word 样本。
- `sprmPFInnerTableCell` 仍未使用：结构与深度已由 `sprmPItap` 决定，该标记不参与判定。
- 脚注/尾注已按 `PlcffndRef`/`PlcffndTxt`（FIB pair 2/3）与 `PlcfendRef`/`PlcfendTxt`（pair 46/47，注意不是 44/45——那两个是 fcPms/fcFormFldSttbs）解析，正文里的 `0x02` 变成引用 run，`Document::notes` 保存正文并按 `w:footnoteReference`/`w:endnoteReference` + `footnotes.xml`/`endnotes.xml`（含 separator/continuationSeparator）输出。限制：脚注内容只按 CHPX/PAPX 还原文本与段落格式，脚注内的图片、表格、域不处理；脚注编号始终自动编号（不还原自定义标记）；`PlcfendTxt` 末尾可能多一个守卫 CP，实际条数以引用 PLC 为准。
- FIB 里 note PLC 的偏移若指向 table 流之外，现在会让转换直接失败（与其他 PLC 读取一致的策略）。合成测试曾把 FIB 的 rgFcLcb 覆盖成文本字节，就是因为这个策略被暴露出来；真实损坏文档需要 M6 的降级策略来决定是跳过还是报错。
- 批注按 `PlcfandRef`（FIB pair 4）+ `PlcfandTxt`（pair 5）+ `GrpXstAtnOwners`（pair 36）解析：正文的 `0x05` 变成锚点 run，`Document::comments` 保存正文与作者，输出 `comments.xml` 与 `w:commentRangeStart`/`w:commentRangeEnd`/`w:commentReference`。限制：锚点是**零长度区间**（真实 Word 里批注可覆盖一段文字，本实现只在引用点开合）；作者名依赖 ATRDPre10 第 20 字节的作者索引，索引越界时退化为姓名缩写（该偏移是从 LibreOffice 样本反推的，未经 Word 样本验证）；批注时间戳未保留（ATRDPre10 无日期字段，`w:date` 省略）。
- 书签按 `SttbfBkmk`（FIB pair 21）+ `PlcfBkf`（pair 22）+ `PlcfBkl`（pair 23）解析，`w:bookmarkStart`/`w:bookmarkEnd` 输出在正文对应 CP 处。限制：`BKF` 的其他字段（`ITBKLSync` 等）忽略；名字缺失时按 `Bookmark<序号>` 兜底；只处理主文本（`ccpText`）范围的书签，页眉/脚注/批注故事内的书签不输出；未做 `ibkl` 之外的配对校验，`ibkl` 越界的书签整条丢弃。
- 表样式（`sprmTIstd`、条件格式 `sprmTCnf`/`sprmPCnf`/`sprmCCnf`）没有真实 `.doc` 语料：LibreOffice 26.8 导出 `.doc` 时会把表样式摊平成单元格直接格式，`tests/fixtures` 与 corpus 中因此不存在带 TAPX 的表样式。这条链路由 `tests/m3_styles_test.cc` 直接构造 STSH 位流断言（`M3Styles.TableStyleDefaultsAndConditionals`），一旦拿到 Word 生成的样本应替换为 golden 样本。
- 修订标记（`sprmCFRMarkDel`/`sprmCFRMarkIns`）输出为 `w:del`/`w:ins`，但作者固定写 `doccraft`、时间戳省略：作者名需要 `SttbfRMark`（M4），`w:id` 仅保证文件内自增唯一。
- `sprmTTlp` 只把 `grfatl` 的 bit5–10 映射为 `w:tblLook`：首行/末行/首列/末列直接对应，band 位按「置位=启用带状」取反为 `noHBand`/`noVBand`；`itl`（自动格式索引）丢弃。
- `sprmCKcd` 以外的新增字符属性只写单向推导：`sprmCFELayout` 的 `iWarichuBracket` 仅在 `fWarichu` 置位时输出，`sprmCFitText` 的 0 宽度视为未设置。

## M5 当前范围

- 正文内联图片解码后保存到 IR 的字节总量上限为 256MiB（`kMaxTotalImageSize`）；重复引用也累计，恰好达到上限允许，超限以 `total image size exceeds limit` 使整篇转换失败，不降级或丢图。检查发生在当前图片解码后，因此该上限不是进程峰值内存上限；元文件仍受单图 64MiB 限制。

- 正文内联 `U+0001` 图片按 `sprmCPicLocation` 从 Data 流读取 PICF，扫描嵌入 Escher BLIP 并输出 DrawingML 与原格式媒体部件。PNG/JPEG 有 LibreOffice golden 媒体字节对比及像素对拍；WMF/EMF 的压缩和非压缩 BLIP 有合成位流断言，但尚无 LibreOffice 导出的真实元文件 golden。非嵌入或延迟 BLIP、ObjectPool/OLE、浮动图片、裁剪/边框/旋转，以及页眉、脚注等其他故事中的图片留待后续；遇到正文内联图片的缺失或不支持载荷会显式返回错误。
- 字符单位缩进（`sprmPDxc*`/`sprmPDyl*`）只写 `w:ind/@*Chars` 与 `w:spacing/@*Lines`，不重复写 twips 绝对值；阅读器若忽略 *Chars 会退回样式默认缩进。
- `sprmSPgbProp` 的枚举按 0=页面边缘/1=文字、0=全部页/1=首页/2=非首页解释；`reserved` 字节非 0 时整条 sprm 视为损坏。
- `sprmSFProtected` 只在 `sprmSFProtected` 出现时输出 `w:formProt`（值为「未被保护」的反相），文档级 `DOP.fProtEnabled` 尚未解析，未出现该 sprm 的节不写保护节点。
- M3 仅保留越界不崩溃底线。针对 sprm 攻击面的专项分析、定向 fuzz 和防御补强移至 M6。
- `src/base/binary.h` 的 `U16`/`U32`/`U64`/`S16` 统一了字节序解码，但不做边界检查：越界安全仍由调用方的前置 `subspan`/范围判断保证。带游标、越界即 `std::expected` 失败的读取器需要重写全部调用点，随 M6 的 fuzz 加固一起做。
