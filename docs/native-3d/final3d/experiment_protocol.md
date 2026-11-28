# 冻结与复现实验协议

冻结提交为 01723729f51e907797194a1f968b42f472eacf90。基线 CMake Release 独立构建保存在 result-3/final3D/baseline/build。旧版 route 的 SHA-256 为 73771ff93531d51ddd615d93e88c6f01e614fed1a52d6c7e3e164e329269118b。冻结源包、二进制指纹和两个阶段的原始日志保存在同目录。此前历史实验的 route 指纹与这个构建不同，因此使用新跑的 V3 全局数据和详细布线。

协议使用 tests/native/benchmark-array.json 和原始 LEF／DEF。导入核对其中 16 个原始输入指纹。封装 2,000 DBU/µm，HBT 原点为 (3000, 3000) DBU，站点间距为 6,000 DBU，双层对 metal7/metal7。既有跟踪交点通孔容量、固定资源和真实终端映射随 Manifest 留存。正式路由请求 4 线程，每次一个设计，初解时限 1,200 秒，阶段及原联合求解时限各 15 秒，单网 30 ms。由进程读取的实际旧联合优化器工作线程和迷宫路由工作线程单独记录。

正式路由采用 Release、关闭计时插桩。router_reported_seconds 从统一布线图与 FLUTE 初始化开始，包含路由和输出，不包含设计输入导入。墙钟时间覆盖全程序、文件导入和输出。重复计时保留三套独立运行的完整记录，第一套正式输出作为详细布线输入。

## V3 性能基线

| 设计 | 进程墙钟 s | 诊断 Stage 1 s | 诊断 Stage 2 s | 诊断 Stage 3 s |
|---|---:|---:|---:|---:|
| aes | 28.250 | 0.653 | 17.045 | 9.459 |
| tinyRocket | 35.467 | 2.325 | 18.259 | 13.194 |
| ethmac | 30.524 | 2.472 | 17.680 | 8.253 |
| mor1kx | 38.515 | 9.695 | 20.293 | 2.438 |
| jpegencode | 63.484 | 21.562 | 21.525 | 3.713 |
| ariane | 114.497 | 64.477 | 23.584 | 13.083 |
| bp | 129.667 | 64.004 | 30.443 | 11.831 |

插桩版七例报告时间合计约 428.222 秒。阶段包含时间合计为 Stage 1 165.190 秒、Stage 2 148.831 秒、Stage 3 61.972 秒。后者记录的主线程迷宫工作包含 26.743 秒，部分并行工作在线程字段另计。HBT 搜索独占约 103.185 秒、资源可用性查询独占 48.205 秒、Guide 输出独占 47.127 秒、树验证独占 40.377 秒、模式动态规划自身 37.476 秒。模块包含时间可能嵌套，此处的独占时间也按各记录的主线程／工作线程定义解释。计时插桩用于归因，正式性能来自无插桩运行。V3 基线七例全部通过独立树和资源账本回放。

## 运行命令

以下命令均从仓库根目录运行。 本轮正式修正使用 CUGR_FINAL3D_REVISION=r2，保留此前独立试跑。相同初解重放共享已冻结的 Stage 1 快照。重复尝试的接收记录遵循同一输入、二进制和参数指纹。开发中断和失败的旧产物保留在原路径。

```bash
cmake -S src -B build/final3d -DCMAKE_BUILD_TYPE=Release -DCUGR_NATIVE_3D=ON
cmake --build build/final3d -j12
ctest --test-dir build/final3d --output-on-failure

export CUGR_FINAL3D_REVISION=r2
python3 scripts/final3d_experiments.py freeze
python3 scripts/final3d_experiments.py screen
python3 scripts/final3d_experiments.py stage2
python3 scripts/final3d_experiments.py formal
python3 scripts/final3d_experiments.py budget
python3 scripts/final3d_experiments.py profile

python3 scripts/final3d_experiments.py detail-v3
python3 scripts/final3d_experiments.py detail-final
python3 scripts/final3d_summary.py --verify --supplements
MPLCONFIGDIR=/tmp/final3d-mpl /home/wanghh/miniconda3/envs/association/bin/python scripts/final3d_figures.py
```

Detail 驱动指定本机 OpenROAD bazel-nostamp 和原 OpenDB 导出及几何检查目录。两侧各 8 线程，逐设计执行。主详细对比的 RT 取实际详细阶段墙钟，另表保存两侧时间之和。显式 HBT 预算实验使用冻结 V3 验证后的真实跨 Die 网数下界和实际 HBT 数量计算同一绝对预算。时限内仍有超额的完整物理解保留为 BUDGET_UNMET。缺失记录与进行中实验在表格中保留，并以 N/A 表示缺失指标。

## 归档

全局总结、比较、消融、相同初解、预算以及原始日志仅进入 result-3/final3D。逐设计初次正式记录位于 final 子目录。详细布线的源 Guide、工具指纹、派生 DEF、DRC 与联合导体证据进入 result-3/final3Ddetail。已有历史结果目录供审查且无需改写。
