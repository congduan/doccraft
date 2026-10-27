# doccraft SPRM 全量覆盖清单

> 来源：[MS-DOC] v20260217 §2.6.1–2.6.4 四张 sprm opcode 表（[Microsoft PDF](https://officeprotocoldocs-f5hpbjgea6b8gneq.b02.azurefd.net/files/MS-DOC/%5bMS-DOC%5d.pdf)）。按每个规范表行独立计数；同一属性的旧版和新版 opcode 分开记账。
> ✅ = 解析、IR 与 OOXML 输出齐备且有合成样本断言；◐ = 部分实现；☐ = 待实现；N/A = 不进入 OOXML 输出，附理由，分为四类：规范允许忽略、编辑期/增量保存的内部状态、无 docx 对应属性的排版提示、由 M4/M5 里程碑承载的载荷。
> 规范 §2.6 是完整 sprm 列表；§2.9 是数据类型定义。旧账的「240 = 已列 152 + 未列 88」未经核实，现按规范重建。

> M4 fast-saved CLX：`PieceTableTest.ReadsFastSavedPiecesAndPrc` 与 `PieceTableTest.AppliesFastSavedPrm0Modifier` 验证正文；`M4FastSaved.HeaderStoryAppliesPrcRunFormatting` 验证非正文 Prc；`M4FastSaved.PiecePItapPlacesParagraphInsideTable` 验证 piece 上的表格深度。这改变修饰来源，不改变 §2.6 各 opcode 的既有覆盖状态。真实版式 golden 未过，§6.1 仍记 ◐。
> M4 表格回归：`M4FastSaved.ParagraphMarkPapKeepsFastSavedCellsTogether` 验证 fast-saved 的正文 FC 与段落结束符 FC 分离时，`sprmPFInTable`/`sprmPFTtp` 由结束符 PAPX 控制整段；真实 `Bug33519.doc` 的表格从 60 个碎片收敛为三张完整表，旧、新 DOCX 的 LO 文本导出均为 691/691 词。Word 6/95 自 M12 起把旧版 1 字节 opcode 归一化到本表的现代 opcode（见文末「Word 6/95 旧版 opcode」），现代路径状态不变。
> M4 第二批文本框：`M4Textbox.LibreOfficeFloatingTextboxKeepsStoryAtAnchor` 与 `M4Textbox.SyntheticFlowBreaksStayInStoryOrder` 覆盖 FTXBXS/Tbkd 与锚点连接；它们是 PLC/故事结构而非 sprm，不改变本表 opcode 状态。§6.6 文本框保持 ◐，限制见 KNOWN-SIMPLIFICATIONS。
> M11 文本框字段 PLC：`M11TextboxFields.FibExposesBodyAndHeaderTextboxFieldPlcs`、`M11TextboxFields.FieldFlagsReachBodyAndHeaderTextboxXml`、`M11TextboxFields.InvalidPlcWarnsAndKeepsHierarchy` 覆盖 FIB pair 57/59、两类文本框域的锁/脏 XML 标志及损坏 PLC 降级。它们不是 sprm；§6.1 因缺真实 Word 文本框域 golden 保持 ◐。
> M11 页眉 PLC 双重损坏：`M11HeaderFields.DoublyCorruptPlcsKeepBothWarnings` 断言故事 PLC 与域 PLC 同时损坏时各发一条警告；不改变 sprm 状态。
> M4 第二批损坏降级：`degraded_fkp_warning`、`degraded_sprm_warning`、`degraded_header_warning` 验证局部跳过、stderr 警告及正文保留；它们不改变正常输入下的各 opcode 覆盖状态。

> 代码中的 opcode 不再是裸字面量：统一引用 `src/doc/format/sprm_codes.h` 的
> `kSprm<Name>` 常量；新增 sprm 时先在本表登记，再在同一文件补常量。

## 1. 字符 CHP（§2.6.1，84 项）

| 状态 | opcode | sprm | 语义（规范摘要） | 归类 |
|---|---|---|---|---|
| ✅ | `0x0800` | sprmCFRMarkDel | A ToggleOperand that specifies whether the text is formatted as deleted revision mark text, which is text that was deleted while revision marking was on. | 字符 CHP §2.6.1；M3Batch8.RevisionMarksWrapRuns |
| ✅ | `0x0801` | sprmCFRMarkIns | A ToggleOperand that specifies whether the text is formatted as inserted revision mark text, which is text that was inserted while revision marking was on. | 字符 CHP §2.6.1；M3Batch8.RevisionMarksWrapRuns |
| ✅ | `0x0802` | sprmCFFldVanish | A ToggleOperand that specifies whether the field text is hidden. | 字符 CHP §2.6.1；M3Batch7.HiddenTextSources |
| ✅ | `0x6A03` | sprmCPicLocation | A signed 32-bit integer that specifies either the position in the Data Stream of a picture or binary data or the name of an OLE object storage. | 字符 CHP §2.6.1；正文内联图片 Data 偏移、PICF/BLIP 到 `w:drawing`；M5InlineImages.LibreOfficeGoldenKeepsMediaBytes、M5InlineImages.SameChpxTextDoesNotAbsorbPicture、M5InlineImages.AggregateImageLimitAcceptsBoundaryRejectsOverflow；OLE 字段分隔符 ObjectPool id 到嵌入部件由 M4Ole.RealObjectPoolSubstorageIsIndependentCfbPart 验证 |
| N/A | `0x4804` | sprmCIbstRMark | A signed 16-bit integer that specifies a zero-based index into SttbfRMark.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| N/A | `0x6805` | sprmCDttmRMark | A DTTM that specifies the date and time at which the text was inserted.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| N/A | `0x0806` | sprmCFData | A Bool8 that specifies whether the picture character in the text represents binary data. | 字符 CHP §2.6.1；图片二进制标记；二进制数据不作为内联图片读取，M5InlineImages.BinaryDataMarkerDoesNotReadPicture；载荷留待后续 |
| N/A | `0x4807` | sprmCIdslRMark | An unsigned 16-bit integer that specifies the reason value of the inserted or modified revision mark text.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| ✅ | `0x6A09` | sprmCSymbol | A CSymbolOperand structure that designates the character as a symbol and specifies the font and character code for the symbol. | 字符 CHP §2.6.1 |
| ✅ | `0x080A` | sprmCFOle2 | A Bool8 value that specifies whether the character is a placeholder for an OLE object. | 字符 CHP §2.6.1；字段分隔符识别 ObjectPool 对象，M4Ole.RealObjectPoolSubstorageIsIndependentCfbPart |
| ✅ | `0x2A0C` | sprmCHighlight | An Ico value that specifies the highlighting color of the text. | 字符 CHP §2.6.1 |
| ✅ | `0x0811` | sprmCFWebHidden | A ToggleOperand value that specifies whether the text is hidden in Web Layout view of the document.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| N/A | `0x6815` | sprmCRsidProp | An integer value that specifies a revision save ID, as specified in [ECMA-376] Part 1, Section 17.15.1.70 rsid (Single Session Revision Save ID), associated with character formatting.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| N/A | `0x6816` | sprmCRsidText | An integer that specifies a revision save ID, as specified in [ECMA-376] Part 1, Section 17.15.1.70 rsid (Single Session Revision Save ID), associated with insertion of text.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| N/A | `0x6817` | sprmCRsidRMDel | An integer that specifies a revision save ID, as specified in [ECMA-376] Part 1, Section 17.15.1.70 rsid (Single Session Revision Save ID), associated with tracked deletion of text.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| ✅ | `0x0818` | sprmCFSpecVanish | A ToggleOperand that specifies that this line break does not indicate a line break but serves as a style separator.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| N/A | `0xC81A` | sprmCFMathPr | A MathPrOperand that specifies the justification of equations in the paragraph. | 字符 CHP §2.6.1；规范允许实现忽略（§2.6.1 脚注 145）；docx 公式由 OMML 重新排版 |
| ✅ | `0x4A30` | sprmCIstd | An unsigned integer that specifies the istd of a character style to apply. | 字符 CHP §2.6.1；M3Batch6.ExistingParagraphAndCharacterPaths |
| N/A | `0xCA31` | sprmCIstdPermute | Character style permutation mapping; changes the applied character style when the current style is in the map. | 字符 CHP §2.6.1；编辑期样式置换表；样式表与 istd 同源读取，无需跨版本重映射 |
| ✅ | `0x2A33` | sprmCPlain | Presence resets character properties to the underlying paragraph style; only its operand is ignored. | 字符 CHP §2.6.1；存在即重置直接字符格式；M3Batch7.PlainResetsDirectCharacterFormatting |
| ✅ | `0x2A34` | sprmCKcd | MUST be one of the following values. 0x00 - No emphasis 0x01 - Solid circle 0x02 - Comma above 0x03 - Circle above 0x04 - Solid circle below The operands map to Unicode characters as shown… | 字符 CHP §2.6.1；w:em 强调符号；M3Batch7.CharacterEmphasisMark |
| ✅ | `0x0835` | sprmCFBold | A ToggleOperand value that specifies whether the text is bold. | 字符 CHP §2.6.1 |
| ✅ | `0x0836` | sprmCFItalic | A ToggleOperand value that specifies whether the text is italicized. | 字符 CHP §2.6.1 |
| ✅ | `0x0837` | sprmCFStrike | A ToggleOperand value that specifies whether the text is formatted with strikethrough. | 字符 CHP §2.6.1 |
| ✅ | `0x0838` | sprmCFOutline | A ToggleOperand value that specifies whether only the outline contour of the characters in the text is rendered, with the inside of each character left empty. | 字符 CHP §2.6.1 |
| ✅ | `0x0839` | sprmCFShadow | A ToggleOperand value that specifies whether the text is formatted with a shadow. | 字符 CHP §2.6.1 |
| ✅ | `0x083A` | sprmCFSmallCaps | A ToggleOperand value that specifies whether the text characters are displayed as their capital letter equivalents, in a font size that is smaller than the actual font size that is specified… | 字符 CHP §2.6.1 |
| ✅ | `0x083B` | sprmCFCaps | A ToggleOperand value that specifies whether the text characters are displayed as their capital letter equivalents. | 字符 CHP §2.6.1 |
| ✅ | `0x083C` | sprmCFVanish | A ToggleOperand value that specifies whether the text is formatted as hidden. | 字符 CHP §2.6.1 |
| ✅ | `0x2A3E` | sprmCKul | A Kul value that specifies the underlining style of the text. | 字符 CHP §2.6.1 |
| ✅ | `0x8840` | sprmCDxaSpace | An XAS value that specifies the extra space, in twips, between a character and the one to its right. | 字符 CHP §2.6.1 |
| ✅ | `0x2A42` | sprmCIco | An Ico value that specifies the color of the text. | 字符 CHP §2.6.1 |
| ✅ | `0x4A43` | sprmCHps | An unsigned 2-byte integer that specifies the size of the text, except for text that meets the qualifications for sprmCHpsBi. | 字符 CHP §2.6.1 |
| ✅ | `0x4845` | sprmCHpsPos | A signed integer value that specifies the vertical position, in half-points, of text relative to the normal position. | 字符 CHP §2.6.1 |
| N/A | `0xCA47` | sprmCMajority | A CMajorityOperand value that specifies which of the character properties of the text to reset to match the properties of the underlying paragraph style, taking the style hierarchy into account. | 字符 CHP §2.6.1；仅清除与下层段落样式等值的冗余直接格式，渲染结果等价 |
| ✅ | `0x2A48` | sprmCIss | Subscript or superscript formatting of the text. | 字符 CHP §2.6.1 |
| ✅ | `0x484B` | sprmCHpsKern | Minimum text size in half-points above which kerning is applied. | 字符 CHP §2.6.1 |
| N/A | `0x484E` | sprmCHresi | An HresiOperand value that specifies the word-breaking behavior for the text. | 字符 CHP §2.6.1；断词/换行行为提示；OOXML 无对应字符属性，由渲染端断词规则决定 |
| ✅ | `0x4A4F` | sprmCRgFtc0 | A 2-byte signed integer value that is an index into the font table (SttbfFfn). | 字符 CHP §2.6.1 |
| ✅ | `0x4A50` | sprmCRgFtc1 | A 2-byte signed integer value that is an index into the font table (SttbfFfn).；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x4A51` | sprmCRgFtc2 | Font table index for the remaining non-ASCII text range.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x4852` | sprmCCharScale | A 2-byte unsigned integer that specifies the percentage by which to horizontally scale the text, thereby changing the shape of the characters. | 字符 CHP §2.6.1 |
| ✅ | `0x2A53` | sprmCFDStrike | A ToggleOperand value that specifies whether the text is formatted with the double strikethrough effect. | 字符 CHP §2.6.1 |
| ✅ | `0x0854` | sprmCFImprint | A ToggleOperand value that specifies whether the text is formatted with the imprint effect. | 字符 CHP §2.6.1 |
| N/A | `0x0855` | sprmCFSpec | A ToggleOperand value that specifies whether the current text has a meaning that differs or displays differently than the underlying character to which it is applied. | 字符 CHP §2.6.1；特殊字符标记；所列字符（图片/脚注/批注/域/绘图）渲染分别由 M4/M5 链承载 |
| ◐ | `0x0856` | sprmCFObj | A Bool8 value that specifies whether the current text represents an embedded object. | 字符 CHP §2.6.1；IR 已保存，OLE 嵌入按 sprmCFOle2/CPicLocation 输出；其他对象类型未覆盖 |
| N/A | `0xCA57` | sprmCPropRMark90 | A PropRMarkOperand value that specifies whether the character run has an associated property revision mark, as well as its author and date/time.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| ✅ | `0x0858` | sprmCFEmboss | A ToggleOperand value that specifies whether the text is embossed. | 字符 CHP §2.6.1 |
| ✅ | `0x2859` | sprmCSfxText | A byte that specifies a text effect to apply to the text.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x085A` | sprmCFBiDi | Specifies right-to-left text layout.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x085C` | sprmCFBoldBi | A ToggleOperand value that specifies whether the text is formatted bold when displayed right-to-left or determined to be complex script. | 字符 CHP §2.6.1 |
| ✅ | `0x085D` | sprmCFItalicBi | A ToggleOperand value that specifies whether the text is italicized when displayed right-to-left or determined to be complex script. | 字符 CHP §2.6.1 |
| ✅ | `0x4A5E` | sprmCFtcBi | A 2-byte signed index into the font table (SttbfFfn).；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x485F` | sprmCLidBi | A LID value that specifies the language of the text when the text is displayed right-to-left or if the text is complex script.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x4A60` | sprmCIcoBi | An ICO value that specifies the color of text when displayed right-to-left or determined to be complex script.<147> | 字符 CHP §2.6.1；M3Batch8.ComplexScriptColor |
| ✅ | `0x4A61` | sprmCHpsBi | An unsigned 2-byte integer value that specifies the size of the text, for text that is displayed right-to-left or text that is a complex script. | 字符 CHP §2.6.1 |
| N/A | `0xCA62` | sprmCDispFldRMark | A DispFldRmOperand value that indicates a revision within the result of a display field.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| N/A | `0x4863` | sprmCIbstRMarkDel | A signed 16-bit integer value that specifies a zero-based index into SttbfRMark.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| N/A | `0x6864` | sprmCDttmRMarkDel | A DTTM value that specifies the date and time at which the text was deleted.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| ✅ | `0x6865` | sprmCBrc80 | A Brc80 value that specifies the border of all four sides of the text. | 字符 CHP §2.6.1 |
| ✅ | `0x4866` | sprmCShd80 | A Shd80 structure that specifies the background shading for the text. | 字符 CHP §2.6.1；M3Batch7.Shd80PaletteToRgb |
| N/A | `0x4867` | sprmCIdslRMarkDel | An unsigned 16-bit integer that specifies the reason why the text under revision was deleted.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| ✅ | `0x0868` | sprmCFUsePgsuSettings | A ToggleOperand value that specifies whether the text is to be displayed according to the document grid.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x486D` | sprmCRgLid0_80 | A LID value that specifies the language of the text, except for East Asian languages.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x486E` | sprmCRgLid1_80 | A LID value that specifies the language of the text if it is an East Asian language.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x286F` | sprmCIdctHint | An 8-bit unsigned integer value that specifies which of the language, font, size, bold, and italic properties is to be used for handling the text, in the case where this cannot be derived…；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x6870` | sprmCCv | COLORREF value specifying text color. | 字符 CHP §2.6.1 |
| ✅ | `0xCA71` | sprmCShd | A SHDOperand value that specifies the background shading for the text. | 字符 CHP §2.6.1 |
| ✅ | `0xCA72` | sprmCBrc | A BrcOperand value that specifies the border on all four sides of the text. | 字符 CHP §2.6.1 |
| ✅ | `0x4873` | sprmCRgLid0 | A LID value that specifies the language of the text, except for East Asian languages.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x4874` | sprmCRgLid1 | A LID value that specifies the language of the text if it is an East Asian language.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0x0875` | sprmCFNoProof | A ToggleOperand value that specifies whether the text is excluded from the proofing analysis.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0xCA76` | sprmCFitText | A CFitTextOperand value that specifies a width, in twips, to which text is expanded or condensed to fit. | 字符 CHP §2.6.1；M3Batch8.FitTextAndEastAsianLayout |
| ✅ | `0x6877` | sprmCCvUl | A COLORREF value that specifies the color of the text underline.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| ✅ | `0xCA78` | sprmCFELayout | A FarEastLayoutOperand value that specifies text layout information for East Asian languages. | 字符 CHP §2.6.1；M3Batch8.FitTextAndEastAsianLayout |
| N/A | `0x2879` | sprmCLbcCRJ | An LBCOperand value that specifies that this character is a special character representing a line break of the given type. | 字符 CHP §2.6.1；仅作用于 U+000B；docx 以 w:br 表达换行字符本身，分栏/分页由节与分栏结构表达 |
| ✅ | `0x0882` | sprmCFComplexScripts | A ToggleOperand value that specifies whether complex script formatting (for example, see sprmCFBoldBi) is applied to the text regardless of the Unicode characters themselves.；M3Batch5.CharacterLanguageAndScriptOpcodes | 字符 CHP §2.6.1 |
| N/A | `0x2A83` | sprmCWall | A Bool8 value that specifies whether the values of character properties are preserved for revision-marking purposes until the modifications are accepted or rejected by the user.；修订元数据留 M4；不影响当前格式值 | 字符 CHP §2.6.1 |
| ✅ | `0xCA85` | sprmCCnf | A CNFOperand that specifies conditional character formatting for a specific condition of a table style. | 字符 CHP §2.6.1；M3Batch8.TableStyleConditionalFormatting |
| N/A | `0x2A86` | sprmCNeedFontFixup | An FFM that specifies the type of font substitution that is needed for the associated text. | 字符 CHP §2.6.1；字体替换提示；渲染端按 w:rFonts 与字体可用性自行替换 |
| N/A | `0x6887` | sprmCPbiIBullet | A CP value in the Bullet Pictures document that specifies which picture is used as a bullet character when rendering the bullet. | 字符 CHP §2.6.1；图片项目符号位置；需 Bullet Pictures 位图与 numbering 的 numPicBullet（M5） |
| N/A | `0x4888` | sprmCPbiGrf | A PbiGrfOperand value that specifies whether a picture is used as a bullet character when rendering the bullet. | 字符 CHP §2.6.1；图片项目符号属性；同 sprmCPbiIBullet，随 M5 输出 |
| N/A | `0xCA89` | sprmCPropRMark | A PropRMarkOperand value that specifies whether the text has an associated property revision mark, as well as its author and the date and time. | 字符 CHP §2.6.1；属性修订元数据（作者/时间）留 M4，与 sprmCPropRMark90 同处理 |
| ✅ | `0x2A90` | sprmCFSdtVanish | A Bool8 value that specifies whether the text is hidden from display when the option to hide arbitrary XML delimiters is enabled. | 字符 CHP §2.6.1；并入 w:vanish；M3Batch7.HiddenTextSources |

## 2. 段落 PAP（§2.6.2，91 项）

| 状态 | opcode | sprm | 语义（规范摘要） | 归类 |
|---|---|---|---|---|
| ✅ | `0x4600` | sprmPIstd | An unsigned integer that specifies the istd of a paragraph style to apply. | 段落 PAP §2.6.2 |
| N/A | `0xC601` | sprmPIstdPermute | Paragraph style permutation mapping; changes the applied paragraph style when the current style is in the map. | 段落 PAP §2.6.2；编辑期样式置换表；同 sprmCIstdPermute |
| ✅ | `0x2602` | sprmPIncLvl | A signed 8-bit integer value. | 段落 PAP §2.6.2；累加到 ilvl；istd 分支未实现；M3Batch7.ParagraphNestAndListLevelIncrement |
| ✅ | `0x2403` | sprmPJc80 | An unsigned 8-bit integer that specifies the physical justification of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x2405` | sprmPFKeep | A Bool8 value that specifies whether an application SHOULD<148> keep this paragraph on a single page. | 段落 PAP §2.6.2 |
| ✅ | `0x2406` | sprmPFKeepFollow | A Bool8 value that specifies whether an application SHOULD<149> keep the end of this paragraph on the same page as the beginning of the next paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x2407` | sprmPFPageBreakBefore | A Bool8 value that specifies whether this paragraph has a page break before it. | 段落 PAP §2.6.2 |
| ✅ | `0x260A` | sprmPIlvl | An unsigned 8-bit integer that specifies the list level of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x460B` | sprmPIlfo | A 16-bit signed integer value that is used to determine which list contains the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x240C` | sprmPFNoLineNumb | A Bool8 value that specifies whether this paragraph is ignored when the application counts or displays line numbers. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0xC60D` | sprmPChgTabsPapx | A PChgTabsPapxOperand value that specifies custom tab stops to be added or ignored. | 段落 PAP §2.6.2 |
| ✅ | `0x840E` | sprmPDxaRight80 | An XAS value that specifies the physical right indent of the paragraph, in twips. | 段落 PAP §2.6.2 |
| ✅ | `0x840F` | sprmPDxaLeft80 | An XAS value that specifies the physical left indent of the paragraph, in twips. | 段落 PAP §2.6.2 |
| ✅ | `0x4610` | sprmPNest80 | An XAS value that is added to sprmPDxaLeft80 to specify the final indent of a paragraph. | 段落 PAP §2.6.2；累加到 w:ind/@w:left；M3Batch7.ParagraphNestAndListLevelIncrement |
| ✅ | `0x8411` | sprmPDxaLeft180 | An XAS value that specifies the logical left indent of the first line of the paragraph, in twips, relative to the rest of the paragraph. | 段落 PAP §2.6.2；M3Batch6.ExistingParagraphAndCharacterPaths |
| ✅ | `0x6412` | sprmPDyaLine | An LSPD value that specifies the spacing between lines in this paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0xA413` | sprmPDyaBefore | A two-byte unsigned integer value that specifies the size, in twips, of the spacing before this paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0xA414` | sprmPDyaAfter | A two-byte unsigned integer value that specifies the size, in twips, of the spacing after this paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0xC615` | sprmPChgTabs | A PChgTabsOperand value that specifies custom tab stops that are added or ignored. | 段落 PAP §2.6.2 |
| ✅ | `0x2416` | sprmPFInTable | A Bool8 value that specifies whether this paragraph is in a table.；M3Batch3.TDefTableCellWidthBorderAndMerge | 段落 PAP §2.6.2 |
| ✅ | `0x2417` | sprmPFTtp | A Bool8 that, when set to 1, specifies that the cell mark it is applied to is a Table Terminating Paragraph (TTP) mark.；M3Batch3.TDefTableCellWidthBorderAndMerge | 段落 PAP §2.6.2 |
| ✅ | `0x8418` | sprmPDxaAbs | Horizontal absolute position of the paragraph frame relative to its anchor. | 段落 PAP §2.6.2；w:framePr/@w:x；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x8419` | sprmPDyaAbs | A YAS_plusOne value that specifies downward vertical position relative to the vertical anchor of the frame. | 段落 PAP §2.6.2；w:framePr/@w:y；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x841A` | sprmPDxaWidth | A XAS_nonNeg value that specifies the width of the frame. | 段落 PAP §2.6.2；w:framePr/@w:w；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x261B` | sprmPPc | Frame anchor used to calculate its position. | 段落 PAP §2.6.2 |
| ✅ | `0x2423` | sprmPWr | A 1-byte integer that specifies how text is wrapped around a frame. | 段落 PAP §2.6.2 |
| ✅ | `0x6424` | sprmPBrcTop80 | A Brc80 value that specifies the top border of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x6425` | sprmPBrcLeft80 | A Brc80 value that specifies the logical left border of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x6426` | sprmPBrcBottom80 | A Brc80 value that specifies the bottom border of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x6427` | sprmPBrcRight80 | A Brc80 value that specifies the logical right border of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x6428` | sprmPBrcBetween80 | A Brc80 value that specifies the border between this paragraph and the next. | 段落 PAP §2.6.2 |
| ✅ | `0x6629` | sprmPBrcBar80 | A Brc80 value that has no effect. | 段落 PAP §2.6.2 |
| ✅ | `0x242A` | sprmPFNoAutoHyph | A Bool8 value that specifies whether this paragraph is auto- hyphenated when hyphenation is enabled for the document. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x442B` | sprmPWHeightAbs | A WHeightAbs value that specifies the height of the frame. | 段落 PAP §2.6.2；w:framePr/@w:h + @w:hRule；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x442C` | sprmPDcs | A DCS value that specifies the properties, if any, of the drop cap for this paragraph. | 段落 PAP §2.6.2；M3Batch8.DropCapAndFrameTextFlow |
| ✅ | `0x442D` | sprmPShd80 | A Shd80 structure that specifies the background shading for the paragraph. | 段落 PAP §2.6.2；SHD80 调色板索引转 RGB；M3Batch7.Shd80PaletteToRgb |
| ✅ | `0x842E` | sprmPDyaFromText | A YAS_nonNeg value that specifies the minimum vertical distance between the edge of the frame and the edge of main document text that wraps around the frame. | 段落 PAP §2.6.2；w:framePr/@w:vSpace；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x842F` | sprmPDxaFromText | A XAS_nonNeg value that specifies the minimum horizontal distance between the edge of the frame and the edge of main document text that wraps around the frame. | 段落 PAP §2.6.2；w:framePr/@w:hSpace；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x2430` | sprmPFLocked | A Bool8 value that specifies whether the anchor of the frame which contains this paragraph is locked to its current location. | 段落 PAP §2.6.2；w:framePr/@w:lock；M3Batch7.ParagraphFrameGeometry |
| ✅ | `0x2431` | sprmPFWidowControl | A Bool8 value that specifies whether widow and orphan control is enabled for this paragraph. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2433` | sprmPFKinsoku | A Bool8 value that specifies whether this paragraph uses East Asian typography and line-breaking rules to determine the valid characters that are allowed to begin and end each line of East… | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2434` | sprmPFWordWrap | A Bool8 value that, when equal to 0, specifies a preference to break Latin text that exceeds text line limits by breaking a word across two lines (breaking on the character level). | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2435` | sprmPFOverflowPunct | A Bool8 value that, when equal to 0, specifies a preference against allowing a punctuation character that follows a word at the end of a line to appear beyond the extent of that line of text. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2436` | sprmPFTopLinePunct | A Bool8 value that specifies a preference to render punctuation characters at the beginning of a line so that they appear to be closer to both the beginning of the line and to the next… | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2437` | sprmPFAutoSpaceDE | A Bool8 value that specifies whether space is automatically inserted between East Asian and Latin text. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2438` | sprmPFAutoSpaceDN | A Bool8 value that specifies whether space is automatically inserted between East Asian text and numbers. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x4439` | sprmPWAlignFont | A 16-bit unsigned integer that specifies vertical font alignment for East Asian languages. | 段落 PAP §2.6.2；M3Batch8.VerticalFontAlignment |
| ✅ | `0x443A` | sprmPFrameTextFlow | A FrameTextFlowOperand that specifies the direction of text flow in the frame. | 段落 PAP §2.6.2；M3Batch8.DropCapAndFrameTextFlow |
| ✅ | `0x2640` | sprmPOutLvl | An unsigned 8-bit integer value that specifies the outline level of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x2441` | sprmPFBiDi | A Bool8 value that specifies whether the paragraph uses right-to-left layout. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| N/A | `0x2443` | sprmPFNumRMIns | A Bool8 value that specifies whether a numbered list was applied to this paragraph after the previous revision.；修订元数据留 M4；不影响当前格式值 | 段落 PAP §2.6.2 |
| N/A | `0xC645` | sprmPNumRM | A NumRMOperand value that specifies a numbering revision mark for this paragraph.；修订元数据留 M4；不影响当前格式值 | 段落 PAP §2.6.2 |
| ✅ | `0x6646` | sprmPHugePapx | A 4-byte unsigned integer that specifies a location in the Data Stream. | 段落 PAP §2.6.2；M3Batch7.IndirectParagraphSprms |
| ✅ | `0x2447` | sprmPFUsePgsuSettings | A Bool8 value that specifies whether the paragraph adheres to the vertical components of the document grid. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x2448` | sprmPFAdjustRight | A Bool8 value that specifies whether this paragraph is set to automatically adjust the right indent when a document grid for East Asian characters is defined. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x6649` | sprmPItap | An integer value that specifies the table depth of this paragraph. | 段落 PAP §2.6.2；sprmPItap 作为表格嵌套深度的权威来源；M3NestedTable.DepthTwoTableLivesInTheEnclosingCell、M4FastSaved.PiecePItapPlacesParagraphInsideTable |
| ✅ | `0x664A` | sprmPDtap | A signed integer that specifies an addition or subtraction to the existing table depth of this paragraph. | 段落 PAP §2.6.2；深度增量形式，累加到 sprmPItap；M3NestedTable.DeltaDepthIsAccumulated、M3NestedTable.DeltaDepthWideArithmetic |
| N/A | `0x244B` | sprmPFInnerTableCell | A Bool8 value that specifies whether this paragraph is the final paragraph in a nested table cell. | 段落 PAP §2.6.2；嵌套表单元格增量保存簿记；不影响格式 |
| ✅ | `0x244C` | sprmPFInnerTtp | A Bool8 value that specifies whether this paragraph is the final paragraph in a nested table row. | 段落 PAP §2.6.2；嵌套表行结束标记；M3NestedTable.WriterNestsTableElements |
| ✅ | `0xC64D` | sprmPShd | A SHDOperand value that specifies the background shading for the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0xC64E` | sprmPBrcTop | A BrcOperand value which specifies the top border of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0xC64F` | sprmPBrcLeft | A BrcOperand value that specifies the logical left border of the paragraph. | 段落 PAP §2.6.2；M3Batch6.ExistingParagraphAndCharacterPaths |
| ✅ | `0xC650` | sprmPBrcBottom | A BrcOperand value that specifies the bottom border of the paragraph This border is hidden if the next paragraph is identical to this one in terms of its top, bottom, left, and right borders;… | 段落 PAP §2.6.2；M3Batch6.ExistingParagraphAndCharacterPaths |
| ✅ | `0xC651` | sprmPBrcRight | A BrcOperand value that specifies the logical right border of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0xC652` | sprmPBrcBetween | Border between adjacent paragraphs. | 段落 PAP §2.6.2 |
| ✅ | `0xC653` | sprmPBrcBar | A BrcOperand value that has no effect. | 段落 PAP §2.6.2 |
| ✅ | `0x4455` | sprmPDxcRight | A signed 16-bit integer value that specifies the logical right indent of the paragraph in hundredths of character units. | 段落 PAP §2.6.2；M3Batch8.CharacterUnitIndentsAndLineSpacing |
| ✅ | `0x4456` | sprmPDxcLeft | A signed 16-bit integer value that specifies the logical left indent of the paragraph in hundredths of character units. | 段落 PAP §2.6.2；M3Batch8.CharacterUnitIndentsAndLineSpacing |
| ✅ | `0x4457` | sprmPDxcLeft1 | A signed 16-bit integer value that specifies the logical left indent of the first line of the paragraph, in hundredths of character units, relative to the rest of the paragraph. | 段落 PAP §2.6.2；M3Batch8.CharacterUnitIndentsAndLineSpacing |
| ✅ | `0x4458` | sprmPDylBefore | A signed 16-bit integer value that specifies the spacing before the paragraph, in 1/100 line units. | 段落 PAP §2.6.2；M3Batch8.CharacterUnitIndentsAndLineSpacing |
| ✅ | `0x4459` | sprmPDylAfter | A signed 16-bit integer that specifies the spacing after the paragraph, in 1/100 line units. | 段落 PAP §2.6.2；M3Batch8.CharacterUnitIndentsAndLineSpacing |
| N/A | `0x245A` | sprmPFOpenTch | A Bool8 value that specifies whether this table cell mark was being displayed when this file was last saved, even though it immediately follows a nested table. | 段落 PAP §2.6.2；保存时的单元格标记显示状态位；无格式语义 |
| ✅ | `0x245B` | sprmPFDyaBeforeAuto | A Bool8 value that specifies whether the space displayed before this paragraph uses auto spacing. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x245C` | sprmPFDyaAfterAuto | A Bool8 value that specifies whether the space displayed after this paragraph uses auto spacing. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| ✅ | `0x845D` | sprmPDxaRight | An XAS value that specifies the logical right indent of the paragraph, in twips. | 段落 PAP §2.6.2 |
| ✅ | `0x845E` | sprmPDxaLeft | An XAS value that specifies the logical left indent of the paragraph, in twips. | 段落 PAP §2.6.2 |
| ✅ | `0x465F` | sprmPNest | An XAS value that is added to the sprmPDxaLeft value to determine the final indent of a paragraph. | 段落 PAP §2.6.2；累加到 w:ind/@w:left；M3Batch7.ParagraphNestAndListLevelIncrement |
| ✅ | `0x8460` | sprmPDxaLeft1 | An XAS value that specifies the logical left indent of the first line of the paragraph, in twips, relative to the rest of the paragraph. | 段落 PAP §2.6.2 |
| ✅ | `0x2461` | sprmPJc | An unsigned 8-bit integer value that specifies the logical justification of the paragraph. | 段落 PAP §2.6.2 |
| N/A | `0x2462` | sprmPFNoAllowOverlap | Prevents overlap of the paragraph frame with other frames. | 段落 PAP §2.6.2；框架防重叠；OOXML w:framePr 无对应属性 |
| N/A | `0x2664` | sprmPWall | Preserves previous paragraph properties for revision marking.；修订元数据留 M4；不影响当前格式值 | 段落 PAP §2.6.2 |
| N/A | `0x6465` | sprmPIpgp | An unsigned integer value that specifies the PGPInfo.ipgpSelf value of the PGPInfo data that is applied to this paragraph. | 段落 PAP §2.6.2；PGPInfo 内部段落属性 id；无 docx 对应 |
| ✅ | `0xC666` | sprmPCnf | A CNFOperand value that specifies conditional paragraph formatting for a specific condition of a table style. | 段落 PAP §2.6.2；M3Batch8.TableStyleConditionalFormatting |
| N/A | `0x6467` | sprmPRsid | An integer value that specifies a revision save ID, as specified in [ECMA-376] Part 1, Section 17.15.1.70 rsid (Single Session Revision Save ID), associated with paragraph formatting.；修订元数据留 M4；不影响当前格式值 | 段落 PAP §2.6.2 |
| N/A | `0xC669` | sprmPIstdListPermute | An SPPOperand value that has no effect and MUST be ignored.；规范注明废弃或仅用于旧版，当前转换不适用 | 段落 PAP §2.6.2 |
| ✅ | `0x646B` | sprmPTableProps | An unsigned integer value that specifies a location in the Data Stream. | 段落 PAP §2.6.2；M3Batch3.IndirectTableProperties |
| N/A | `0xC66C` | sprmPTIstdInfo | Table style information associated with a paragraph. | 段落 PAP §2.6.2；段落的表样式条件记录；条件格式已由 sprmTCnf/sprmPCnf/sprmCCnf 输出 |
| ✅ | `0x246D` | sprmPFContextualSpacing | A Bool8 value that specifies whether contextual spacing is enabled for this paragraph. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| N/A | `0xC66F` | sprmPPropRMark | A PropRMarkOperand value that specifies whether the paragraph has an associated property revision mark, as well as its author and the date and time.；修订元数据留 M4；不影响当前格式值 | 段落 PAP §2.6.2 |
| ✅ | `0x2470` | sprmPFMirrorIndents | A Bool8 value that specifies whether the left and right indents that are set for this paragraph are interpreted as inside and outside margins for odd and even numbered pages. | 段落 PAP §2.6.2；M3Batch6.ParagraphLayoutFlags |
| N/A | `0x2471` | sprmPTtwo | A 1-byte integer that specifies text wrapping options for a text box when tight wrapping is set for the text box. | 段落 PAP §2.6.2；文本框紧环绕选项；随 M5 文本框链输出为 drawing 环绕属性 |

## 3. 表格 TAP（§2.6.3，80 项）

| 状态 | opcode | sprm | 语义（规范摘要） | 归类 |
|---|---|---|---|---|
| ✅ | `0x5400` | sprmTJc90 | An unsigned 16-bit integer value that specifies the physical justification of the table. | 表格 TAP §2.6.3；M3Batch6.ExistingAndNewTablePaths |
| ✅ | `0x9601` | sprmTDxaLeft | Table horizontal origin relative to the logical left margin. | 表格 TAP §2.6.3；M3Batch6.ExistingAndNewTablePaths |
| ✅ | `0x9602` | sprmTDxaGapHalf | An XAS value that specifies the average width, in twips, between the left and right default cell margins for the first cell in the row. | 表格 TAP §2.6.3 |
| ✅ | `0x3403` | sprmTFCantSplit90 | A Bool8 value. | 表格 TAP §2.6.3 |
| ✅ | `0x3404` | sprmTTableHeader | A Bool8 value that specifies that the current table row is a header row. | 表格 TAP §2.6.3 |
| ✅ | `0xD605` | sprmTTableBorders80 | A TableBordersOperand80 value that specifies border information for the cells in a table row. | 表格 TAP §2.6.3 |
| ✅ | `0x9407` | sprmTDyaRowHeight | A YAS value that specifies the height of the row. | 表格 TAP §2.6.3 |
| ✅ | `0xD608` | sprmTDefTable | A TDefTableOperand value that specifies the number of columns in the table row, the width of each column, border attributes, and a variety of other settings. | 表格 TAP §2.6.3 |
| ✅ | `0xD609` | sprmTDefTableShd80 | A DefTableShd80Operand value that specifies the default shading applied to each cell in a row. | 表格 TAP §2.6.3；M3Batch7.TableRowShd80 |
| ✅ | `0x740A` | sprmTTlp | A TLP structure that specifies the table style options for this table. | 表格 TAP §2.6.3；M3Batch8.TableStyleOptionsAndAnchors；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0x560B` | sprmTFBiDi | A Bool16 value that specifies whether this table is right-to-left. | 表格 TAP §2.6.3 |
| ✅ | `0xD60C` | sprmTDefTableShd3rd | A DefTableShdOperand that specifies the default shading for cells 45 to 63 in the row, or all remaining cells in the row beginning with cell 45 if the row contains fewer than 63 cells. cb… | 表格 TAP §2.6.3 |
| ✅ | `0x360D` | sprmTPc | A PositionCodeOperand structure that specifies the origin that is used to calculate the table position when it is absolutely positioned. | 表格 TAP §2.6.3；M3Batch8.TableStyleOptionsAndAnchors |
| ✅ | `0x940E` | sprmTDxaAbs | A XAS_plusOne value that specifies the horizontal position of the table relative to the horizontal anchor of the table. | 表格 TAP §2.6.3；w:tblpPr/@w:tblpX；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0x940F` | sprmTDyaAbs | A YAS_plusOne value that specifies downward vertical position relative to the vertical anchor of the table. | 表格 TAP §2.6.3；w:tblpPr/@w:tblpY；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0x9410` | sprmTDxaFromText | An XAS_nonNeg value that specifies the minimum horizontal distance between the physical left edge of the table and the physical right edge of the text that wraps around the table. | 表格 TAP §2.6.3；w:tblpPr/@w:leftFromText；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0x9411` | sprmTDyaFromText | A YAS_nonNeg value that specifies the minimum vertical distance between the top edge of the table and the bottom edge of text that wraps around the table. | 表格 TAP §2.6.3；w:tblpPr/@w:topFromText；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0xD612` | sprmTDefTableShd | A DefTableShdOperand value that specifies the default shading for cells 1 – 22 in the row, or all cells in the row if the row contains fewer than 22 cells. | 表格 TAP §2.6.3 |
| ✅ | `0xD613` | sprmTTableBorders | A TableBordersOperand value that specifies the borders for this row unless modified by other Sprms applied to the cells. | 表格 TAP §2.6.3 |
| ✅ | `0xF614` | sprmTTableWidth | An FtsWWidth_Table structure that specifies the preferred total width of the table of which this row is a part. | 表格 TAP §2.6.3 |
| ✅ | `0x3615` | sprmTFAutofit | A Bool8 value that specifies whether the table column widths are to be automatically resized to best fit the contents of the whole table. | 表格 TAP §2.6.3 |
| ✅ | `0xD616` | sprmTDefTableShd2nd | A DefTableShdOperand that specifies the default shading for cells 23 – 44 in the row, or all remaining cells in the row beginning with cell 23 if the row contains fewer than 44 cells. | 表格 TAP §2.6.3 |
| N/A | `0xF617` | sprmTWidthBefore | An FtsWWidth_TablePart structure that specifies the preferred additional leading indent of the first cell of the row, relative to the leading edge of the table as a whole. | 表格 TAP §2.6.3；行级额外前置缩进；docx 只有表级 w:tblInd |
| N/A | `0xF618` | sprmTWidthAfter | An FtsWWidth_TablePart structure that specifies the preferred trailing indent following the last cell of the row. | 表格 TAP §2.6.3；行级额外后置缩进；同 sprmTWidthBefore |
| ✅ | `0x3619` | sprmTFKeepFollow | A Bool8 value that specifies whether page breaks are avoided between the rows of this table, if possible. | 表格 TAP §2.6.3；M3Batch6.ExistingAndNewTablePaths |
| ✅ | `0xD61A` | sprmTBrcTopCv | Top border color for cells in a table row. | 表格 TAP §2.6.3；M3Batch8.RowBorderColors |
| ✅ | `0xD61B` | sprmTBrcLeftCv | A BrcCvOperand value that specifies the color of the logical left border for each cell in a table row. | 表格 TAP §2.6.3；M3Batch8.RowBorderColors |
| ✅ | `0xD61C` | sprmTBrcBottomCv | A BrcCvOperand value that specifies the color of the bottom border for each cell in a table row. | 表格 TAP §2.6.3；M3Batch8.RowBorderColors |
| ✅ | `0xD61D` | sprmTBrcRightCv | A BrcCvOperand value that specifies the color of the logical right border for each cell in a table row. | 表格 TAP §2.6.3；M3Batch8.RowBorderColors |
| ✅ | `0x941E` | sprmTDxaFromTextRight | An XAS_nonNeg value that specifies the minimum horizontal distance between the physical right edge of the table and the physical left edge of the text that wraps around the table. | 表格 TAP §2.6.3；w:tblpPr/@w:rightFromText；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0x941F` | sprmTDyaFromTextBottom | A YAS_nonNeg value that specifies the minimum vertical distance between the bottom edge of the table and the top edge of text that wraps around the table. | 表格 TAP §2.6.3；w:tblpPr/@w:bottomFromText；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0xD620` | sprmTSetBrc80 | A TableBrc80Operand value that specifies the borders of a set of cells in the table row. | 表格 TAP §2.6.3 |
| ✅ | `0x7621` | sprmTInsert | A TInsertOperand value that specifies a range of new table cell definitions to insert into the table row. | 表格 TAP §2.6.3 |
| ✅ | `0x5622` | sprmTDelete | An ItcFirstLim value that specifies a range of table cell definitions to delete from the table row. | 表格 TAP §2.6.3 |
| ✅ | `0x7623` | sprmTDxaCol | A TDxaColOperand value that specifies the width of a range of cells in this table. | 表格 TAP §2.6.3 |
| ✅ | `0x5624` | sprmTMerge | An ItcFirstLim structure that specifies a set of cells in the current table row that are to be merged. | 表格 TAP §2.6.3 |
| ✅ | `0x5625` | sprmTSplit | An ItcFirstLim structure that specifies a set of cells in the current table row that are not to be merged. | 表格 TAP §2.6.3 |
| ✅ | `0x7629` | sprmTTextFlow | A CellRangeTextFlow value that specifies a set of cells in the current table row and the text flow model for each cell. | 表格 TAP §2.6.3 |
| ✅ | `0xD62B` | sprmTVertMerge | A VertMergeOperand value that specifies a cell in the current row, and whether that cell is vertically merged with the cell above or below it. | 表格 TAP §2.6.3 |
| ✅ | `0xD62C` | sprmTVertAlign | A CellRangeVertAlign value that specifies a set of cells in the current table row and the vertical alignment of cell contents in each cell. | 表格 TAP §2.6.3 |
| ✅ | `0xD62D` | sprmTSetShd | A TableShadeOperand value that specifies a set of cells in a table row and the background shading for each cell. | 表格 TAP §2.6.3 |
| ✅ | `0xD62E` | sprmTSetShdOdd | A TableShadeOperand value that specifies a set of cells in a table row and the background shading for odd numbered cells in that set. | 表格 TAP §2.6.3 |
| ✅ | `0xD62F` | sprmTSetBrc | A TableBrcOperand value that specifies the border type of a set of cells in a table row. | 表格 TAP §2.6.3 |
| ✅ | `0xD632` | sprmTCellPadding | A CSSAOperand value that specifies the cell margin for one or more cell sides. cssa.ftsWidth MUST be ftsNil (0x00) or ftsDxa (0x03). | 表格 TAP §2.6.3 |
| ✅ | `0xD633` | sprmTCellSpacingDefault | A CSSAOperand that specifies the cell spacing for each cell in the entire row. cssa.itc.itcFirst MUST be 0, cssa.itc.itcLim MUST be 1, cssa.grfbrc MUST be fbrcSidesOnly (0x0F), cssa.ftsWidth… | 表格 TAP §2.6.3 |
| ✅ | `0xD634` | sprmTCellPaddingDefault | A CSSAOperand that specifies the cell margin for one or more cell sides for each cell in the entire row. cssa.itc.itcFirst MUST be 0, cssa.itc.itcLim MUST be 1, cssa.ftsWidth MUST be ftsNil… | 表格 TAP §2.6.3 |
| ✅ | `0xD635` | sprmTCellWidth | Preferred width of one or more table cells. | 表格 TAP §2.6.3 |
| ✅ | `0xF636` | sprmTFitText | A CellRangeFitText value that specifies a set of cells in a table row and whether their contents are to be stretched or compressed to exactly fill their widths. | 表格 TAP §2.6.3 |
| ✅ | `0xD639` | sprmTFCellNoWrap | A CellRangeNoWrap value that specifies a set of cells in a table row and whether their contents wrap over multiple lines. | 表格 TAP §2.6.3 |
| ✅ | `0x563A` | sprmTIstd | An unsigned integer value that specifies the istd value of a table style to apply. | 表格 TAP §2.6.3；w:tblStyle；M3Batch7.TableStyleAndFloatingPosition |
| ✅ | `0xD63E` | sprmTCellPaddingStyle | Default cell padding for a table style. | 表格 TAP §2.6.3；M3Batch8.TableStyleOptionsAndAnchors；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0xD642` | sprmTCellFHideMark | A CellHideMarkOperand that specifies that table cell content is rendered with no height if all cells in the row are empty; however, cells have a visible height if they have nonzero cell… | 表格 TAP §2.6.3 |
| ✅ | `0xD660` | sprmTSetShdTable | A SHDOperand value that specifies the background shading for the entire table. | 表格 TAP §2.6.3 |
| ✅ | `0xF661` | sprmTWidthIndent | An FtsWWidth_Indent structure that specifies the preferred leading indent of the table where the row resides. | 表格 TAP §2.6.3 |
| N/A | `0xD662` | sprmTCellBrcType | A TCellBrcTypeOperand value that specifies the border type for the first several consecutive cells in a table row. | 表格 TAP §2.6.3；单元格整体边框类型数组（无边信息）；最终边框已由逐边 Brc 输出 |
| ✅ | `0x5664` | sprmTFBiDi90 | A Bool16 value that specifies whether this table is right-to-left. | 表格 TAP §2.6.3 |
| ✅ | `0x3465` | sprmTFNoAllowOverlap | A Bool8 value that specifies whether the table is allowed to overlap other tables. | 表格 TAP §2.6.3；M3Batch6.ExistingAndNewTablePaths |
| ✅ | `0x3466` | sprmTFCantSplit | A Bool8 value. | 表格 TAP §2.6.3 |
| N/A | `0xD667` | sprmTPropRMark | A PropRMarkOperand that specifies whether the table row has an associated property revision mark, as well as its author and date/time.；修订元数据留 M4；不影响当前格式值 | 表格 TAP §2.6.3 |
| N/A | `0x3668` | sprmTWall | A Bool8 value that specifies whether the values of table properties are preserved for revision marking purposes until the modifications are accepted or rejected by the user.；修订元数据留 M4；不影响当前格式值 | 表格 TAP §2.6.3 |
| N/A | `0x7469` | sprmTIpgp | An unsigned integer value that specifies the PGPInfo.ipgpSelf value of the PGPInfo data to be applied to this table row. | 表格 TAP §2.6.3；PGPInfo 内部 id；同 sprmPIpgp |
| ✅ | `0xD66A` | sprmTCnf | A CNFOperand that specifies conditional table formatting for a specific condition of a table style. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0xD670` | sprmTDefTableShdRaw | A DefTableShdOperand value that specifies the default shading for cells 1 to 22 in the row, or all cells in the row if the row contains fewer than 22 cells. | 表格 TAP §2.6.3 |
| ✅ | `0xD671` | sprmTDefTableShdRaw2nd | A DefTableShdOperand value that specifies the default shading for cells 23 to 44 in the row, or all remaining cells in the row beginning with cell 23 if the row contains fewer than 44 cells. | 表格 TAP §2.6.3；M3Batch6.ExistingAndNewTablePaths |
| ✅ | `0xD672` | sprmTDefTableShdRaw3rd | A DefTableShdOperand that specifies the default shading for cells 45 to 63 in the row, or all remaining cells in the row beginning with cell 45 if the row contains fewer than 63 cells. cb… | 表格 TAP §2.6.3；M3Batch6.ExistingAndNewTablePaths |
| N/A | `0x7479` | sprmTRsid | An integer value that specifies a revision save ID, as specified in [ECMA- 376] Part 1, Section 17.15.1.70 rsid (Single Session Revision Save ID), associated with table formatting.；修订元数据留 M4；不影响当前格式值 | 表格 TAP §2.6.3 |
| ✅ | `0x347C` | sprmTCellVertAlignStyle | A VerticalAlign value that specifies the vertical alignment of content within cells as defined by a Table style. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0x347D` | sprmTCellNoWrapStyle | A Bool8 value that specifies whether content within cells MAY<154> word wrap. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0xD47F` | sprmTCellBrcTopStyle | A BrcOperand value that specifies the top border for cells that are affected by a CNFOperand value. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0xD680` | sprmTCellBrcBottomStyle | A BrcOperand value that specifies the bottom border for cells that are affected by a CNFOperand value. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD681` | sprmTCellBrcLeftStyle | A BrcOperand value that specifies the logical left border for cells that are affected by a CNFOperand value. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD682` | sprmTCellBrcRightStyle | A BrcOperand value that specifies the logical right border for cells that are affected by a CNFOperand value. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD683` | sprmTCellBrcInsideHStyle | A BrcOperand value that specifies the border between a table row that is affected by a CNFOperand value and the following table row. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD684` | sprmTCellBrcInsideVStyle | A BrcOperand value that specifies the border between cells of a table row that are affected by a CNFOperand. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD685` | sprmTCellBrcTL2BRStyle | A BrcOperand value that specifies a diagonal border from the top, logical left corner to the bottom, logical right corner of each cell that is affected by a CNFOperand. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD686` | sprmTCellBrcTR2BLStyle | A BrcOperand value that specifies a diagonal border from the top, logical right corner to the bottom, logical left corner of each cell that is affected by a CNFOperand. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting |
| ✅ | `0xD687` | sprmTCellShdStyle | A SHDOperand value that specifies the background shading to be applied to an entire table defined by a Table style. | 表格 TAP §2.6.3；M3Batch8.TableStyleConditionalFormatting；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0x3488` | sprmTCHorzBands | An unsigned 8-bit integer value that specifies the number of rows in a horizontal band that is used for conditional formatting as defined by a Table style. | 表格 TAP §2.6.3；M3Batch8.TableStyleOptionsAndAnchors；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0x3489` | sprmTCVertBands | An unsigned 8-bit integer value that specifies the number of columns in a vertical band that is used for conditional formatting as defined by a Table style. | 表格 TAP §2.6.3；M3Batch8.TableStyleOptionsAndAnchors；M3Styles.TableStyleDefaultsAndConditionals |
| ✅ | `0x548A` | sprmTJc | An unsigned 16-bit integer value that specifies the logical justification of the table. | 表格 TAP §2.6.3 |

## 4. 节 SEP（§2.6.4，59 项）

| 状态 | opcode | sprm | 语义（规范摘要） | 归类 |
|---|---|---|---|---|
| ✅ | `0x3000` | sprmScnsPgn | A CNS indicating the number separator used between the chapter number and the page number for purpose of chapter numbering in page number fields (that is, when sprmSiHeadingPgn specifies a… | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x3001` | sprmSiHeadingPgn | An unsigned 8-bit integer value that specifies which heading level starts new chapters for the purposes of chapter numbering in page number fields. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0xF203` | sprmSDxaColWidth | An SDxaColWidthOperand that specifies the width of a particular column, in case columns are not evenly spaced as specified by sprmSFEvenlySpaced. | 节 SEP §2.6.4；w:cols/w:col/@w:w；M3Batch7.SectionColumnWidthAndSpacing |
| ✅ | `0xF204` | sprmSDxaColSpacing | An SDxaColSpacingOperand that specifies the spacing between two columns in case columns are not evenly spaced (as instructed by sprmSFEvenlySpaced). | 节 SEP §2.6.4；w:cols/w:col/@w:space；M3Batch7.SectionColumnWidthAndSpacing |
| ✅ | `0x3005` | sprmSFEvenlySpaced | A Bool8 value that specifies whether the space between page margins is distributed evenly between all columns (after subtracting the space between columns, as instructed by sprmSDxaColumns).；非等宽栏输出 w:cols/@w:equalWidth="0" | 节 SEP §2.6.4；M3Batch7.SectionColumnWidthAndSpacing |
| ✅ | `0x3006` | sprmSFProtected | A Bool8 value that specifies whether the section is unprotected in case document editing is restricted to form fields only (see DopBase.fProtEnabled). | 节 SEP §2.6.4；M3Batch8.PageBorderScopeAndFormProtection |
| ✅ | `0x5007` | sprmSDmBinFirst | A SDmBinOperand that specifies the paper source used by the printer for the first page of the section. | 节 SEP §2.6.4；w:paperSrc/@w:first；0xFFFF 视为自动；M3Batch7.SectionPaperSourceAndEndnotePosition |
| ✅ | `0x5008` | sprmSDmBinOther | An SDmBinOperand that specifies the paper source used by the printer for all pages in the section except the first. | 节 SEP §2.6.4；w:paperSrc/@w:other；0xFFFF 视为自动；M3Batch7.SectionPaperSourceAndEndnotePosition |
| ✅ | `0x3009` | sprmSBkc | An SBkcOperand that specifies what kind of section break terminates the section. | 节 SEP §2.6.4 |
| ✅ | `0x300A` | sprmSFTitlePage | A Bool8 value that specifies whether the section has a different first page (a "title page"). | 节 SEP §2.6.4 |
| ✅ | `0x500B` | sprmSCcolumns | An unsigned 16-bit integer whose value is one less than the number of columns in this section. | 节 SEP §2.6.4 |
| ✅ | `0x900C` | sprmSDxaColumns | An XAS_nonNeg that specifies the space between columns, in case columns are evenly spaced (as instructed by sprmSFEvenlySpaced). | 节 SEP §2.6.4 |
| ✅ | `0x300E` | sprmSNfcPgn | Page number format for the section.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x3011` | sprmSFPgnRestart | A Bool8 value that specifies whether the section starts with a new page number. | 节 SEP §2.6.4 |
| ✅ | `0x3012` | sprmSFEndnote | A Bool8 value that specifies whether endnotes are shown at the end of the section. | 节 SEP §2.6.4；w:endnotePr/w:pos=sectEnd；M3Batch7.SectionPaperSourceAndEndnotePosition |
| ✅ | `0x3013` | sprmSLnc | An SLncOperand that specifies the line numbering mode to use in case line numbers are enabled (see sprmSNLnnMod).；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x5015` | sprmSNLnnMod | An unsigned 16-bit integer that specifies the distance in the number of lines between line number labels.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x9016` | sprmSDxaLnn | An XAS_nonNeg that specifies the distance between line numbers and the lines of text to which they apply.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0xB017` | sprmSDyaHdrTop | A YAS_nonNeg that specifies the header distance, in twips, from the top edge of the page.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0xB018` | sprmSDyaHdrBottom | Footer distance in twips from the bottom page edge.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x3019` | sprmSLBetween | A Bool8 value that specifies whether lines are drawn between columns of text.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x301A` | sprmSVjc | A Vjc value that specifies the vertical justification of the section.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x501B` | sprmSLnnMin | An unsigned 16-bit integer whose value is one less than the starting value for line numbers.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x501C` | sprmSPgnStart97 | An unsigned 16-bit integer that specifies the starting value for page numbers when the section has page number restart enabled (as specified by sprmSFPgnRestart). | 节 SEP §2.6.4 |
| ✅ | `0x301D` | sprmSBOrientation | An SBOrientationOperand that specifies the page orientation of the section. | 节 SEP §2.6.4 |
| ✅ | `0xB01F` | sprmSXaPage | An unsigned 16-bit integer that specifies the page width of the section in twips. | 节 SEP §2.6.4 |
| ✅ | `0xB020` | sprmSYaPage | An unsigned 16-bit integer that specifies the page height of the section, in twips. | 节 SEP §2.6.4 |
| ✅ | `0xB021` | sprmSDxaLeft | An XAS_nonNeg that specifies the width, in twips, of the left margin. | 节 SEP §2.6.4 |
| ✅ | `0xB022` | sprmSDxaRight | An XAS_nonNeg that specifies the width, in twips, of the right margin. | 节 SEP §2.6.4 |
| ✅ | `0x9023` | sprmSDyaTop | A YAS that specifies the height of the top margin, in twips. | 节 SEP §2.6.4 |
| ✅ | `0x9024` | sprmSDyaBottom | A YAS that specifies the height of the bottom margin, in twips. | 节 SEP §2.6.4 |
| ✅ | `0xB025` | sprmSDzaGutter | An unsigned 16-bit integer that specifies the size of the gutter margin, in twips. | 节 SEP §2.6.4 |
| N/A | `0x5026` | sprmSDmPaperReq | A 16-bit unsigned integer that specifies a tie-breaker value to be used when more than one available paper format ("Letter Matte", "Letter Gloss", "Letter w/ Letterhead", "Letter Pink", and… | 节 SEP §2.6.4；打印机纸张 tie-breaker；不影响页面排版 |
| ✅ | `0x3228` | sprmSFBiDi | A Bool8 value that specifies whether the section uses right-to-left layout; that is, line numbers are displayed on the right side of text and columns are populated from right to left.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x322A` | sprmSFRTLGutter | A Bool8 value that specifies whether the gutter margin requires right-to-left layout.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x702B` | sprmSBrcTop80 | A Brc80 that specifies the top page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x702C` | sprmSBrcLeft80 | A Brc80 that specifies the left page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x702D` | sprmSBrcBottom80 | A Brc80 that specifies the bottom page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x702E` | sprmSBrcRight80 | A Brc80 that specifies the right page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x522F` | sprmSPgbProp | An SPgbPropOperand that specifies page border properties. | 节 SEP §2.6.4；M3Batch8.PageBorderScopeAndFormProtection |
| ✅ | `0x7030` | sprmSDxtCharSpace | A signed 32-bit integer that specifies the difference between the desired character pitch for the document grid, if enabled (see sprmSClm), and the pitch of the font that is specified by the… | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x9031` | sprmSDyaLinePitch | A YAS that specifies, in twips, the line height that is used for document grid, if enabled (see sprmSClm). | 节 SEP §2.6.4 |
| ✅ | `0x5032` | sprmSClm | An SClmOperand that specifies the document grid mode that is in use for the section. | 节 SEP §2.6.4 |
| ✅ | `0x5033` | sprmSTextFlow | A MSOTXFL that specifies the text flow of the section, as specified in [MS- ODRAW] section 2.4.5. | 节 SEP §2.6.4 |
| ✅ | `0xD234` | sprmSBrcTop | A BrcOperand that specifies the top page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0xD235` | sprmSBrcLeft | A BrcOperand that specifies the left page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0xD236` | sprmSBrcBottom | A BrcOperand that specifies the bottom page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0xD237` | sprmSBrcRight | A BrcOperand that specifies the right page border. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| N/A | `0x3239` | sprmSWall | A Bool8 value that specifies whether the values of section properties are preserved for revision marking purposes until the modifications are accepted or rejected by the user.；修订元数据留 M4；不影响当前格式值 | 节 SEP §2.6.4 |
| N/A | `0x703A` | sprmSRsid | An integer that specifies a revision save ID, as specified in [ECMA-376] Part 1, Section 17.15.1.70 rsid (Single Session Revision Save ID), associated with section formatting.；修订元数据留 M4；不影响当前格式值 | 节 SEP §2.6.4 |
| ✅ | `0x303B` | sprmSFpc | An SFpcOperand that specifies the footnote positioning for the section.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x303C` | sprmSRncFtn | An Rnc that specifies whether and when footnote numbering is restarted.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x303E` | sprmSRncEdn | An Rnc value that specifies whether and when endnote numbering is restarted. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x503F` | sprmSNFtn | An unsigned 16-bit integer that specifies an offset to add to footnote numbers in this section.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x5040` | sprmSNfcFtnRef | A 16-bit MSONFC (as specified in [MS-OSHARED] section 2.2.1.3) that specifies the numbering format used for footnotes.；M3Batch5.SectionModifiersPreviouslyUnasserted | 节 SEP §2.6.4 |
| ✅ | `0x5041` | sprmSNEdn | An unsigned 16-bit integer that specifies an offset to add to endnote numbers in this section. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| ✅ | `0x5042` | sprmSNfcEdnRef | A 16-bit MSONFC (as specified in [MS-OSHARED] section 2.2.1.3) that specifies the numbering format used for endnotes. | 节 SEP §2.6.4；M3Batch6.SectionBordersAndNumbering |
| N/A | `0xD243` | sprmSPropRMark | A PropRMarkOperand that specifies whether the section has an associated property revision mark, as well as its author and date/time.；修订元数据留 M4；不影响当前格式值 | 节 SEP §2.6.4 |
| ✅ | `0x7044` | sprmSPgnStart | An unsigned 32-bit integer that specifies the starting value for page numbers when the section has page number restart enabled (as specified by sprmSFPgnRestart). | 节 SEP §2.6.4 |

## 统计

| 类别 | 总数 | ✅ | ◐ | ☐ | N/A | 覆盖率（✅ / 适用项） |
|---|---:|---:|---:|---:|---:|---:|
| 字符 CHP | 84 | 59 | 0 | 0 | 25 | 100% |
| 段落 PAP | 91 | 78 | 0 | 0 | 13 | 100% |
| 表格 TAP | 80 | 73 | 0 | 0 | 7 | 100% |
| 节 SEP | 59 | 55 | 0 | 0 | 4 | 100% |
| **合计** | 314 | 265 | 0 | 0 | 49 | 100% |

**口径说明（重要）**：本表只统计 [MS-DOC] §2.6 四张 sprm 表的**属性**覆盖。✅ 表示该 opcode 的解析、IR 字段与 OOXML 输出齐备且有一条断言（相当比例是合成位流断言，而非真实 Word 文档）。它**不等于**格式还原率或产品完成度：结构级特性（图片、文本框、OLE、脚注尾注批注书签、嵌套表格、多节页眉页脚、fast-save、加密）不在本表计数内，见 `DESIGN.md` §6 与 `KNOWN-SIMPLIFICATIONS.md`。

M6 的 RC4 解密和加密类型检测状态记录在 `DESIGN.md` §6.8；它们不对应 sprm opcode，因此不改变本表的覆盖计数。

基线：上一轮清单为 151 ✅ / 52.1%（同一份规范表口径）。第 7 批回填 65 ✅，第 8 批新增 45 ✅ 并把 29 项按上述四类归为 N/A，覆盖率达到 100%；后续把 sprmPItap/PDtap/PFInnerTtp 从 N/A 拉回实现。M5 内联图片再把 sprmCPicLocation 的图片分支转为实现（当前 265 / 265 适用项）。

## Word 6/95 旧版 opcode（不计入 314）

> 旧版 grpprl 用 1 字节 opcode（LibreOffice `sprmids.hxx` v6 命名空间 + wvWare
> `rgsprmWord6` 双源核对）。`src/doc/format/fkp.cc` 的 `NormalizeLegacySprms`
> 把已支持项翻译为上表现代 opcode 后走同一 IR 路径；FKP 与样式 UPX 共用。
> 未列出的 opcode 一律警告并跳过该组剩余修饰（partial-keep）。

| 旧版 opcode | sprm | 现代去向 | 备注 |
|---|---|---|---|
| `0x05` | sprmPJc | sprmPJc80 | 1B |
| `0x07`/`0x08`/`0x09` | sprmPFKeep/KeepFollow/PageBreakBefore | 同名 | 1B |
| `0x0F` | sprmPChgTabsPapx | —（丢弃） | 变长，按 cb 消费避免错位；制表位不迁移 |
| `0x10`/`0x11`/`0x13` | sprmPDxaRight/Left/Left1 | sprmPDxa*80 | 2B |
| `0x14` | sprmPDyaLine | 同名 | 4B |
| `0x15`/`0x16` | sprmPDyaBefore/After | 同名 | 2B |
| `0x18`/`0x19` | sprmPFInTable/PFTtp | 同名 | 1B |
| `0x2C` | sprmPFNoAutoHyph | 同名 | 1B；M13 |
| `0x33` | sprmPFWidowControl | 同名 | 1B |
| `0x55`/`0x56`/`0x57` | sprmCFBold/Italic/Strike | 同名 | 1B |
| `0x5D` | sprmCFtc | sprmCRgFtc0 | 2B；M13 起映射字体名 |
| `0x5E` | sprmCKul | 同名 | 1B |
| `0x60` | sprmCDxaSpace | 同名 | 2B；M13 |
| `0x62` | sprmCIco | 同名 | 1B |
| `0x63` | sprmCHps | 同名 | **2B**；wvWare spra 位与 LO aSprms 表双证，hps<256 时与 1 字节读法等价；M13 修正 |

> 实测样本中仍遇到的未映射项：`0x25` sprmPWr（十进制 37，框架环绕；本语料未遇到，
> 早年登记时误写作十六进制 `0x37`）、`0x61` sprmCLid（语言；
> 现代对应 sprmCRgLid0 操作数宽度不同，不能直译）。

## 规范外旧账别名（不计入 314）

| 旧账项目 | 核对结果 |
|---|---|
| sprmCFLanguage `0x083F` | 当前 §2.6.1 无此 opcode；语言由 `sprmCRgLid0_80/1_80`、`sprmCRgLid0/1`、`sprmCLidBi` 表示，均已覆盖。旧别名 N/A。 |
| sprmCFObjOffset、sprmCObjOffset | 当前四表无此名称或 opcode；对象位置以 `sprmCPicLocation` 表示，M5 处理对象载荷。旧别名 N/A。 |
| sprmCCondTable | 当前 §2.6.1 名称为 `sprmCCnf` (`0xCA85`)，条件表格样式尚未实现。旧别名 N/A。 |
| sprmPWatermark `0x6422` | 当前 §2.6.2 不含该 opcode；水印按图片/页眉载荷处理。旧别名 N/A。 |
| sprmCRmf `0x4875` | 当前 §2.6.1 不含该 opcode；`0x0875` 是 `sprmCFNoProof`。旧别名 N/A。 |
| sprmSDopLayoutUnix | 当前 §2.6.4 无此名称；属于文档级兼容设置而非 SEPX sprm。旧别名 N/A。 |
| sprmSFTextFlow `0x3615` | `0x3615` 是表格 `sprmTFAutofit`；节文字方向为 `sprmSTextFlow` (`0x5033`)，已覆盖。旧别名 N/A。 |
| sprmCFRMark | 当前名称为 `sprmCFRMarkIns` (`0x0801`)；修订标记及 `sprmCFRMarkDel`、`sprmCPropRMark` 完整语义留 M4。 |

## 5. 第 7 批增量（本次 28 项）

| opcode | sprm | 解析落点 | OOXML 输出 |
|---|---|---|---|
| `0x2A34` | sprmCKcd | `Run.emphasis` | `<w:em w:val="dot\|comma\|circle\|underDot\|none"/>` |
| `0x2A90` | sprmCFSdtVanish | `Run.hidden` | `<w:vanish/>`（与 `sprmCFVanish` 同路） |
| `0x2A33` | sprmCPlain | 调用点重置 `Run` 为样式继承态 | 无独立元素（丢弃此前直接字符格式） |
| `0x0802` | sprmCFFldVanish | `Run.field_hidden` | `<w:vanish/>` |
| `0x4866` | sprmCShd80 | 调色板索引转 RGB | `<w:shd w:val w:fill w:color/>` |
| `0x2430` | sprmPFLocked | `Paragraph.frame_lock` | `<w:framePr w:lock="1"/>` |
| `0x8418` | sprmPDxaAbs | `frame_x` | `w:framePr/@w:x` |
| `0x8419` | sprmPDyaAbs | `frame_y` | `w:framePr/@w:y` |
| `0x841A` | sprmPDxaWidth | `frame_width` | `w:framePr/@w:w` |
| `0x442B` | sprmPWHeightAbs | `frame_height`/`frame_height_exact` | `w:framePr/@w:h` + `@w:hRule` |
| `0x842E` | sprmPDyaFromText | `frame_v_space` | `w:framePr/@w:vSpace` |
| `0x842F` | sprmPDxaFromText | `frame_h_space` | `w:framePr/@w:hSpace` |
| `0x442D` | sprmPShd80 | 调色板索引转 RGB | `<w:pPr><w:shd/>` |
| `0x2602` | sprmPIncLvl | `list_level_inc` | `<w:numPr><w:ilvl/>`（累加并夹到 0–8） |
| `0x4610` | sprmPNest80 | `nest` | `<w:ind w:left/>`（与 `sprmPDxaLeft80` 相加） |
| `0x465F` | sprmPNest | `nest` | `<w:ind w:left/>` |
| `0x6646` | sprmPHugePapx | `WalkParagraph` 递归 Data 流 | 嵌套 sprm 照常展开 |
| `0x563A` | sprmTIstd | `Table.style` | `<w:tblStyle w:val/>` |
| `0x940E` | sprmTDxaAbs | `tblp_x` | `<w:tblpPr w:tblpX/>` |
| `0x940F` | sprmTDyaAbs | `tblp_y` | `<w:tblpPr w:tblpY/>` |
| `0x9410` | sprmTDxaFromText | `from_text[0]` | `@w:leftFromText` |
| `0x9411` | sprmTDyaFromText | `from_text[1]` | `@w:topFromText` |
| `0x941E` | sprmTDxaFromTextRight | `from_text[2]` | `@w:rightFromText` |
| `0x941F` | sprmTDyaFromTextBottom | `from_text[3]` | `@w:bottomFromText` |
| `0xD609` | sprmTDefTableShd80 | 行内 SHD80 数组转 RGB | `<w:tcPr><w:shd/>` |
| `0x5007` | sprmSDmBinFirst | `Section.paper_first` | `<w:paperSrc w:first/>`（`0xFFFF` 视为自动，不输出） |
| `0x5008` | sprmSDmBinOther | `Section.paper_other` | `<w:paperSrc w:other/>` |
| `0x3012` | sprmSFEndnote | `endnote_at_section_end` | `<w:endnotePr><w:pos w:val="sectEnd"/>` |
| `0xF203` | sprmSDxaColWidth | `ColumnSpec.width` | `<w:cols><w:col w:w/>` |
| `0xF204` | sprmSDxaColSpacing | `ColumnSpec.space` | `<w:cols><w:col w:space/>` |

### 5.1 本批已知简化

- `sprmPIncLvl` 只累加 `ilvl`；`sprmPIlfo == 0` 时按规范应增量 `istd` 的分支未实现。
- `sprmPNest`/`sprmPNest80` 只折算进 `w:ind/@w:left`，不参与悬挂/首行缩进重算。
- 浮动表格：`sprmTPc`（`0x360D`）未实现，`w:tblpPr` 的 `w:horzAnchor`/`w:vertAnchor` 固定输出 `margin`。
- 非等宽分栏只对已知宽度的列输出 `<w:col>`；仅给出间距（无宽度）的列不输出。
- SHD80 只有 16 色调色板，索引 0（自动色）不产生 `w:shd`。
- `w:framePr` 的位置属性用 `-1` 作“未设置”哨兵，合法的 `-1` twip 偏移无法表达。
- `sprmCPlain` 只丢弃已累积的直接字符格式，不重建样式继承链。

## 6. 第 8 批增量（收敛到 100%）

本批新增 45 项实现、29 项 N/A 归类，表格口径见上。实现按 OOXML 落点分组：

| 组 | opcode | 输出 |
|---|---|---|
| 修订 | `0x0800` `0x0801` | 文本包进 `<w:del>`/`<w:ins>`，删除文本用 `<w:delText>` |
| 东亚版式 | `0xCA76` `0xCA78` | `<w:fitText>`、`<w:eastAsianLayout>`（vert/combine/combineBrackets/vertCompress） |
| 颜色 | `0x4A60` | RTL/复杂文种下 `<w:color>` 取 sprmCIcoBi |
| 段落框 | `0x442C` `0x443A` `0x4439` | `w:framePr/@w:dropCap+@w:lines`、`@w:textFlow`、`<w:textAlignment>` |
| 字符单位缩进 | `0x4455` `0x4456` `0x4457` `0x4458` `0x4459` | `w:ind/@w:leftChars/@w:rightChars/@w:firstLineChars`、`w:spacing/@w:beforeLines/@w:afterLines` |
| 表样式条件格式 | `0xCA85` `0xC666` `0xD66A` `0x347C` `0x347D` `0xD47F` `0xD680`–`0xD687` `0xD63E` `0x3488` `0x3489` | `<w:tblStylePr w:type>` 内的 `w:pPr`/`w:rPr`/`w:tcPr`，以及 `w:tblPr` 的 `w:tblStyleRowBandSize`/`w:tblStyleColBandSize`/`w:tblCellMar`；优先级 `M3Styles.TableConditionalPriorityIgnoresStshOrder` |
| 浮动表格 | `0x360D` `0x740A` | `w:tblpPr/@w:horzAnchor+@w:vertAnchor`（sprmTPc）、`w:tblLook`（sprmTTlp） |
| 行边框颜色 | `0xD61A`–`0xD61D` | 逐单元格 Brc 的 `w:color` |
| 页面边框/保护 | `0x522F` `0x3006` `0x702C` `0x702D` `0xD235` `0xD236` | `w:pgBorders/@w:offsetFrom+@w:display`、`<w:formProt>`、左右页边框 |

### 6.1 N/A 归类明细（第 8 批 29 项，M5 已转实现 1 项）

| 类别 | opcode | 理由 |
|---|---|---|
| M5 图片/OLE 载荷 | `0x0806` `0x080A` `0x0856` `0x0855` `0x6887` `0x4888` `0x2471` | 非图片二进制/OLE/图片项目符号/文本框环绕仍待 M5 后续；`0x6A03` 的正文图片分支已实现 |
| M4 修订元数据 | `0xCA89` | 作者与时间戳需要 SttbfRMark，与 `sprmCPropRMark90` 同处理 |
| 规范允许忽略 | `0xC81A` | §2.6.1 脚注 145 允许实现忽略；公式由 OMML 重排 |
| 编辑期/增量保存内部状态 | `0xC601` `0xCA31` `0x244B` `0x245A` `0x6465` `0x7469` `0xCA47` | 样式置换表、嵌套表簿记、PGPInfo id；docx 用显式结构或同源读取表达，最终格式等价 |
| 无 docx 对应属性 | `0x2879` `0x484E` `0x2A86` `0x2462` `0xC66C` `0xF617` `0xF618` `0xD662` `0x5026` | 换行类型/断词提示/字体替换/框架防重叠/表级缩进/纸张 tie-breaker；条件格式已由 CNF 三兄弟输出 |
