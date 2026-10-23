# M2 Codex 战报

## 实现

FIB 版本/标志/文本段长度与 CLX 定位；双编码 piece table 文本抽取；最小 Document IR；store ZIP/CRC32 DOCX；CLI convert。

## 规范与样本核验

- FIB 的 `csw → cslw → cbRgFcLcb` 读取顺序按 [MS-DOC] 2.5.15；`fcClx` 在 FibRgFcLcb97 第 33 对。
- `FcCompressed` 的 bit30 表示压缩，`fc/2` 为字节偏移；PlcPcd 大小按 `4 + 12*n` 验证。
- 五份真实语料均转换成功；LibreOffice TXT 基准已入库。表格样本的 cell/row 控制码产生连续换行；列表样本的项目符号来自编号属性，故测试将其从基准内容比较中剥除。

## 验收命令与原始输出

### `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`

```text
-- Configuring done (1.0s)
-- Generating done (0.1s)
-- Build files have been written to: /home/ubuntu/doccraft/build
[  8%] Built target gtest
[ 25%] Built target doccraft_model
[ 33%] Built target doccraft_doc
[ 37%] Built target doccraft_cfb
[ 45%] Built target gtest_main
[ 54%] Built target doccraft_docx
[ 62%] Built target gmock
[ 75%] Built target cfb_reader_test
[ 83%] Built target gmock_main
[ 91%] Built target doccraft
[100%] Built target m2_test
```

退出码：0

### `ctest --test-dir build --output-on-failure`

```text
Internal ctest changing into directory: /home/ubuntu/doccraft/build
Test project /home/ubuntu/doccraft/build
      Start  1: inspect_plain_text
 1/24 Test  #1: inspect_plain_text ...............................................   Passed    0.00 sec
      Start  2: CfbReaderTest.RejectsNonCfbBytes
 2/24 Test  #2: CfbReaderTest.RejectsNonCfbBytes .................................   Passed    0.01 sec
      Start  3: CfbReaderTest.RejectsTruncatedHeader
 3/24 Test  #3: CfbReaderTest.RejectsTruncatedHeader .............................   Passed    0.00 sec
      Start  4: CfbReaderTest.OpensSyntheticSingleStream
 4/24 Test  #4: CfbReaderTest.OpensSyntheticSingleStream .........................   Passed    0.00 sec
      Start  5: CfbReaderTest.ReportsSectorSize
 5/24 Test  #5: CfbReaderTest.ReportsSectorSize ..................................   Passed    0.00 sec
      Start  6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory
 6/24 Test  #6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory ..................   Passed    0.00 sec
      Start  7: CfbReaderTest.ReadsFourKilobyteSectors
 7/24 Test  #7: CfbReaderTest.ReadsFourKilobyteSectors ...........................   Passed    0.00 sec
      Start  8: CfbReaderTest.ReadsDifatExtension
 8/24 Test  #8: CfbReaderTest.ReadsDifatExtension ................................   Passed    0.05 sec
      Start  9: CfbReaderTest.RejectsCorruptLinksAndTruncation
 9/24 Test  #9: CfbReaderTest.RejectsCorruptLinksAndTruncation ...................   Passed    0.01 sec
      Start 10: CfbReaderTest.RejectsRootSiblingWithoutThrowing
10/24 Test #10: CfbReaderTest.RejectsRootSiblingWithoutThrowing ..................   Passed    0.01 sec
      Start 11: CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing
11/24 Test #11: CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing ........   Passed    0.01 sec
      Start 12: CfbReaderTest.FiveStorageLevelsKeepCorrectPath
12/24 Test #12: CfbReaderTest.FiveStorageLevelsKeepCorrectPath ...................   Passed    0.01 sec
      Start 13: CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing
13/24 Test #13: CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing .........   Passed    0.01 sec
      Start 14: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing
14/24 Test #14: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing ...   Passed    0.01 sec
      Start 15: CfbReaderTest.ReadsLibreOfficePlainTextCorpus
15/24 Test #15: CfbReaderTest.ReadsLibreOfficePlainTextCorpus ....................   Passed    0.00 sec
      Start 16: CfbReaderTest.ReadsEveryStreamInDocCorpus
16/24 Test #16: CfbReaderTest.ReadsEveryStreamInDocCorpus ........................   Passed    0.00 sec
      Start 17: CfbReaderTest.BuilderRepackagesLibreOfficeStreams
17/24 Test #17: CfbReaderTest.BuilderRepackagesLibreOfficeStreams ................   Passed    0.00 sec
      Start 18: FibTest.ParsesFlagsAndOffsets
18/24 Test #18: FibTest.ParsesFlagsAndOffsets ....................................   Passed    0.00 sec
      Start 19: FibTest.RejectsEncryption
19/24 Test #19: FibTest.RejectsEncryption ........................................   Passed    0.00 sec
      Start 20: PieceTableTest.RejectsFastSavedDocuments
20/24 Test #20: PieceTableTest.RejectsFastSavedDocuments .........................   Passed    0.00 sec
      Start 21: PieceTableTest.ReadsCompressedAndUtf16Pieces
21/24 Test #21: PieceTableTest.ReadsCompressedAndUtf16Pieces .....................   Passed    0.00 sec
      Start 22: ZipTest.CrcKnownVector
22/24 Test #22: ZipTest.CrcKnownVector ...........................................   Passed    0.00 sec
      Start 23: docx_zip_validate
23/24 Test #23: docx_zip_validate ................................................   Passed    0.07 sec
      Start 24: corpus_text_consistency
24/24 Test #24: corpus_text_consistency ..........................................   Passed    0.09 sec

100% tests passed, 0 tests failed out of 24

Total Test time (real) =   0.33 sec
```

退出码：0

### `python3 tests/corpus/compare_text.py build/src/cli/doccraft`

```text
alignment-indents.doc: 100.00%
bold-italic-underline.doc: 100.00%
headings-list.doc: 100.00%
plain-text.doc: 100.00%
simple-table.doc: 100.00%
minimum: 100.00%
```

退出码：0

### `python3 tests/pixelcompare/compare.py tests/corpus/corpus/plain-text.doc`

```text
OK  page 1: rms=0.00 max=0 bad_px=0 (0.000%)

PASS (1 pages, threshold 0.1%) — heatmaps: build/pixelcompare/
```

退出码：0

### `ASAN_OPTIONS=detect_leaks=0 cmake --build build-asan -j && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure`

```text
[  4%] Building CXX object src/doc/CMakeFiles/doccraft_doc.dir/fib.cc.o
[  8%] Building CXX object src/doc/CMakeFiles/doccraft_doc.dir/text.cc.o
[ 16%] Built target gtest
[ 29%] Built target doccraft_cfb
[ 25%] Building CXX object src/model/CMakeFiles/doccraft_model.dir/document.cc.o
[ 37%] Built target gmock
[ 45%] Built target gtest_main
[ 54%] Built target gmock_main
[ 66%] Built target cfb_reader_test
[ 70%] Linking CXX static library libdoccraft_doc.a
[ 70%] Built target doccraft_doc
[ 75%] Linking CXX static library libdoccraft_model.a
[ 75%] Built target doccraft_model
[ 79%] Building CXX object src/docx/CMakeFiles/doccraft_docx.dir/writer.cc.o
[ 83%] Linking CXX static library libdoccraft_docx.a
[ 83%] Built target doccraft_docx
[ 87%] Building CXX object tests/CMakeFiles/m2_test.dir/m2_test.cc.o
[ 91%] Building CXX object src/cli/CMakeFiles/doccraft.dir/main.cc.o
[ 95%] Linking CXX executable doccraft
[ 95%] Built target doccraft
[100%] Linking CXX executable m2_test
[100%] Built target m2_test
Internal ctest changing into directory: /home/ubuntu/doccraft/build-asan
Test project /home/ubuntu/doccraft/build-asan
      Start  1: inspect_plain_text
 1/24 Test  #1: inspect_plain_text ...............................................   Passed    0.03 sec
      Start  2: CfbReaderTest.RejectsNonCfbBytes
 2/24 Test  #2: CfbReaderTest.RejectsNonCfbBytes .................................   Passed    0.06 sec
      Start  3: CfbReaderTest.RejectsTruncatedHeader
 3/24 Test  #3: CfbReaderTest.RejectsTruncatedHeader .............................   Passed    0.02 sec
      Start  4: CfbReaderTest.OpensSyntheticSingleStream
 4/24 Test  #4: CfbReaderTest.OpensSyntheticSingleStream .........................   Passed    0.03 sec
      Start  5: CfbReaderTest.ReportsSectorSize
 5/24 Test  #5: CfbReaderTest.ReportsSectorSize ..................................   Passed    0.03 sec
      Start  6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory
 6/24 Test  #6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory ..................   Passed    0.03 sec
      Start  7: CfbReaderTest.ReadsFourKilobyteSectors
 7/24 Test  #7: CfbReaderTest.ReadsFourKilobyteSectors ...........................   Passed    0.03 sec
      Start  8: CfbReaderTest.ReadsDifatExtension
 8/24 Test  #8: CfbReaderTest.ReadsDifatExtension ................................   Passed    0.16 sec
      Start  9: CfbReaderTest.RejectsCorruptLinksAndTruncation
 9/24 Test  #9: CfbReaderTest.RejectsCorruptLinksAndTruncation ...................   Passed    0.03 sec
      Start 10: CfbReaderTest.RejectsRootSiblingWithoutThrowing
10/24 Test #10: CfbReaderTest.RejectsRootSiblingWithoutThrowing ..................   Passed    0.03 sec
      Start 11: CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing
11/24 Test #11: CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing ........   Passed    0.09 sec
      Start 12: CfbReaderTest.FiveStorageLevelsKeepCorrectPath
12/24 Test #12: CfbReaderTest.FiveStorageLevelsKeepCorrectPath ...................   Passed    0.03 sec
      Start 13: CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing
13/24 Test #13: CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing .........   Passed    0.09 sec
      Start 14: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing
14/24 Test #14: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing ...   Passed    0.39 sec
      Start 15: CfbReaderTest.ReadsLibreOfficePlainTextCorpus
15/24 Test #15: CfbReaderTest.ReadsLibreOfficePlainTextCorpus ....................   Passed    0.04 sec
      Start 16: CfbReaderTest.ReadsEveryStreamInDocCorpus
16/24 Test #16: CfbReaderTest.ReadsEveryStreamInDocCorpus ........................   Passed    0.09 sec
      Start 17: CfbReaderTest.BuilderRepackagesLibreOfficeStreams
17/24 Test #17: CfbReaderTest.BuilderRepackagesLibreOfficeStreams ................   Passed    0.05 sec
      Start 18: FibTest.ParsesFlagsAndOffsets
18/24 Test #18: FibTest.ParsesFlagsAndOffsets ....................................   Passed    0.02 sec
      Start 19: FibTest.RejectsEncryption
19/24 Test #19: FibTest.RejectsEncryption ........................................   Passed    0.02 sec
      Start 20: PieceTableTest.RejectsFastSavedDocuments
20/24 Test #20: PieceTableTest.RejectsFastSavedDocuments .........................   Passed    0.02 sec
      Start 21: PieceTableTest.ReadsCompressedAndUtf16Pieces
21/24 Test #21: PieceTableTest.ReadsCompressedAndUtf16Pieces .....................   Passed    0.02 sec
      Start 22: ZipTest.CrcKnownVector
22/24 Test #22: ZipTest.CrcKnownVector ...........................................   Passed    0.02 sec
      Start 23: docx_zip_validate
23/24 Test #23: docx_zip_validate ................................................   Passed    0.11 sec
      Start 24: corpus_text_consistency
24/24 Test #24: corpus_text_consistency ..........................................   Passed    0.21 sec

100% tests passed, 0 tests failed out of 24

Total Test time (real) =   1.66 sec
```

退出码：0

## 遗留 gap

- `fComplex=1` 增量保存文档明确拒绝；加密文档待 M6。
- 列表编号、表格结构、样式和其他文档属性待后续里程碑；当前字体与页边距为最小写出所需的固定默认值。
- `0x07`/`0x0B` 在 DOCX 写为换行，`0x1E` 写为不间断连字符；IR 保留源控制码及其位置。
