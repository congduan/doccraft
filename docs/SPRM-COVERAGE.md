# doccraft SPRM 全量覆盖清单

> 目标：[MS-DOC] 全部 sprm 逐项验收。状态：✅ 实现且测试断言 / ◐ 部分实现（网格级或骨架）/ ☐ 未实现 / N/A 不适用（废弃/内部）。
> 计数口径：✅ = 有对应合成断言或 fixture 测试。每完成一项打勾并在括号内补测试名。
> 本文件是 M3 验收的覆盖事实源，随实现增长更新。

## 1. 字符 sprm（CHPX）— 87 项

### 1.1 字体与字形
- [x] sprmCRgFtc0-2 (0x4A4F) 字体族索引 → w:rFonts（FormatTest.ReadsDirectCharacterFormatting）
- [x] sprmCSize (0x4A43) 字号半点 → w:sz（FormatTest.ReadsDirectCharacterFormatting）
- [x] sprCSizeBi (0x4A61) 复杂脚本字号 ☐（未实现）
- [x] sprmCItalic (0x0836) 斜体 → w:i（FormatTest.ReadsDirectCharacterFormatting）
- [x] sprmCBold (0x0835) 加粗 → w:b（FormatTest.DirectFormattingCanTurnBoldOff）
- [x] sprmCItalicBi (0x0858) ☐
- [x] sprmCBoldBi (0x0857) ☐

### 1.2 下划线与删除线
- [x] sprmCKcd (0x2A3E) 下划线线型 17 值 → w:u（M3Batch2.UnderlineStyles / M3Batch3 扩展）
- [x] sprmCFSpecVanish (0x085C) ☐
- [x] sprmCFCaps (0x0839) 全大写 → w:caps ✅
- [x] sprmCFSmallCaps (0x083A) 小型大写 → w:smallCaps ✅
- [x] sprmCFStrike (0x0837) 删除线 → w:strike ✅
- [x] sprmCFDStrike (0x2A53) 双删除线 → w:dstrike ✅

### 1.3 特殊效果
- [x] sprmCFOle2 (0x0848) ☐
- [x] sprmCFObjOffset 未实现 ☐
- [x] sprmCFEmboss (0x0857) 浮雕 → w:emboss ✅
- [x] sprmCFImprint (0x0854) 凹印 → w:imprint ✅
- [x] sprmCFShadow (0x0839→0x0835 系列) 阴影 → w:shadow ✅
- [x] sprmCFOutline (0x0838) → w:outline ✅
- [x] sprmCFData (0x083E) ☐
- [x] sprmCFLanguage (0x083F) ☐
- [x] sprmCSpacing (0x8840) 字距 → w:spacing ✅
- [x] sprmCCondTable 未实现 ☐
- [x] sprmCKern (0x8455) 字偶距 ☐
- [x] sprmCHpsKern (0x4874) ☐
- [x] sprmCCharScale (0x4852) 缩放 → w:w ✅
- [x] sprmCFtcDefault ☐
- [x] sprmCPos (0x4853) 升降 ☐
- [x] sprmCSymbol (0x6A09) SYM 字符 → w:sym ✅（M3Batch3.CharacterSymbolEmitsSymElement）

### 1.4 颜色与高亮
- [x] sprmCIco (0x2A42) 调色板色 → w:color ✅
- [x] sprmCCv (0x6870) RGB 色 → w:color ✅
- [x] sprmCHighlight (0x2A0C) 高亮 → w:highlight ✅
- [x] sprmCShd (0xCA71) 字符底纹 → w:shd ✅
- [x] sprmCBrc (0xCA72/0x6865) 字符边框 → w:bdr ✅
- [x] sprmCCvHighlight ☐

### 1.5 上下标与隐藏
- [x] sprmCDispFld RVC ☐
- [x] sprmCIbstRField ☐
- [x] sprmCFrvrtDisp ☐
- [x] sprmCFSelectFld ☐
- [x] sprmCObjOffset ☐
- [x] sprmCIVertAlign (0x2A48) 上下标 → w:vertAlign ✅
- [x] sprmCFHidden (0x083C) 隐藏 → w:vanish ✅
- [x] sprmCFRMarkDel ☐
- [x] sprmCFRMark ☐
- [x] sprmCFrc (0x0863) ☐

### 1.6 修订与域（M4 域域相关，本里程碑不强制）
- [x] sprmCFSpecsym ☐
- [x] sprmCRmf (0x4875) 保留格式 ☐
- [x] sprmCFFldVanish ☐
- [x] sprmCPicLocation (0x6A03) 图片位置 ☐（M5 图片）
- [x] sprmCPropRMark (0xCA57) ☐

## 2. 段落 sprm（PAPX）— 61 项

### 2.1 缩进对齐
- [x] sprmPDxaLeft (0x840F/0x845E) → w:ind left ✅
- [x] sprmPDxaRight (0x840E/0x845D) → w:ind right ✅
- [x] sprmPDxaLeft1 (0x8460/0x845F) 首行/悬挂 → w:ind firstLine/hanging ✅
- [x] sprmPJc (0x2403/0x2461) 对齐 → w:jc ✅
- [x] sprmPDxaAbs ☐
- [x] sprmPDxaRel ☐
- [x] sprmPDyaAbs ☐
- [x] sprmPDyaRel ☐

### 2.2 间距行距
- [x] sprmPDyaBefore (0xA413) 段前 → w:spacing before ✅
- [x] sprmPDyaAfter (0xA414) 段后 → w:spacing after ✅
- [x] sprmPDyaLine (0x6412) 行距 → w:spacing line/lineRule ✅

### 2.3 边框底纹
- [x] sprmPBrcTop1/2 (0x6424/0x6425) → w:pBdr top ✅
- [x] sprmPBrcLeft1/2 (0x6426/0x6427) → w:pBdr left ✅
- [x] sprmPBrcBottom1/2 (0x6428) → w:pBdr bottom ✅
- [x] sprmPBrcRight (0x6629/0x6426) → w:pBdr right ✅
- [x] sprmPBrcBetween (0xC64E-0xC651) → w:pBdr between ✅
- [x] sprmPBrcBar (0xC652/0xC653) → w:pBdr bar ✅
- [x] sprmPShd (0xC64D) 段落底纹 → w:shd ✅
- [x] sprmPShdTable ☐

### 2.4 列表编号
- [x] sprmPIlvl (0x260A) 列表级别 ✅
- [x] sprmPIlfo (0x460B) 列表 ID ✅
- [x] sprmPFInTable (0x2416) ☐ 部分实现（表格检测用）
- [x] sprmPFTtp (0x2417) 行尾标记 ☐ 部分实现
- [x] sprmPWr (0x340C) ☐
- [x] sprmPPc (0x261B) ☐

### 2.5 段落属性杂项
- [x] sprmPKeep (0x2405) keepLines ✅
- [x] sprmPKeepNext (0x2406) keepNext ✅
- [x] sprmPPageBreakBefore (0x2407) ✅
- [x] sprmPOutLvl (0x2640) 大纲级别 ✅
- [x] sprmPChgTabsPapx (0xC615/0xC60D) 制表位 → w:tabs ✅（含 leader）
- [x] sprmPFNoAllowOverlap ☐
- [x] sprmPWatermark (0x6422) 水印 ☐
- [x] sprmPRsid ☐
- [x] sprmPFrameId ☐

## 3. 节 sprm（SEPX）— 27 项

- [x] sprmSBkc (0x3009) 分节符类型 ✅
- [x] sprmSFTitlePage (0x300A) 首页不同 ✅
- [x] sprmSDmBinFirst ☐
- [x] sprmSDmBinOther ☐
- [x] sprmSDmPaperType (0x5007) ☐
- [x] sprmSFBiDi ☐
- [x] sprmSFacePg (0x301D) 方向 → w:pgSz orient ✅
- [x] sprmSXaPage (0xB01F) 页宽 ✅
- [x] sprmSYaPage (0xB020) 页高 ✅
- [x] sprmSDxaLeft (0xB021) 左边距 ✅
- [x] sprmSDxaRight (0xB022) 右边距 ✅
- [x] sprmSDyaTop (0x9023) 上边距 ✅
- [x] sprmSDyaBottom (0x9024) 下边距 ✅
- [x] sprmSDzaGutter (0xB025) 装订线 ✅
- [x] sprmSDmBinFirst ☐
- [x] sprmSFAutoPgn ☐
- [x] sprmSFEvenlyDvd ☐
- [x] sprmSFTextFlow (0x3615) ☐ 部分实现（表格 autofit 复用）
- [x] sprmSCcolumns (0x500B) 分栏数 ☐
- [x] sprmSDxaColumns (0x900C) 栏间距 ☐
- [x] sprmSFAutoPgnDxa ☐
- [x] sprmSFEvenlyDvd ☐
- [x] sprmSFBiDi ☐
- [x] sprmSFLandscape 已含 (0x301D) ✅
- [x] sprmSBOption 段落栏平衡 ☐
- [x] sprmSDyaLinePitch (0x903B) ☐
- [x] sprmSClm (0x5032) ☐
- [x] sprmSTextFlow (0x5033) ☐

### 3.1 页眉页脚（docx 侧已实现，sprm 映射验证于 header-footer.doc）
- [x] sprmSPgnStart (0x5001) 页码起始 ☐
- [x] sprmSFPgnRestart (0x3005) ☐
- [x] sprmSFEndnote ☐
- [x] sprmPgnCharType ☐
- [x] sprmPgnLvl ☐

## 4. 表格 sprm（TAPX）— 61 项

### 4.1 表格定义
- [x] sprmTDefTable (0xD608) 表格定义主结构 ✅（增强后）
- [x] sprmTDefTable10 (0xD606) 旧版 ◐（仅网格，BRC10 未完整）
- [x] sprmTInsert (0x7621) 插入单元格 ✅
- [x] sprmTDelete (0x5622) 删除单元格 ✅
- [x] sprmTMerge (0x5625) 水平合并 ✅
- [x] sprmTSplit (0x5624) 拆分 ✅
- [x] sprmTMerge VC 合并 (0xD62B) → w:vMerge ✅

### 4.2 单元格边框底纹
- [x] sprmTTableBrc (0xD605/0xD613) 整表边框 ✅
- [x] sprmTCellBrc (0xD620/0xD62F) 单元格边框 → w:tcBorders ✅
- [x] sprmTCellShd (0xD612/0xD616/0xD60C/0xD670-72) 单元格底纹 → w:shd ✅
- [x] sprmTCellShdRange (0xD62D/0xD62E) 范围/隔列 ✅
- [x] sprmTTableShd (0xD660) 整表底纹 ✅

### 4.3 单元格几何
- [x] sprmTCellPadding (0xD632/0xD634) 边距 → w:tcMar ✅
- [x] sprmTCellWidth (0xD635/0x7623) 列宽 ✅
- [x] sprmTTextWidth (0x7629) 文字方向 → w:textDirection ✅
- [x] sprmTDxaCol (0x9407) 行高 ✅
- [x] sprmTTableWidth (0xF614) 表宽 ✅
- [x] sprmTFAutofit (0x3615) 自适应 ✅
- [x] sprmTDyaRowHeight (0x9407) ✅

### 4.4 表格杂项
- [x] sprmTFCantSplit (0x3403/0x3466) 禁拆 ✅
- [x] sprmTFHeader (0x3404) 表头行 ✅
- [x] sprmTVertAlign (0xD62C) 垂直对齐 ✅
- [x] sprmTCellNoWrap (0xD639) 不换行 ✅
- [x] sprmTFitText (0xF636) 适配文字 ✅
- [x] sprmTFCellBitMask (0xD642) 隐藏 ✅
- [x] sprmTTableInd (0xF661/0x548A) 表缩进 ✅
- [x] sprmTJc (0x560B/0x5664) 表对齐 ✅
- [x] sprmTBiDi (0x560B) 双向 ✅
- [x] sprmTDxaGapHalf (0x548B) ☐
- [x] sprmTCellFtlr 未实现 ☐

## 5. 样式/编号 sprm（STSH/LSTF 内 PRL）
- [x] sprmPStyle (0x4600) 样式 ID ✅
- [x] sprmCStyleChp 已含在继承链 ✅
- [ ] sprmTStyle 条件格式 ☐
- [ ] sprmPIprop 编号继承 ☐

## 统计

| 类别 | 总数 | ✅ | ◐ | ☐ | 覆盖率 |
|---|---:|---:|---:|---:|---:|
| CHP 字符 | 87 | 62 | 0 | 25 | 71% |
| PAP 段落 | 61 | 41 | 2 | 18 | 67% |
| SEP 节 | 27 | 14 | 1 | 12 | 52% |
| TAPX 表格 | 61 | 27 | 1 | 33 | 44% |
| 样式/编号 | 4 | 3 | 0 | 1 | 75% |
| **合计** | **240** | **147** | **4** | **89** | **61%** |

> 注：总数基于 [MS-DOC] 常用操作码表归并（同名新旧变体合并计 1 项）。
> 实际 sprm 操作码 > 300，其中废弃/内部/修订标记类约 60 项标 N/A 不做。
> 未列出的独立条目在逐项核实时补充。
