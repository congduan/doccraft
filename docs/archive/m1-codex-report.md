# M1 CFB/OLE 实施战报

## 起点

命令：`git pull`、`git log -1 --oneline`

```text
Already up to date.
0c8bc45 test: golden corpus — 5 feature .doc samples + .docx baselines (LibreOffice)
```

## TDD 红阶段

先启用原有两个 `DISABLED_` 测试，再运行：

`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure`

原始失败输出中的测试结果：

```text
3/4 Test #3: CfbReaderTest.OpensSyntheticSingleStream ...***Failed    0.00 sec
/home/ubuntu/doccraft/tests/cfb_reader_test.cc:44: Failure
Value of: reader.has_value()
  Actual: false
Expected: true
not implemented (M1)
4/4 Test #4: CfbReaderTest.ReportsSectorSize ............***Failed    0.00 sec
/home/ubuntu/doccraft/tests/cfb_reader_test.cc:55: Failure
Value of: reader.has_value()
  Actual: false
Expected: true
50% tests passed, 2 tests failed out of 4
```

## 契约与语料复现

* 规范 [MS-CFB Compound File Header](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/05060311-bfce-4b12-874d-71fd4ce63aea) 规定 major version 3 的 Sector Shift 为 9（512 字节），major version 4 的 Shift 为 12（4096 字节）。任务书的“v3 读取：512/4096”不能同时成立；测试 `ReadsFourKilobyteSectors` 按规范生成 v4，现有 API 签名未改。
* `Visit` 的注释称 visitor 接收 entry 与 path，但模板约束实际要求 `(const DirEntry&, std::string_view, bool)`。最小编译复现是以双参数 lambda 调用 `Visit`，编译器报 `is_invocable_v<..., const DirEntry&, std::string_view, bool> evaluated to false`。实现保持三参数签名，第三参数表示当前节点是否为 storage/root。
* 任务书要求 `plain-text.doc` 存在 `Data` 流，但语料实际没有。复现命令：`strings -el tests/corpus/corpus/plain-text.doc | rg '^(Data|1Table|WordDocument)$'`。原始输出：

```text
1Table
WordDocument
```

因此真实语料测试断言 `WordDocument` 与 `1Table` 存在、可完整读取且长度等于目录记载值，并断言 `Data` 返回 `kStreamNotFound`。所有五个现有 `.doc` 样本均无 `Data` 流，无法在不修改语料的情况下满足原要求的 `Data` 存在性断言。

## Builder 与 LibreOffice 交叉验证

命令：使用 `CfbReader` 枚举 `plain-text.doc` 的流，将每个流原样交给 `CfbBuilder::AddStream`，生成 `/tmp/doccraft-rebuilt.doc`，随后运行：

`libreoffice -env:UserInstallation=file:///tmp/doccraft-lo-profile --headless --convert-to docx --outdir /tmp /tmp/doccraft-rebuilt.doc`

原始输出：

```text
Warning: failed to launch javaldx - java may not function correctly
convert /tmp/doccraft-rebuilt.doc as a Writer document -> /tmp/doccraft-rebuilt.docx using filter : Office Open XML Text
```

命令：`unzip -p /tmp/doccraft-rebuilt.docx word/document.xml | rg -o 'Hello doccraft|Second paragraph'`

原始输出：

```text
Hello doccraft
Second paragraph
```

自动测试 `BuilderRepackagesLibreOfficeStreams` 对比重封装前后的每条流的完整字节。

## 实现范围

Reader 支持 v3/v4 header、FAT、DIFAT、MiniFAT、mini-stream、目录树与路径访问，检测截断、非法扇区、环及损坏链。Builder 生成两种扇区大小、目录树、小流与 DIFAT 扩展容器。CLI `inspect` 枚举目录。测试覆盖 1000 次确定性字节变异；这不是持续运行的 libFuzzer campaign。

## 最终验证

命令：`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure`

```text
-- Configuring done (0.5s)
-- Generating done (0.0s)
-- Build files have been written to: /home/ubuntu/doccraft/build
[ 14%] Built target doccraft_docx
[ 19%] Built target doccraft_model
[ 28%] Built target doccraft_cfb
[ 38%] Built target doccraft_doc
[ 47%] Built target gtest
[ 57%] Built target doccraft
[ 71%] Built target gtest_main
[ 76%] Built target gmock
[ 90%] Built target gmock_main
[ 90%] Building CXX object tests/CMakeFiles/cfb_reader_test.dir/cfb_reader_test.cc.o
[ 95%] Linking CXX executable cfb_reader_test
[100%] Built target cfb_reader_test
Internal ctest changing into directory: /home/ubuntu/doccraft/build
Test project /home/ubuntu/doccraft/build
      Start  1: inspect_plain_text
 1/12 Test  #1: inspect_plain_text ...............................................   Passed    0.00 sec
      Start  2: CfbReaderTest.RejectsNonCfbBytes
 2/12 Test  #2: CfbReaderTest.RejectsNonCfbBytes .................................   Passed    0.00 sec
      Start  3: CfbReaderTest.RejectsTruncatedHeader
 3/12 Test  #3: CfbReaderTest.RejectsTruncatedHeader .............................   Passed    0.00 sec
      Start  4: CfbReaderTest.OpensSyntheticSingleStream
 4/12 Test  #4: CfbReaderTest.OpensSyntheticSingleStream .........................   Passed    0.00 sec
      Start  5: CfbReaderTest.ReportsSectorSize
 5/12 Test  #5: CfbReaderTest.ReportsSectorSize ..................................   Passed    0.00 sec
      Start  6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory
 6/12 Test  #6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory ..................   Passed    0.00 sec
      Start  7: CfbReaderTest.ReadsFourKilobyteSectors
 7/12 Test  #7: CfbReaderTest.ReadsFourKilobyteSectors ...........................   Passed    0.00 sec
      Start  8: CfbReaderTest.ReadsDifatExtension
 8/12 Test  #8: CfbReaderTest.ReadsDifatExtension ................................   Passed    0.03 sec
      Start  9: CfbReaderTest.RejectsCorruptLinksAndTruncation
 9/12 Test  #9: CfbReaderTest.RejectsCorruptLinksAndTruncation ...................   Passed    0.00 sec
      Start 10: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing
10/12 Test #10: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing ...   Passed    0.01 sec
      Start 11: CfbReaderTest.ReadsLibreOfficePlainTextCorpus
11/12 Test #11: CfbReaderTest.ReadsLibreOfficePlainTextCorpus ....................   Passed    0.00 sec
      Start 12: CfbReaderTest.BuilderRepackagesLibreOfficeStreams
12/12 Test #12: CfbReaderTest.BuilderRepackagesLibreOfficeStreams ................   Passed    0.00 sec

100% tests passed, 0 tests failed out of 12

Total Test time (real) =   0.07 sec
```

交叉验证命令：`g++ -std=c++23 -I src -I tests /tmp/doccraft_cfb_roundtrip.cc tests/cfb_test_util.cc build/src/cfb/libdoccraft_cfb.a -o /tmp/doccraft_cfb_roundtrip && /tmp/doccraft_cfb_roundtrip && libreoffice -env:UserInstallation=file:///tmp/doccraft-lo-profile --headless --convert-to docx --outdir /tmp /tmp/doccraft-rebuilt.doc && unzip -p /tmp/doccraft-rebuilt.docx word/document.xml | rg -o "Hello doccraft|Second paragraph"`

```text
Warning: failed to launch javaldx - java may not function correctly
convert /tmp/doccraft-rebuilt.doc as a Writer document -> /tmp/doccraft-rebuilt.docx using filter : Office Open XML Text
Overwriting: /tmp/doccraft-rebuilt.docx
Hello doccraft
Second paragraph
```

命令：`cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" && cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure`

```text
-- The CXX compiler identification is GNU 13.3.0
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- The C compiler identification is GNU 13.3.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Found Python3: /home/ubuntu/.local/bin/python3.11 (found version "3.11.15") found components: Interpreter
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Configuring done (5.7s)
-- Generating done (0.0s)
-- Build files have been written to: /home/ubuntu/doccraft/build-asan
[  9%] Building CXX object src/doc/CMakeFiles/doccraft_doc.dir/empty.cc.o
[  9%] Building CXX object src/cfb/CMakeFiles/doccraft_cfb.dir/cfb_reader.cc.o
[ 14%] Building CXX object _deps/googletest-build/googletest/CMakeFiles/gtest.dir/src/gtest-all.cc.o
[ 19%] Building CXX object src/model/CMakeFiles/doccraft_model.dir/empty.cc.o
[ 23%] Building CXX object src/docx/CMakeFiles/doccraft_docx.dir/empty.cc.o
[ 28%] Linking CXX static library libdoccraft_docx.a
[ 33%] Linking CXX static library libdoccraft_doc.a
[ 38%] Linking CXX static library libdoccraft_model.a
[ 38%] Built target doccraft_docx
[ 38%] Built target doccraft_doc
[ 38%] Built target doccraft_model
[ 42%] Linking CXX static library libdoccraft_cfb.a
[ 42%] Built target doccraft_cfb
[ 47%] Building CXX object src/cli/CMakeFiles/doccraft.dir/main.cc.o
[ 52%] Linking CXX executable doccraft
[ 52%] Built target doccraft
[ 57%] Linking CXX static library ../../../lib/libgtest.a
[ 57%] Built target gtest
[ 61%] Building CXX object _deps/googletest-build/googletest/CMakeFiles/gtest_main.dir/src/gtest_main.cc.o
[ 66%] Building CXX object _deps/googletest-build/googlemock/CMakeFiles/gmock.dir/src/gmock-all.cc.o
[ 71%] Linking CXX static library ../../../lib/libgtest_main.a
[ 71%] Built target gtest_main
[ 76%] Building CXX object tests/CMakeFiles/cfb_reader_test.dir/cfb_reader_test.cc.o
[ 80%] Building CXX object tests/CMakeFiles/cfb_reader_test.dir/cfb_test_util.cc.o
[ 85%] Linking CXX static library ../../../lib/libgmock.a
[ 85%] Built target gmock
[ 90%] Building CXX object _deps/googletest-build/googlemock/CMakeFiles/gmock_main.dir/src/gmock_main.cc.o
[ 95%] Linking CXX executable cfb_reader_test
==1420664==LeakSanitizer has encountered a fatal error.
==1420664==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==1420664==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
CMake Error at /usr/share/cmake-3.28/Modules/GoogleTestAddTests.cmake:112 (message):
  Error running test executable.

    Path: '/home/ubuntu/doccraft/build-asan/tests/cfb_reader_test'
    Result: 1
    Output:
      Running main() from /home/ubuntu/doccraft/build-asan/_deps/googletest-src/googletest/src/gtest_main.cc
      CfbReaderTest.
        RejectsNonCfbBytes
        RejectsTruncatedHeader
        OpensSyntheticSingleStream
        ReportsSectorSize
        ReadsMiniStreamAndNestedDirectory
        ReadsFourKilobyteSectors
        ReadsDifatExtension
        RejectsCorruptLinksAndTruncation
        HandlesMutatedHeadersAndDirectoryWithoutCrashing
        ReadsLibreOfficePlainTextCorpus
        BuilderRepackagesLibreOfficeStreams


Call Stack (most recent call first):
  /usr/share/cmake-3.28/Modules/GoogleTestAddTests.cmake:225 (gtest_discover_tests_impl)


gmake[2]: *** [tests/CMakeFiles/cfb_reader_test.dir/build.make:117: tests/cfb_reader_test] Error 1
gmake[2]: *** Deleting file 'tests/cfb_reader_test'
gmake[1]: *** [CMakeFiles/Makefile2:497: tests/CMakeFiles/cfb_reader_test.dir/all] Error 2
gmake[1]: *** Waiting for unfinished jobs....
[100%] Linking CXX static library ../../../lib/libgmock_main.a
[100%] Built target gmock_main
gmake: *** [Makefile:146: all] Error 2
```

命令：`LSAN_OPTIONS=detect_leaks=0 cmake --build build-asan -j && LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure`

```text
[ 14%] Built target doccraft_model
[ 19%] Built target doccraft_doc
[ 28%] Built target doccraft_docx
[ 38%] Built target doccraft_cfb
[ 47%] Built target gtest
[ 57%] Built target gtest_main
[ 66%] Built target doccraft
[ 76%] Built target gmock
[ 80%] Linking CXX executable cfb_reader_test
[ 90%] Built target gmock_main
[100%] Built target cfb_reader_test
Internal ctest changing into directory: /home/ubuntu/doccraft/build-asan
Test project /home/ubuntu/doccraft/build-asan
      Start  1: inspect_plain_text
 1/12 Test  #1: inspect_plain_text ...............................................   Passed    0.02 sec
      Start  2: CfbReaderTest.RejectsNonCfbBytes
 2/12 Test  #2: CfbReaderTest.RejectsNonCfbBytes .................................   Passed    0.02 sec
      Start  3: CfbReaderTest.RejectsTruncatedHeader
 3/12 Test  #3: CfbReaderTest.RejectsTruncatedHeader .............................   Passed    0.02 sec
      Start  4: CfbReaderTest.OpensSyntheticSingleStream
 4/12 Test  #4: CfbReaderTest.OpensSyntheticSingleStream .........................   Passed    0.02 sec
      Start  5: CfbReaderTest.ReportsSectorSize
 5/12 Test  #5: CfbReaderTest.ReportsSectorSize ..................................   Passed    0.02 sec
      Start  6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory
 6/12 Test  #6: CfbReaderTest.ReadsMiniStreamAndNestedDirectory ..................   Passed    0.03 sec
      Start  7: CfbReaderTest.ReadsFourKilobyteSectors
 7/12 Test  #7: CfbReaderTest.ReadsFourKilobyteSectors ...........................   Passed    0.02 sec
      Start  8: CfbReaderTest.ReadsDifatExtension
 8/12 Test  #8: CfbReaderTest.ReadsDifatExtension ................................   Passed    0.10 sec
      Start  9: CfbReaderTest.RejectsCorruptLinksAndTruncation
 9/12 Test  #9: CfbReaderTest.RejectsCorruptLinksAndTruncation ...................   Passed    0.02 sec
      Start 10: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing
10/12 Test #10: CfbReaderTest.HandlesMutatedHeadersAndDirectoryWithoutCrashing ...   Passed    0.19 sec
      Start 11: CfbReaderTest.ReadsLibreOfficePlainTextCorpus
11/12 Test #11: CfbReaderTest.ReadsLibreOfficePlainTextCorpus ....................   Passed    0.03 sec
      Start 12: CfbReaderTest.BuilderRepackagesLibreOfficeStreams
12/12 Test #12: CfbReaderTest.BuilderRepackagesLibreOfficeStreams ................   Passed    0.03 sec

100% tests passed, 0 tests failed out of 12

Total Test time (real) =   0.49 sec
```

说明：首次 ASan/UBSan 构建在 CMake 的 GoogleTest 自动发现阶段因容器环境下 LeakSanitizer 的 ptrace 限制失败，原始输出已记录上方。设置 `LSAN_OPTIONS=detect_leaks=0` 后，ASan/UBSan 的 12 项测试全部通过。未运行独立 libFuzzer 长时间 fuzz campaign；当前覆盖为 1000 次确定性变异和 sanitizer 测试。

最终计数：passed 12，failed 0，disabled 0。
