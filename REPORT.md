# M7 发布工程批 战报

## 0. 基线

- 开工 HEAD：`f24ad429181477dea5b27a423bd907192f7afcb1`
  （2026-11-01，`docs: record the r51 degradation-boundary tightening`）
- 开工 `git status --porcelain`：空。
- 外部语料 `/home/ubuntu/doccraft-corpus` 全程只读，未修改。
- 本批未改 `third_party/`、未 push。

> 与任务简报的数字出入（按仓库实际证据修正，未按简报写）：
> - ctest 不是 547，开工实测 **585**（`ctest -N` 与全量运行一致）。
> - 黄金门不是 35/36 份，仓库现有非加密 `.doc` 为 **40** 份
>   （12 corpus + 31 fixtures − 3 encrypted）。
> - 100 MiB 不是 3.99 s：`tests/perf/baseline.json` 记录 **6.367 s / 996.2 MiB**
>   （regression host，本批实测 6.943 s / 944.7 MiB）；
>   `docs/perf-baseline.md` 的 **3.913 s / 990.4 MiB** 是更快的 reference host。
>   README 同时标注两台机器，不把 3.9 s 当作当前唯一的实测值。
> - 二进制 1,394,400 bytes = **1.33 MiB**，README 写 "1.3 MiB" 并给出字节数。

## 1. 交付总览

| 块 | commit | 内容 |
|---|---|---|
| 0 | （基线） | `f24ad42`，工作区干净 |
| 3a | `e159d66` | `poi_corpus_smoke` 支持 `DOCRAFT_CORPUS_DIR` |
| 3b | `9bd6145` | CI：ASan、corpus 预留、perf smoke、README 链接检查 + `docs/CI.md` |
| 3c | `71846fc` | `docs/CI.md` 措辞与并发覆盖说明 |
| 1 | `d91b5b3` | README 重写：定位、竞品实测、六平台、质量工程、性能/像素数字、Known limits |
| 1b | `929e0a8` | README 像素基线样本数勘误（37/40，未测 3 个 fixture） |
| 2 | `aa3723f` | 质量巡检小修：list sprm 命名、错误消息小写、头文件 include 最小化 |
| 4 | （本文件） | 巡检四张清单 + 原始验证输出 |

## 2. 块1：README 项目叙事

重写后的 `README.md` 结构：

1. 开头定位：`A fast, dependency-free .doc → .docx converter with a clean-room
   [MS-CFB]/[MS-DOC] parser`，紧接可核验的二进制大小、5/100 MiB 时间。
2. **Why doccraft**：同一份 4.75 MiB 结构化 DOC 上 doccraft / LibreOffice /
   antiword / wvWare 的实测表（墙钟、峰值 RSS、安装占用、输出类型）。
   在线服务不编数字，明确写“需要上传、未在本机基准内”。
3. Quick start / What it handles。
4. **Platform support**：Linux、macOS、Windows CLI；Android、iOS、WebAssembly
   走 C API `doccraft_convert_buffer`，逐项标注“CI 已验证 / 工具链未在此运行”。
5. **Quality engineering**：585 ctest、680 语料（596 干净 + 29 带警告 = 91.9%，
   55 正确拒绝，0 崩溃）、40 样本 part 级 SHA-256 黄金门、ASan、pixelcompare。
6. **Measured performance**：`tests/perf/baseline.json` 五档表；脚注说明
   reference host 的 3.913 s 与 regression host 的 6.367 s 是两台机器。
7. Visual fidelity / gallery / Architecture / Known limits / License。

数字来源与交叉核对：

| README 数字 | 来源 | 核对结果 |
|---|---|---|
| 1,394,400 bytes | `stat build/src/cli/doccraft` | 1.33 MiB |
| 5 MiB 0.288 s / 54.0 MiB | `tests/perf/baseline.json` | 5,244,928 B，0.28835 s，55,340 KB |
| 100 MiB 6.367 s / 996.2 MiB | `tests/perf/baseline.json` | 104,845,312 B，6.366793 s，1,020,080 KB |
| 100 MiB 3.913 s / 990.4 MiB | `docs/perf-baseline.md` | reference host 记录 |
| 585 ctest | `ctest -N` / 全量运行 | 585/585 |
| 680 / 596 / 29 / 55 | `docs/KNOWN-SIMPLIFICATIONS.md` r51 记录 | 596+29=625，625/680=91.9% |
| 40 黄金样本 | `12 corpus + 28 非加密 fixtures` | 本批实际枚举 |
| 37 像素样本 32/4/1 | `docs/pixel-baseline.tsv` | `awk` 计数一致 |
| 14 个 Windows 代码页 | `src/base/charset.cc` `IsWindowsCodepage` | 874/932/936/949/950/1250–1258 |

竞品实测（同一 4,975,616 B DOC，3 次，`/usr/bin/time`，取中位数）：

```text
doccraft     median 0.290 s  peak RSS 52,516 KB
libreoffice  median 5.540 s  peak RSS 221,672 KB
antiword     median 0.140 s  peak RSS 2,688 KB   (text only)
wvText       median 0.540 s  peak RSS 11,392 KB  (text only)
```

```text
$ du -sh /usr/lib/libreoffice
256M    /usr/lib/libreoffice
$ dpkg-query -W -f='${Installed-Size}\t${Package}\n' antiword wv
589     antiword
732     wv
```

已修正的过时数字：README 原性能表（0.037/0.180/0.905/1.111/3.990 s，
1287.9 MiB）、`100 MiB ≈ 1.3 GiB RSS`、`15 code-page decoders`（实为 14）。
`docs/perf-baseline.md` 末尾原称“表与 baseline.json 是同一次运行”，已改为
两台机器的分别记录。

## 3. 块2：代码质量巡检

### 3.1 超长文件审计

命令：

```text
$ find src -name "*.cc" | xargs wc -l | sort -rn | head -11
 20171 total
  8427 src/ooxml/generated/wml_schema_data.cc   # 生成物，排除
  1553 src/doc/format/sprm.cc
  1141 src/doc/format/fkp.cc
  1030 src/doc/format/formatted.cc
   949 src/cfb/cfb_reader.cc
   755 src/doc/format/pictures.cc
   572 src/doc/format/headers.cc
   533 src/doc/format/styles.cc
   467 src/docx/writer.cc
   450 src/cli/main.cc
```

>800 行的手工文件（4 个）职责与拆分评估：

| 文件 | 行数 | 职责 | 拆分判断 |
|---|---:|---|---|
| `src/doc/format/sprm.cc` | 1553 | 31–86 `LanguageTag`/`Ico`/`Toggle`；87–158 operand 读取；159–711 表格/行/单元格属性应用；717–968 `ApplyRun`；969–1252 `ApplyPara`；1253–1440 `ApplySection`；1441–1552 样式条件格式 | 表格族（约 550 行）理论上可整体搬到 `sprm_table.cc`，但需要暴露 operand 读取器并改动分派入口；属大重构，本批只登记 |
| `src/doc/format/fkp.cc` | 1141 | 34–325 `ReadChars`；326–577 Word 6/95 sprm 重建助手；578–1022 `ScanLegacySprms`/`NormalizeLegacySprms`/`ReadLegacyFkps`；1023–1130 `ReadFkps`；1131–1140 `Find` | legacy 路径（约 700 行）可拆到 `fkp_legacy.cc`，但共用 `Put*`/BRC 助手；属大重构，本批只登记 |
| `src/doc/format/formatted.cc` | 1030 | 72–131 `Prm0Opcode` 表；133–1029 `ParseFormatted` 主遍历（正文/域/表格/注释/文本框/节） | 单一状态机，语义强耦合；不建议拆 |
| `src/cfb/cfb_reader.cc` | 949 | Header/FAT/DIFAT 解析、目录名/条目、`ReadChain`、`Open`、`Walk`、`ReadStream` | 共享解析状态，拆分收益低；不建议拆 |

结论：本批不拆任何 >800 行文件。任务边界要求“只拆机械可拆的、大改另立任务”，
四个文件都需要改头文件与分派边界，不属于 ≤5 行小修。已登记为后续独立重构。

### 3.2 死代码扫描

- `gcc -Wunused-function -Werror` 全量构建通过；匿名命名空间/`static` 函数无未引用告警。
- 交叉检查 `src/doc/format/sprm_codes.h` 的 282 个 `kSprm*` 常量：**16 个**在
  `sprm.cc` 及所有 `.cc/.h` 中无引用（`docs/` 的 SPRM 台账除外）：

```text
kSprmCFMathPr        kSprmCHresi          kSprmCIstdPermute
kSprmCMajority       kSprmCPbiGrf         kSprmCPbiIBullet
kSprmPFNoAllowOverlap kSprmPFOpenTch      kSprmPIpgp
kSprmPIstdPermute    kSprmPTIstdInfo      kSprmSDmPaperReq
kSprmTCellBrcType    kSprmTIpgp           kSprmTWidthAfter
kSprmTWidthBefore
```

处理：**全部保留**。逐项核对 `docs/SPRM-COVERAGE.md`，16 个都是 `N/A` 行
（规范允许忽略、OOXML 无对应语义、编辑器期置换表等），常量存在是为了
“一行一个 opcode”的台账可追溯，不是可删死代码。没有确认无引用的函数可删。

### 3.3 TODO/FIXME/HACK 清点

```text
$ grep -rn "TODO\|FIXME\|HACK\|XXX" src/ | wc -l
0
```

结论：零条目，无需挂 issue；没有需要升级为 KNOWN-SIMPLIFICATIONS 的临时标记。
已知缺口都由 `docs/KNOWN-SIMPLIFICATIONS.md` 的正式条目承载。

### 3.4 头文件卫生（抽查 5 个）

| 头文件 | 检查结果 | 处理 |
|---|---|---|
| `src/cfb/cfb_reader.h` | `<concepts>`/`<functional>`/`<utility>`/`<span>` 等 9 个均被使用（`std::invocable`、`std::function`、`std::forward`） | 无需改 |
| `src/docx/writer.h` | `<cstddef>`、`<span>` 未使用；`namespace` 前缺空行 | 已删 2 个 include，补空行 |
| `src/model/document.h` | `<cstddef>`（裸 `size_t`）、`<utility>`（`std::pair`）等均被使用 | 无需改 |
| `src/doc/format/internal.h` | `<algorithm>`（`std::clamp`）、`<map>`、`<array>` 等均被使用 | 无需改 |
| `src/api/doccraft_api.h` | C99 自包含，仅 `<stddef.h>`/`<stdint.h>` | 无需改 |

附带发现：`src/cli/main.cc` 使用 `std::span` 但依赖 `writer.h` 的传递包含；
已在 `main.cc` 直接补 `<span>`。include 分组顺序（本文件 → C 系统 → C++ 标准 →
third_party → 项目）在 5 个头文件中均符合 AGENTS.md。

### 3.5 魔数扫描（`src/doc/format/`，排除 0/1/2/-1 与 `internal.h` 已定义）

原始扫描 111 行候选；剔除密集一对一数据表（AGENTS §3 允许裸值）后，登记 7 簇
共 38 处：

| 簇 | 位置 | 数字 | 说明 |
|---|---|---|---|
| FKP 页大小 | `fkp.cc` 975/977/983/985/994/997/1035/1037/1046/1047/1057/1066/1071/1079 | `512`、`511`（14 处） | `[MS-DOC]` FKP 页 512 字节、末字节 511；建议命名 `kFkpPageSize` |
| BRC6 位域 | `fkp.cc` 416–418 | `0x1F`、`0x07`、`0x03` | ico/width/type 掩码，建议命名 |
| CLX Prc 上限 | `fkp.cc` 177 | `0x3FA2` | 行内已引 [MS-DOC] §2.9.210，优先级低 |
| 域字符 | `headers.cc` 178/183 | `0x13`/`0x14`/`0x15` | Fld.fldch begin/sep/end；建议命名 |
| Prm0 / 域标志 | `formatted.cc` 206/476/477 | `0x7F`、`0x10`、`0x04` | `headers.cc` 已有 `kFldLocked`/`kFldResultsDirty`，未共享 |
| 列表结构 | `lists.cc` 32/44/55/59/85 | `1024`、`28`、`24`/`25`、`0x0fff`、`8`、`0x17` | LSTF/LVLF 尺寸、哨兵、格式码 |
| 样式结构 | `styles.cc` 244/261/407/442 | `4096`、`0x0fff`、`8` | STSH 计数上限与 `stiNil` 哨兵 |

明确豁免的数据表（AGENTS §3）：`sprm.cc` 34–79 的 LCID 标签与 Ico 调色板、
`formatted.cc` 74–126 的 128 项 Prm0 表、`headers.cc` 49 的 6 个故事位、
`styles.cc` 321–322 的 12 项 CNF 优先级表。本批未改这些数字（避免语义变更）。

### 3.6 错误消息一致性

规则：小写开头、无句号结尾。扫描 `Error{...}`/`std::unexpected`/warning 字符串：

- **修了 1 处**：`src/docx/writer.cc:53`
  `Unknown relationship target: ...` → `unknown relationship target: ...`；
  `tests/docx_writer_test.cc:394` 同步更新。
- 修后仍有 **50 个**字面量以大写开头，全部是专有名词/缩写：
  `Word` 19、`PICF` 6、`OLE` 5、`Escher` 3、`CLX` 3、`XOR` 2、`ZIP` 2、
  `SEPX` 2，以及 `ANSI`/`UTF-16`/`BTE`/`FKP`/`PAPX`/`CHPX`/`SttbfRMark`/`FAT`
  各 1。逐个检查后确认它们应保留大写（`Word 6/95`、`ZIP`、`XML` 等不能改成
  小写）；`fkp.cc:1116` 的 `PAPX`/`CHPX` 是格式参数而非消息开头。
- **0 个**消息以句号结尾。

### 3.7 本批实际改动的代码

```text
$ git show --stat aa3723f
 src/cli/main.cc             | 1 +
 src/doc/format/lists.cc     | 4 ++--
 src/docx/writer.cc          | 2 +-
 src/docx/writer.h           | 3 +--
 tests/docx_writer_test.cc   | 2 +-
 5 files changed, 6 insertions(+), 6 deletions(-)
```

- `lists.cc`：`0x845E`/`0x8460` → `kSprmPDxaLeft`/`kSprmPDxaLeft1`（纯命名）。
- `writer.cc` + 测试：错误消息小写（纯措辞）。
- `writer.h`/`main.cc`：include 最小化（`<cstddef>`/`<span>` 移除，`main.cc` 补 `<span>`）。

均为非语义小修；40 样本黄金门前后逐 part 相同（见 §5.4）。

## 4. 块3：CI 补充

### 4.1 `ci.yml` 新 job

| job | 触发 | 关键设置 | 作用 |
|---|---|---|---|
| `build-test-format` | push / PR / manual | `timeout-minutes: 40`，concurrency `...-build` | 原 job，补超时与并发组；ctest 加 `--timeout 300` |
| `asan` | push / PR / manual | `timeout-minutes: 60`，concurrency `...-asan`，job env `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` | Debug + `-fsanitize=address -fno-omit-frame-pointer`，全量 ctest |
| `corpus` | 预留 | `if: false`，concurrency `...-corpus`，`DOCRAFT_CORPUS_DIR: ${{ vars.DOCRAFT_CORPUS_DIR }}` | 外部语料不常驻 runner；注释与 `docs/CI.md` 写明启用三步 |
| `perf-smoke` | `workflow_dispatch` | `timeout-minutes: 30`，concurrency `...-perf` | 只生成 5 MiB，5 次中位数，对比 `baseline.json["5"]`，>30% 只 `::warning::`，不失败 |
| `docs-links` | push / PR / manual | `timeout-minutes: 5`，concurrency `...-docs` | `scripts/check_readme_links.py` 检查 README 相对链接/图片存在性 |

`build-matrix.yml` 的两个 job 补了 `concurrency`（`cancel-in-progress: false`，
避免丢平台结果）；`pages.yml` 的 job 补了 `timeout-minutes: 15`。

### 4.2 corpus 机制

- 语料不在仓库：`tests/poi_corpus_smoke.py` 从 `DOCRAFT_CORPUS_DIR`（默认
  `/home/ubuntu/doccraft-corpus/poi-test-data`）glob `*.doc`。
- 目录不存在时打印 `POI corpus not present - skipping (...)` 并 `exit 0`；
  `ctest` 因此干净通过。验证：

```text
$ DOCRAFT_CORPUS_DIR=/tmp/does-not-exist \
    python3 tests/poi_corpus_smoke.py build/src/cli/doccraft
POI corpus not present - skipping (download to /tmp/does-not-exist)
$ echo $?
0
```

- 有语料时断言语料 ≥100 文件、成功 ≥100、零崩溃/超时。
- 启用 hosted runner：需另加下载步骤；self-hosted runner：设置
  `DOCRAFT_CORPUS_DIR`，把 `if: false` 换成
  `if: ${{ vars.DOCRAFT_CORPUS_DIR != '' }}`。

### 4.3 ASan 本地验证

CI 的 `asan` job 按同样参数在本地跑通（`build-asan-ci`，未入库）：

```text
$ ASAN_OPTIONS=detect_leaks=0 cmake --build build-asan-ci -j
...
[100%] Built target api_cli_fixture
asan_build_exit=0

$ ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
    ctest --test-dir build-asan-ci --output-on-failure -j4
...
585/585 Test #232: ConverterSchemaTest.SamplesMatchTheWordprocessingMlSchema ... Passed 79.50 sec

100% tests passed, 0 tests failed out of 585
```

注意：`gtest_discover_tests` 在构建期会运行测试二进制，因此 `ASAN_OPTIONS`
必须覆盖 build 步骤；CI 的 job 级 `env` 已覆盖，`docs/CI.md` 本地命令也已补上。

### 4.4 YAML 自检

```text
$ command -v actionlint
（无输出，未安装 → 按任务约定跳过）

$ python3 -c "import yaml; yaml.safe_load(open(...))"
YAML OK .github/workflows/ci.yml
YAML OK .github/workflows/build-matrix.yml
YAML OK .github/workflows/pages.yml
```

### 4.5 文档链接检查

```text
$ python3 scripts/check_readme_links.py
README relative links: all targets exist
$ echo $?
0
```

## 5. 验证（原始输出）

### 5.1 构建

```text
$ cmake --build build -j
...
[100%] Built target api_cli_fixture
build_exit=0
```

### 5.2 全量 ctest

```text
$ ctest --test-dir build --output-on-failure -j4
...
585/585 Test #95: PieceTableTest.ReadsCompressedAndUtf16Pieces ... Passed 0.00 sec

100% tests passed, 0 tests failed out of 585
Total Test time (real) =   8.15 sec
```

### 5.3 format-check

```text
$ cmake --build build --target format-check
[100%] clang-format: verifying Google style formatting
[100%] Built target format-check
format_exit=0
```

### 5.4 黄金门

方法：开工时（`f24ad42`，`src/` 未改）对 12 corpus + 28 非加密 fixtures
（共 40 份）各转换两次，逐 ZIP part 计 SHA-256；代码改动后同一二进制再跑一次，
比较 manifest。

```text
# 开工快照（HEAD f24ad42）
baseline samples: 40
aggregate part-manifest sha256:
f57149112e2928e2a88e7396e6fa0ae995f492ebe410b36c8aa488091e6710b7

# 改动后
baseline samples: 40
aggregate part-manifest sha256:
f57149112e2928e2a88e7396e6fa0ae995f492ebe410b36c8aa488091e6710b7

$ python3 /tmp/golden_gate.py compare before.json after.json
IDENTICAL: 40 samples, manifest f57149112e2928e2a88e7396e6fa0ae995f492ebe410b36c8aa488091e6710b7
```

两次转换的 part hash 全部一致（确定性 40/40）；输入 `.doc` 无改动
（`git status` 未出现 corpus/fixtures）。

### 5.5 性能冒烟脚本

```text
$ python3 scripts/perf_smoke.py build/src/cli/doccraft \
    build/perf/structured-5mb.doc --baseline tests/perf/baseline.json --size 5 --runs 5
median: 0.290 s over 5 runs
baseline: 0.288 s (+0.5%)
exit=0
```

### 5.6 最终仓库状态

```text
$ git log --oneline -5
（见 §1 的 commit 表；本文件为最后一个提交）

$ git status --porcelain
（提交后为空）
```

## 6. 登记但未处理（后续任务）

1. **大文件拆分**：`sprm.cc` 表格族 → `sprm_table.cc`；`fkp.cc` legacy 路径 →
   `fkp_legacy.cc`。都需要移动共享助手并改分派，单独一批做，配 40 样本黄金门。
2. **魔数命名**：FKP `512/511`、BRC6 掩码、`Fld.fldch` 0x13–0x15、LSTF/LVLF
   尺寸与哨兵、STSH `0x0fff`/4096。建议先加 `internal.h` 常量再逐文件替换，
   不改变任何分支。
3. **16 个 N/A sprm 常量**：保留为台账锚点；若未来引入“台账驱动检查”，可在
   `sprm_codes.h` 给它们加 `// N/A:` 注释，当前不动。
4. **像素基线缺口**：40 个非加密样本中 37 个有记录，3 个 fixture
   （`basic-shapes`、`comments-range`、`header-textbox`）未测；另 3 个 encrypted
   fixture 不参与像素基线。补测后更新 `docs/pixel-baseline.tsv` 与
   `PIXEL-BASELINE.md`。
5. **680 全库未在本批重跑**：本批改的是常量命名/错误措辞/include，40 样本
   part 级零差异；README 的 92% 引用 `KNOWN-SIMPLIFICATIONS` r51 的已记录结果。

## 7. 边界与规范

- 未改 `third_party/`、未改外部语料、未 push。
- 未新增第三方依赖；CI 只使用 GitHub 官方 action、apt 包（clang-format、
  libreoffice-writer）与仓库自带 Python 脚本。
- `src/` 改动 4 个文件、5 行增 5 行删（命名/措辞/include），测试 1 行同步更新；
  格式解析与写出行为零变更（黄金门 40/40 manifest 相同）。
- 新增文件：`docs/CI.md`、`scripts/check_readme_links.py`、
  `scripts/perf_smoke.py`；C++ 无新增文件，故无需补 Apache-2.0 文件头。
- 未使用 `actionlint`（未安装），以 `yaml.safe_load` 代替，三个 workflow 均零错误。
