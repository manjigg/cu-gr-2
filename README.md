CUGR 2.0
======================================
CUGR 2.0, the successor of [CUGR](https://github.com/cuhk-eda/cu-gr), is a VLSI global routing tool developed by the research team supervised by Prof. Evangeline F. Y. Young at The Chinese University of Hong Kong (CUHK).
CUGR 2.0 is a detailed routability-driven global router and its solution quality is solely determined by the final detailed routing results.

Please read our paper for more technical details:

* Jinwei Liu, Evangeline F. Y. Young,
EDGE: Efficient DAG-Based Global Routing Engine,
ACM/IEEE Design Automation Conference (DAC), San Francisco, CA, USA, July 9-13, 2023.

(CUGR 2.0 supports ICCAD'19 benchmarks ([v2](http://iccad-contest.org/2019/Problem_C/iccad19_benchmarks_v2.tar.gz), [hidden](http://iccad-contest.org/2019/Problem_C/iccad19_hidden_benchmarks.tar.gz)).
This version of code is consistent with the DAC paper.)

## 中文代码导览

从 [CODE_GUIDE.md](CODE_GUIDE.md) 按实际执行顺序学习；遇到变量查 [VARIABLE_REFERENCE.md](VARIABLE_REFERENCE.md)，查询完整语法、逐文件职责和算法手算看 [CODE_REFERENCE.md](CODE_REFERENCE.md)。源码中的成员说明、函数变量表与步骤注释解释单位、索引、输入输出和逻辑。

## H3D 双 die 路由扩展

本分支实现了 H3D 论文中的核心流程：双 die 输入、固定拓扑 HBT 数量 DP、
异构网格上的 HBT 位置 DP、同网 HBT 合并、子网分解，以及 CUGR 模式/迷宫布线和双 DEF/guide 输出。
**源规格修正后的完整结果见 [run_007/REPORT.md](eval/h3d_aes/run_007/REPORT.md)**：
从真实 placer `.input` 恢复 1 µm 终端、1 µm 间隔，生成 2 µm 阵列；八方向、单次数量回溯完成全部 14310 网，两 die 溢出为 0。
修正输入独立保存，旧 3 µm 数据及其失败结果未覆盖；这不是旧 3 µm 固定条件下的成功。
针对 AES 失败的单变量根因定位见 [run_006/REPORT.md](eval/h3d_aes/run_006/REPORT.md)：
取消八方向裁剪仍然耗尽，根因是逐网局部最优分配提前消耗 HBT；原 3 µm 配置仍未按单次论文策略全量完成。
论文算法的 2 µm 间距对照已完整通过，它不代表原 3 µm 条件已修好。
运行方法、算法对应、配置约定和验证范围见 [H3D_IMPLEMENTATION.md](H3D_IMPLEMENTATION.md)。
默认 `-h3d-method paper -h3d-seed 0`，采用论文的连续区域、八方向候选和公式 (7) DP。
全域精确位置优化、未来站点预算和强制单站点后备通过 `-h3d-method enhanced` 显式启用。
默认在位置等成本解中优先合并同网站点；`-h3d-paper-ties first` 可恢复原始首选平局规则。
`-h3d-assignment-trials N` 可显式比较多个数量 DP 等价回溯（默认 1），每个回溯仍调用论文位置算法。
最新改进、取舍和真实 AES 结果见 [run_005/REPORT.md](eval/h3d_aes/run_005/REPORT.md)；
原始平局规则的方法审计保留在 [run_003/REPORT.md](eval/h3d_aes/run_003/REPORT.md)。
仓库内已有无需外部数据的小样例及端到端检查：

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DCUGR_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## 1. How to Build

**Step 1:** Download the source code. For example,
```bash
$ git clone https://github.com/cuhk-eda/cu-gr-2
```

**Step 2:** Go to the project root and build by
```bash
$ cd cu-gr-2
$ scripts/build.py -o release
```

Note that this will generate two folders under the root, `build` and `run` (`build` contains intermediate files for build/compilation, while `run` contains binaries and auxiliary files).
More details are in [`scripts/build.py`](scripts/build.py).

### 1.1. Dependencies

* [GCC](https://gcc.gnu.org/) (version >= 5.5.0) or other working c++ compliers
* [CMake](https://cmake.org/) (version >= 2.8)
* [Boost](https://www.boost.org/) (version >= 1.58)
* [Python](https://www.python.org/) (version 3, optional, for utility scripts)
* [Innovus®](https://www.cadence.com/content/cadence-www/global/en_US/home/tools/digital-design-and-signoff/soc-implementation-and-floorplanning/innovus-implementation-system.html) (version 18.1, optional, for design rule checking and evaluation)
* [Rsyn](https://github.com/RsynTeam/rsyn-x) (a trimmed version is used, already added under folder `rsyn`)
* [Dr. CU](https://github.com/cuhk-eda/dr-cu) (v4.1.1, optional, official detailed router for ICCAD'19 Contest, [binary](http://iccad-contest.org/2019/Problem_C/drcu_june19.zip) is already included under the root)

## 2. License

READ THIS LICENSE AGREEMENT CAREFULLY BEFORE USING THIS PRODUCT. BY USING THIS PRODUCT YOU INDICATE YOUR ACCEPTANCE OF THE TERMS OF THE FOLLOWING AGREEMENT. THESE TERMS APPLY TO YOU AND ANY SUBSEQUENT LICENSEE OF THIS PRODUCT.



License Agreement for CUGR 2.0



Copyright (c) 2023 by The Chinese University of Hong Kong



All rights reserved



CU-SD LICENSE (adapted from the original BSD license) Redistribution of the any code, with or without modification, are permitted provided that the conditions below are met.



Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.



Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.



Neither the name nor trademark of the copyright holder or the author may be used to endorse or promote products derived from this software without specific prior written permission.



Users are entirely responsible, to the exclusion of the author, for compliance with (a) regulations set by owners or administrators of employed equipment, (b) licensing terms of any other software, and (c) local, national, and international regulations regarding use, including those regarding import, export, and use of encryption software.
