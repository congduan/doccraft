# doccraft SPRM 全量覆盖清单

> 目标：[MS-DOC] 全部 sprm 逐项验收。状态：✅ 实现且测试断言 / ◐ 部分实现（网格级或骨架）/ ☐ 未实现 / N/A 不适用（废弃/内部）。
> 计数口径：✅ = 有对应合成断言或 fixture 测试。每完成一项打勾并在括号内补测试名。
> 本文件是 M3 验收的覆盖事实源，随实现增长更新。
> 核账（M3 第四批）：原文宣称 240 项，但只列出 151 行；本批新增 1 行后为 152 行，其中 SEP 已列 33 行，超过原小节标题的 27 行。原统计 147 个 ✅ 无法由逐项状态复算。下表把未列出的 88 项单列为待盘点，不把它们算作已覆盖。

## 1. 字符 sprm（CHPX）— 已列 50 项

### 1.1 字体与字形
- [x] sprmCRgFtc0-2 (0x4A4F) 字体族索引 → w:rFonts（FormatTest.ReadsDirectCharacterFormatting）
- [x] sprmCSize (0x4A43) 字号半点 → w:sz（FormatTest.ReadsDirectCharacterFormatting）
- [x] sprmCHpsBi (0x4A61) 复杂脚本字号 → w:szCs ✅（M3Batch3.ComplexScriptKerningAndPosition）
- [x] sprmCItalic (0x0836) 斜体 → w:i（FormatTest.ReadsDirectCharacterFormatting）
- [x] sprmCBold (0x0835) 加粗 → w:b（FormatTest.DirectFormattingCanTurnBoldOff）
- [x] sprmCFItalicBi (0x085D) → w:iCs ✅（M3Batch3.ComplexScriptKerningAndPosition）
- [x] sprmCFBoldBi (0x085C) → w:bCs ✅（M3Batch3.ComplexScriptKerningAndPosition）

### 1.2 下划线与删除线
- [x] sprmCKul (0x2A3E) 下划线线型 17 值 → w:u（M3Batch2.UnderlineStyles / M3Batch3 扩展）
- [ ] sprmCFSpecVanish (0x0818) ☐
- [x] sprmCFCaps (0x083B) 全大写 → w:caps ✅
- [x] sprmCFSmallCaps (0x083A) 小型大写 → w:smallCaps ✅
- [x] sprmCFStrike (0x0837) 删除线 → w:strike ✅
- [x] sprmCFDStrike (0x2A53) 双删除线 → w:dstrike ✅

### 1.3 特殊效果
- [ ] sprmCFOle2 (0x080A) ☐
- [ ] sprmCFObjOffset 未实现 ☐
- [x] sprmCFEmboss (0x0858) 浮雕 → w:emboss ✅
- [x] sprmCFImprint (0x0854) 凹印 → w:imprint ✅
- [x] sprmCFShadow (0x0839) 阴影 → w:shadow ✅
- [x] sprmCFOutline (0x0838) → w:outline ✅
- [ ] sprmCFData (0x0806) ☐
- [ ] sprmCFLanguage (0x083F) ☐
- [x] sprmCSpacing (0x8840) 字距 → w:spacing ✅
- [ ] sprmCCondTable 未实现 ☐
- [x] sprmCHpsKern (0x484B) 字偶距阈值 → w:kern ✅（M3Batch3.ComplexScriptKerningAndPosition）
- [ ] sprmCKern (0x8455) 旧清单操作码；[MS-DOC] 字偶距阈值为 0x484B，待核实 N/A
- [x] sprmCCharScale (0x4852) 缩放 → w:w ✅
- [ ] sprmCFtcDefault ☐
- [x] sprmCHpsPos (0x4845) 升降 → w:position ✅（M3Batch3.ComplexScriptKerningAndPosition）
- [x] sprmCSymbol (0x6A09) SYM 字符 → w:sym ✅（M3Batch3.CharacterSymbolEmitsSymElement）

### 1.4 颜色与高亮
- [x] sprmCIco (0x2A42) 调色板色 → w:color ✅
- [x] sprmCCv (0x6870) RGB 色 → w:color ✅
- [x] sprmCHighlight (0x2A0C) 高亮 → w:highlight ✅
- [x] sprmCShd (0xCA71) 字符底纹 → w:shd ✅
- [x] sprmCBrc (0xCA72/0x6865) 字符边框 → w:bdr ✅
- [ ] sprmCCvHighlight ☐

### 1.5 上下标与隐藏
- [ ] sprmCDispFld RVC ☐
- [ ] sprmCIbstRField ☐
- [ ] sprmCFrvrtDisp ☐
- [ ] sprmCFSelectFld ☐
- [ ] sprmCObjOffset ☐
- [x] sprmCIVertAlign (0x2A48) 上下标 → w:vertAlign ✅
- [x] sprmCFHidden (0x083C) 隐藏 → w:vanish ✅
- [ ] sprmCFRMarkDel ☐
- [ ] sprmCFRMark ☐
- [ ] sprmCFrc (0x0863) ☐

### 1.6 修订与域（M4 域域相关，本里程碑不强制）
- [ ] sprmCFSpecsym ☐
- [ ] sprmCRmf (0x4875) 保留格式 ☐
- [ ] sprmCFFldVanish ☐
- [ ] sprmCPicLocation (0x6A03) 图片位置 ☐（M5 图片）
- [ ] sprmCPropRMark (0xCA89；旧版 0xCA57) ☐

## 2. 段落 sprm（PAPX）— 已列 34 项

### 2.1 缩进对齐
- [x] sprmPDxaLeft (0x840F/0x845E) → w:ind left ✅
- [x] sprmPDxaRight (0x840E/0x845D) → w:ind right ✅
- [x] sprmPDxaLeft1 (0x8460/0x845F) 首行/悬挂 → w:ind firstLine/hanging ✅
- [x] sprmPJc (0x2403/0x2461) 对齐 → w:jc ✅
- [ ] sprmPDxaAbs ☐
- [ ] sprmPDxaRel ☐
- [ ] sprmPDyaAbs ☐
- [ ] sprmPDyaRel ☐

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
- [ ] sprmPShdTable ☐

### 2.4 列表编号
- [x] sprmPIlvl (0x260A) 列表级别 ✅
- [x] sprmPIlfo (0x460B) 列表 ID ✅
- [ ] sprmPFInTable (0x2416) ☐ 部分实现（表格检测用）
- [ ] sprmPFTtp (0x2417) 行尾标记 ☐ 部分实现
- [x] sprmPWr (0x2423) 环绕 → w:framePr wrap ✅（M3Batch2.FloatingParagraphWrapAndAnchors）
- [x] sprmPPc (0x261B) 定位锚点 → w:framePr hAnchor/vAnchor ✅（M3Batch2.FloatingParagraphWrapAndAnchors）

### 2.5 段落属性杂项
- [x] sprmPKeep (0x2405) keepLines ✅
- [x] sprmPKeepNext (0x2406) keepNext ✅
- [x] sprmPPageBreakBefore (0x2407) ✅
- [x] sprmPOutLvl (0x2640) 大纲级别 ✅
- [x] sprmPChgTabsPapx (0xC615/0xC60D) 制表位 → w:tabs ✅（含 leader）
- [ ] sprmPFNoAllowOverlap ☐
- [ ] sprmPWatermark (0x6422) 水印 ☐
- [ ] sprmPRsid ☐
- [ ] sprmPFrameId ☐

## 3. 节 sprm（SEPX）— 已列 33 项

- [x] sprmSBkc (0x3009) 分节符类型 ✅
- [x] sprmSFTitlePage (0x300A) 首页不同 ✅
- [ ] sprmSDmBinFirst ☐
- [ ] sprmSDmBinOther ☐
- [ ] sprmSDmPaperType (0x5007) ☐
- [ ] sprmSFBiDi ☐
- [x] sprmSFacePg (0x301D) 方向 → w:pgSz orient ✅
- [x] sprmSXaPage (0xB01F) 页宽 ✅
- [x] sprmSYaPage (0xB020) 页高 ✅
- [x] sprmSDxaLeft (0xB021) 左边距 ✅
- [x] sprmSDxaRight (0xB022) 右边距 ✅
- [x] sprmSDyaTop (0x9023) 上边距 ✅
- [x] sprmSDyaBottom (0x9024) 下边距 ✅
- [x] sprmSDzaGutter (0xB025) 装订线 ✅
- [ ] sprmSDmBinFirst ☐
- [ ] sprmSFAutoPgn ☐
- [ ] sprmSFEvenlyDvd ☐
- [ ] sprmSFTextFlow (0x3615) ☐ 部分实现（表格 autofit 复用）
- [x] sprmSCcolumns (0x500B) 分栏数 → w:cols ✅（M3Batch3.SectionColumnsPageNumberAndGrid）
- [x] sprmSDxaColumns (0x900C) 栏间距 → w:cols ✅（M3Batch3.SectionColumnsPageNumberAndGrid）
- [ ] sprmSFAutoPgnDxa ☐
- [ ] sprmSFEvenlySpaced (0x3005) 等宽栏 ◐（已解析；非等宽栏宽度尚未输出）
- [ ] sprmSFBiDi ☐
- [x] sprmSFLandscape 已含 (0x301D) ✅
- [ ] sprmSBOption 段落栏平衡 ☐
- [x] sprmSDyaLinePitch (0x9031) 网格行高 → w:docGrid ✅（M3Batch3.SectionColumnsPageNumberAndGrid）
- [x] sprmSClm (0x5032) 网格模式 → w:docGrid ✅（M3Batch3.SectionColumnsPageNumberAndGrid）
- [x] sprmSTextFlow (0x5033) → w:textDirection ✅（M3Batch3.SectionColumnsPageNumberAndGrid）

### 3.1 页眉页脚（docx 侧已实现，sprm 映射验证于 header-footer.doc）
- [x] sprmSPgnStart97/Start (0x501C/0x7044) 页码起始 → w:pgNumType ✅（M3Batch3.SectionColumnsPageNumberAndGrid）
- [x] sprmSFPgnRestart (0x3011) → w:pgNumType ✅（M3Batch3.SectionColumnsPageNumberAndGrid）
- [ ] sprmSFEndnote ☐
- [ ] sprmPgnCharType ☐
- [ ] sprmPgnLvl ☐

## 4. 表格 sprm（TAPX）— 已列 31 项

### 4.1 表格定义
- [x] sprmTDefTable (0xD608) 表格定义主结构 ✅（增强后）
- [ ] sprmTDefTable10 (0xD606) 旧版 ◐（仅网格，BRC10 未完整）
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
- [x] sprmTDxaGapHalf (0x9602) 表格左缘偏移 → w:tblInd ✅（M3Batch3.TableSpacingAndGapOffset）
- [x] sprmTCellSpacingDefault (0xD633) 单元格间距 → w:tblCellSpacing ✅（M3Batch3.TableSpacingAndGapOffset）
- [ ] sprmTCellFtlr 未实现 ☐

## 5. 样式/编号 sprm（STSH/LSTF 内 PRL）
- [x] sprmPStyle (0x4600) 样式 ID ✅
- [x] sprmCStyleChp 已含在继承链 ✅
- [ ] sprmTStyle 条件格式 ☐
- [ ] sprmPIprop 编号继承 ☐

## 统计

| 类别 | 总数 | ✅ | ◐ | ☐ | 覆盖率 |
|---|---:|---:|---:|---:|---:|
| CHP 字符（已列） | 50 | 28 | 0 | 22 | 56% |
| PAP 段落（已列） | 34 | 23 | 2 | 9 | 68% |
| SEP 节（已列） | 33 | 18 | 2 | 13 | 55% |
| TAPX 表格（已列） | 31 | 29 | 1 | 1 | 94% |
| 样式/编号（已列） | 4 | 2 | 0 | 2 | 50% |
| 未列出、待逐项盘点 | 88 | 0 | 0 | 88 | 0% |
| **合计（原目标口径）** | **240** | **100** | **5** | **135** | **42%** |

> 注：240 是原清单宣称的目标，尚未逐项列齐，不能视为已核实的规范总数。每个已列类别的小节数字按实际行数计算；未列出的 88 项不能计为 ✅。
