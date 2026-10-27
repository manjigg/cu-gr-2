# 最小双 die 合成样例

这是一套手工构造、无需下载工艺库或运行综合/布局工具的开发数据。
用于双 die 读取、跨 die 网表、HBT 资源与子网分解接口的开发；不是论文中的真实设计或工艺。

## 文件

| 文件 | 内容 |
| --- | --- |
| `technology.lef` | 3 个金属层、2 类普通过孔、1 个 BUF 单元及引脚/障碍几何 |
| `top.def` | Top die 的 4 个 BUF、1 个外部端口、5 个线网片段 |
| `bottom.def` | Bottom die 的 4 个 BUF、1 个外部端口、4 个线网片段 |
| `hbt.json` | HBT 阵列和坐标约定，供后续 H3D 接口使用 |
| `nets.json` | 明确的跨 die 连接清单及单 die 网名 |

## 单位与坐标

- 1 微米 = 2000 DBU，两个 die 范围都是 `(0,0)-(25200,25200)`，即 12.6 × 12.6 微米。
- 两份 DEF 已使用同一个键合平面坐标系，Top 不需再次镜像。
- GCell 间距为 4200 DBU（2.1 微米），每个 die 是 6 × 6 × 3 网格。
- HBT pitch 为 6000 DBU（3 微米），从 `(3000,3000)` 开始，两个方向各 4 个位置。
- 站点 `(i,j)` 的位置为 `origin + (i,j) * pitch`；`blocked_sites` 使用从 0 开始的站点索引。
- 共 16 个站点，预留 2 个，因此可用站点为 14 个。pad 尺寸是合成设定，不来自论文工艺。

## 连接关系

共有 **6 个原始线网：3 个跨 die 网、3 个单 die 网**。
两个 DEF 中同名的 `cross_a/cross_b/cross_c` 分别属于同一个原始线网。
两个 die 故意复用 `U0` 等实例名，以检验程序是否正确携带 die 身份。
`cross_c` 在两边各只有一个引脚，双 die 读取后才形成两端网。
这些跨 die 片段没有预先插入虚拟 HBT 端口，位置由后续算法决定。

## 当前验证命令

先完成项目构建：

```bash
python3 scripts/build.py -o release
python3 tests/h3d/check_fixture.py
```

检查脚本自动使用 `flute/` 中的查找表，在独立进程中分别路由两个 die，验证读取统计、guide 网名与矩形边界，
并将结果和日志存到 `build/h3d_smoke/`。单 die 路由不包含跨 die 连接，不能作为完整 H3D 的结果。
`hbt.json` 是 H3D 模式的输入配置；`nets.json` 是供人工查阅和测试使用的连接清单。

完整 H3D 流程：

```bash
python3 tests/h3d/check_end_to_end.py
```

该检查生成实际 HBT 终端、分解后的网表和对应 DEF/guide，并独立验证经 HBT 拼接后的连通性。
基准结果为 3 条跨 die 网使用 4 个合法 HBT，Top 6 条、Bottom 4 条子网，两边均无溢出。
更多运行方式见 [实现说明](../../../../H3D_IMPLEMENTATION.md)。

## 后续真实设计数据入口

已核对 OpenROAD 的全局布线测试使用下列现成数据：

- [GCD 测试脚本](https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/grt/test/gcd.tcl)
- [已布局的 gcd.def](https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/grt/test/gcd.def)
- [配套 Nangate45.lef](https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/test/Nangate45/Nangate45.lef)

可以在保留单元坐标、朝向和完整原始连接关系的基础上，将实例分配给两个 die，
再生成两份 DEF、跨 die 网表和 HBT 配置。原 2D 布局没有单元重叠，按实例子集拆分也不会新增同 die 单元重叠。
这种生成数据适合扩大开发测试规模，但它不是论文所用的三维布局结果。
拆分策略、外部端口归属、跨 die 网比例需要明确记录；不能把复制两份完整 DEF 当成实例拆分。
该公开 LEF 有 10 个布线层，而论文实验每 die 使用 6 层，后续对照实验需要明确层范围和配置差异。

论文第 IV-A 节的数据生成流程为：公开 RTL → Yosys 综合/Nangate45 → 三维布局器 [39][40]
→ Top/Bottom DEF → H3D → TritonRoute 详细布线。
严格复现论文应向作者索取最终布局输入及实验配置，或按所述流程重新生成。
