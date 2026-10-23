# M1 CFB 必修项修复战报

## 起点与复现

完整阅读 `/tmp/doccraft-m1-review-out.md` 后，执行 `git pull --ff-only` 和 `git rev-parse --short HEAD`；原始输出：

```text
Already up to date.
b55c1f4
```

三个 PoC 均复现，未对 review 的三项必修结论提出异议。测试使用 `CfbBuilder` 构造 root 兄弟与 257 层 storage，另构造一个约 0.5 MiB 的 v4 容器：128 个 FAT 扇区、一个 DIFAT 扇区、目录流声称 512 MiB。后者在 256 MiB 地址空间限制下稳定触发旧实现的 `std::bad_alloc`，避免依赖主机剩余内存。

### 必修 1：root 兄弟导致 `ReadStream` 异常

回归测试 `RejectsRootSiblingWithoutThrowing` 将 root.left 指向短名流，并将 root.child 置为 `NOSTREAM`（两个四字节字段）。测试用显式 `try/catch (...)` 确认无异常，并要求 `Open` 返回 `kCorruptDirectory`。修复在 `Open` 拒绝 root 左右兄弟，且在 `ReadStream` 的 `substr` 前检查路径长度。

红测命令：

`./build/tests/cfb_reader_test --gtest_filter=CfbReaderTest.RejectsRootSiblingWithoutThrowing`

原始失败输出（从包含三项测试的运行中摘取该项）：

```text
[ RUN      ] CfbReaderTest.RejectsRootSiblingWithoutThrowing
/home/ubuntu/doccraft/tests/cfb_reader_test.cc:219: Failure
Value of: reader.has_value()
  Actual: true
Expected: false
[  FAILED  ] CfbReaderTest.RejectsRootSiblingWithoutThrowing (0 ms)
```

绿测命令：`ctest --test-dir build --output-on-failure`。原始输出：

```text
10/17 Test #10: CfbReaderTest.RejectsRootSiblingWithoutThrowing ..................   Passed    0.00 sec
```

### 必修 2：目录深度带来的任务栈路径复制

回归测试 `RejectsExcessiveStorageDepthWithoutThrowing` 构造 257 层 storage，要求 `kCorruptDirectory` 且不抛异常；`FiveStorageLevelsKeepCorrectPath` 检查合法嵌套路径。修复将 `Task` 中的 parent 字符串改为 entry 索引，在 emit 时沿父索引拼路径，并把目录深度限制为 256。

红测命令：

`./build/tests/cfb_reader_test --gtest_filter='CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing:CfbReaderTest.FiveStorageLevelsKeepCorrectPath'`

原始失败输出（从包含三项测试的运行中摘取）：

```text
[ RUN      ] CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing
/home/ubuntu/doccraft/tests/cfb_reader_test.cc:236: Failure
Value of: reader.has_value()
  Actual: true
Expected: false
[  FAILED  ] CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing (0 ms)
[ RUN      ] CfbReaderTest.FiveStorageLevelsKeepCorrectPath
[       OK ] CfbReaderTest.FiveStorageLevelsKeepCorrectPath (0 ms)
```

绿测命令：`ctest --test-dir build --output-on-failure`。原始输出：

```text
11/17 Test #11: CfbReaderTest.RejectsExcessiveStorageDepthWithoutThrowing ........   Passed    0.00 sec
12/17 Test #12: CfbReaderTest.FiveStorageLevelsKeepCorrectPath ...................   Passed    0.00 sec
```

### 必修 3：FAT 声称大小导致盲目预留

回归测试 `RejectsInflatedAllFatStreamWithoutThrowing` 使用全 FAT v4 布局，要求返回 `kCorruptFat` 或 `kTruncated` 且不抛异常。修复把初始 `reserve` 限为声称大小、实际后备数据大小与 64 KiB 的最小值，后续由 `vector` 随已验证扇区几何增长。

红测命令：

`ulimit -v 262144; ./build/tests/cfb_reader_test --gtest_filter=CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing`

原始输出：

```text
[ RUN      ] CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing
/home/ubuntu/doccraft/tests/cfb_reader_test.cc:272: Failure
Value of: threw
  Actual: true
Expected: false
[  FAILED  ] CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing (1 ms)
```

同一命令绿测原始输出：

```text
[ RUN      ] CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing
[       OK ] CfbReaderTest.RejectsInflatedAllFatStreamWithoutThrowing (1 ms)
[  PASSED  ] 1 test.
```

## 其他处理

三处 `SectorBytes` 的 expected 裸解引用已改为显式错误检查；删除未使用的 `kIoError`、`minor_version()` 及其存储、恒假大小比较；修正 include 字母序。新增 `ReadsEveryStreamInDocCorpus`，逐一 `Open` 五份 `.doc` 语料、`Visit` 并按完整路径读取每条流，核对目录声称的长度。其余建议项 1、5、8 登记在 `docs/KNOWN-SIMPLIFICATIONS.md`。

## 全量验证

Release 命令：`cmake --build build -j 4 && ctest --test-dir build --output-on-failure`。原始结果尾部：

```text
16/17 Test #16: CfbReaderTest.ReadsEveryStreamInDocCorpus ........................   Passed    0.00 sec
17/17 Test #17: CfbReaderTest.BuilderRepackagesLibreOfficeStreams ................   Passed    0.00 sec
100% tests passed, 0 tests failed out of 17
Total Test time (real) =   0.10 sec
```

ASan/UBSan 命令：

`cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`

`ASAN_OPTIONS=detect_leaks=0 cmake --build build-asan -j 4 && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure`

原始结果尾部：

```text
16/17 Test #16: CfbReaderTest.ReadsEveryStreamInDocCorpus ........................   Passed    0.06 sec
17/17 Test #17: CfbReaderTest.BuilderRepackagesLibreOfficeStreams ................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 17
Total Test time (real) =   0.73 sec
```

首次 sanitizer 构建未把 `ASAN_OPTIONS=detect_leaks=0` 传给构建过程，GoogleTest 构建后测试发现阶段触发环境中的 LSan/ptrace 错误。补上该环境变量后构建和 17 项测试通过；这与原 review 战报记录的环境限制一致。
