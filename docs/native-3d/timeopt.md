# Native CUGR-3D 性能剖析与优化

冻结起点为 `f3498c305d8656d39f4e182d29c17fda14568338`，与 `native-3d-f2f` 的分支头一致。开发分支 `timeopt_native-3d-f2f` 起始工作区干净。原实验目录保留。所有基线重新运行使用仓库内 `tests/native/benchmark-array.json`，逐文件验证 LEF/DEF 的 SHA-256。

## M0 性能基线

同一服务器使用 GCC 9.4、CMake Release、单线程、初解 1200 s、各候选阶段和主问题分别 15 s。先跑 aes、ethmac、ariane、bp，随后运行另外三个。使用冻结的原版二进制重新运行得到 V0，SHA-256 为 `dddc3d7cce3c2a4ede25ce0426f1516c039be7be89e95bc11441df835d988e7e`。计时版执行同一算法并写出嵌套计时，用于定位热点。GNU time 的进程耗时用于正式比较，避免将计时版的探针开销计为原版算法开销。

原 `routing_seconds` 从 `GlobalRouter::route` 创建并检查输出目录后开始，到初解、三阶段候选与 Guide 输出完成后结束，包含 RoutingGraph 构建、FLUTE LUT 初始化及解统计。LEF/DEF 导入、MultiDieDesign 内部的两侧 GridGraph 构建、统计 JSON 最终写盘及对象析构属于进程时间。新增 `Profile::Total` 从原生输入前覆盖到 `GlobalRouter::route` 返回和设计销毁。两个口径均保留。

每个 `Measured` 用 steady_clock 测量整段时间，退出时从该层耗时减去直接子作用域的测量值作为独占耗时。只相加独占时间。逐网表用同样的嵌套扣除，保留慢网 ID、两侧网名、真实 Pin 数、封装 DBU 包围盒、最终 HBT 数和重试次数。当前 DP 峰值记录单个活跃状态桶的最大标签数。

| AES 模块 | inclusive s | exclusive s |
|---|---:|---:|
| HBT 搜索 | 29.894 | 16.227 |
| 资源摘要 | 15.852 | 15.852 |
| Pattern DP | 48.809 | 11.765 |
| 资源可用性检查 | 26.399 | 7.816 |
| 路由验证 | 7.768 | 4.911 |
| 静态容量查询 | 4.301 | 4.301 |
| 主问题 | 18.695 | 3.509 |
| Commit | 11.496 | 2.736 |
| Release | 2.249 | 2.249 |
| 初始解 | 1.446 | 0.046 |

AES 在计时版中累计做了 1,874,469 次资源摘要计算、88,320,078 次容量查询和 173,445 次提交。最慢网 `_00002_` 仅有两个真实 Pin，却耗时 32.926 s。Stage 1 和 Stage 2 都在这根网内超时，分别只尝试了极少 DP 合并。该证据支持优先降低静态资源查询和重复提交成本，再避免按网 ID 从头耗尽阶段预算。[AES 逐网剖析](../../result-3/timeopt/_profiles/baseline/aes/slow_nets.tsv)

## 复现基线

```bash
cmake -S src -B build-timeopt -DCMAKE_BUILD_TYPE=Release -DCUGR_NATIVE_3D=ON
cmake --build build-timeopt -j8
ctest --test-dir build-timeopt --output-on-failure
python3 scripts/native_bench.py build-timeopt/route tests/native/benchmark-array.json /tmp/native-profile-run --seed-seconds 1200 --stage-seconds 15 --master-seconds 15 --threads 1 --profile
python3 scripts/native_profile.py /tmp/native-profile-run /tmp/native-profile-tables
```
