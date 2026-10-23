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
- 8-bit（compressed）piece 固定按 cp1252 解码，不读 FibBase/文档 codepage——CJK codepage（cp932/936/949/950）文档的压缩 piece 会乱码。五份语料正文全 ASCII，此路径无语料级覆盖。
- `0x0C`（分页符）及其他未列于已处理集合的 <0x20 控制符被静默丢弃（分页边界信息不保留）。
- 非 complex 文档若 piece 覆盖不足 `ccpText`，静默截断、无覆盖数校验。
- U+FFFE/U+FFFF 不在 XML 1.0 Char 产生式内，替换为 U+FFFD 而非报错（防止损坏文件产出 not-well-formed 的 document.xml）。
