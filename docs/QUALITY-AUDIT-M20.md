# M7 代码质量巡检报告（CodeBuddy 额度尽，主控亲征）

日期：2026-10-31。范围：src/ 全量 + CI workflow。

## 1. 超长文件审计（>800 行手工文件，排除生成物）
| 文件 | 行数 | 职责 | 拆分评估 |
|---|---|---|---|
| src/doc/format/sprm.cc | 1553 | sprm dispatch（ApplyPara/ApplyRun/ApplyTable/ApplySection 四族） | **可拆**：四族各自独立文件（sprm_para.cc/sprm_run.cc/sprm_table.cc/sprm_section.cc），机械拆分低风险。下批 |
| src/doc/format/fkp.cc | 1141 | 现代 FKP + Word6/95 Legacy FKP + ChgTabs | **可拆**：Legacy 部分独立 fkp_legacy.cc。下批 |
| src/doc/format/formatted.cc | 1030 | 格式帧主循环 | 耦合较深，暂不动 |
| src/cfb/cfb_reader.cc | 949 | CFB 读取器 | 单一职责，不动 |

## 2. 死代码扫描
- TODO/FIXME/HACK/XXX：**0 处**（src/ 全量 grep）——无技术债注释堆积
- kSprm* 常量表：SPRM-COVERAGE.md 台账管理，未发现孤儿常量（表驱动按台账核对）

## 3. 错误消息风格不一致（29 项大写 vs 194 项小写）
不一致集中于 pictures.cc(13)/fkp.cc(8)/fib.cc(3)/writer.cc(2) 等。
**批替换实验失败**：正则统一会把 "ANSI character..." 改成 "aNSI..."、"ZIP init failed" 改成 "zIP..."（缩写词被误伤），已回退。
处置：登记为**低优先人工任务**（约 16 条需逐条判断缩写词），不阻塞发布。

## 4. 头文件卫生 / 魔数
- 抽查 5 个 .h（sprm.h/internal.h/fkp 相关）include 最小化符合 AGENTS.md
- 魔数：internal.h 集中管理 + 表驱动豁免的纪律保持良好，未发现新违规

## 5. CI 现状（本批未改，已达标）
- ci.yml：build-test-format + asan + corpus + perf-smoke + docs-links 五 job，均有 timeout-minutes + concurrency
- build-matrix.yml：linux-gcc / macos-clang / windows-msvc 三平台矩阵 + library-only 变体
- pages.yml：GitHub Pages 部署
- 三个 yml YAML 解析零错

## 结论
项目健康度良好：零 TODO 债、测试纪律严格、魔数管理规范。可执行改进仅两项（sprm.cc/fkp.cc 机械拆分、16 条错误消息人工统一），均为低优先，不阻塞 v0.9.0 发布。
