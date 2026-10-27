# Open3DBench AES → 双 Die DEF：转换结果与使用说明

## 这份数据是什么

根据上传的 **Open3DBench/Place-LoL** AES 六个原始文件，重建了未经 HBT 预分配的双 Die 三维全局布线测试输入：`top.def`、`bottom.def`，及跨 Die 网络连接表 `3d_nets.json`。本数据并非 H3D ICCAD 2025 作者的原始七个测例。

### 文件说明

- `top.def`：5958 个上层逻辑单元、其局部网端点；不添加 HBT 虚拟单元。
- `bottom.def`：7252 个下层逻辑单元、全部 388 个外部 I/O 端口（**转换假设：外部 I/O 全归 Bottom Die**），及其局部网端点；不添加 HBT 虚拟单元。
- `tech.lef`、`cells.lef`：来自提供的原始 NanGate45 工艺技术与标准单元 LEF，宏名称未附 `_upper` 或 `_bottom` 后缀。
- `3d_nets.json`：保留 1679 条跨 Die 网络以及两边的真实 `(instance, pin)` 端点。对其中 1492 条，保留预分配 Terminal 的坐标作为**诊断参考**，**不强制采用**；另外 187 条含有外部 I/O，原布局器没有生成 Terminal。
- `hbt_config.json`：建议的实验 HBT 网格参数；不是工艺验证的合法键合端子文件。
- `instance_mapping.json`：`C1`～`C13210` 到真实 DEF 单元名称的对应关系。
- `validation_report.json`：数据完整性和基础结构验证报告。

### 重要限制与注意事项

1. 双 Die DEF **并不能直接送入未改造的 CUGR 2.0**。需要让三维布线器读取两个 DEF、通过相同 Net 名称及 `3d_nets.json` 恢复跨 Die 连通，然后自行决定 HBT，并将 HBT 作为双方子网特殊端点，再做单 Die routing。直接用普通二维布线器读取单份 DEF 不会自动跨 Die 布线。
2. 外部 I/O 被放在 Bottom Die，沿用 Open3DBench 官方转换中的默认处理约定，**不是原始布局器提供的物理分层信息**。因此 1679 条跨 Die 网络是这一假设下的值（1492 条明确原始 Terminal + 187 条含外部 I/O），不要将 1679 与 H3D 论文的 AES 网络数量直接比较。
3. 当前源文件技术 LEF 包含 metal1～metal10；两份新 DEF 的 `TRACKS` 只保留 metal1～metal6，与 H3D 每 Die 6 层的目标相近，但 Tech LEF 尚未裁切到 6 层。正式布线时，需要在程序层面约束可用 routing layers，不能默认为 LEF 全部金属层都可以走。
4. 单元来自三维布局器输出，**大多数单元的 X 不是标准单元 Site 的整数倍**；行坐标 Y 已对齐。已通过网络完整性、引脚存在性等检查，但**尚未在 OpenROAD/CUGR 中验证读取、单元合法化、布线和 DRC**。在进行详细布线前，可能需要单独的 legalization（合法化）。
5. 本次保留原始布局器使用的公共二维投影坐标；没有擅自镜像 Top Die。具体 F2F 的方向变换仍需按后续布线模型校验。
6. 网格起点、间距和 HBT 6 层连接规则都是拟设参数，尚未通过工艺间距、障碍物和可达性校验，不要称其为已合法 HBT 布局。

## 在原服务器上复现

把 `h3d_aes_convert.py` 放在你的 `Place-LoL` 当前目录，执行：

```bash
cd ~/Archived/Open3DBench/Place-LoL
python3 h3d_aes_convert.py --root . --out ./h3d_aes_input
```

脚本从你现有的 `benchmarks/`、`binaries/` 读取数据，输出到 **当前目录下的 `h3d_aes_input/`**，不会修改原始文件。

### 关键验证结果

- 原始 DEF 声明 `COMPONENTS 13478`，实际组件记录为 13210。之前官方转换版 DEF 声明组件数 13210，但实际 14702（多出 1492 HBT）；脚本全部根据实际记录重算。
- 原始 14310 个网络及其全部 51663 个端点都在双 Die 输出的并集中保留。
- 上层 7247 条局部网、下层 8742 条局部网；因为 1679 个跨 Die 网络分别出现在两份 DEF 中，所以总局部网记录数是 15989。
- 标准单元和其 Net 中引用的引脚名称全部通过 LEF 名称匹配检查。

## 下一步研究建议

先让改造版 CUGR 2.0 只解析双 Die DEF + `3d_nets.json`，检查每一个 3D Net 的真实端点；然后逐步添加 HBT 资源图、分配算法和跨 Die 的 routing。**不要**把本结果当作已经通过 OpenROAD routing 的测例。
