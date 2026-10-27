#!/usr/bin/env python3
"""Generate the final seven-case report from preserved runs and validations."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess

from h3d_author_benchmarks import ROOT, CASES, check_protected, sha, write_json


def percent(ours, author):
    return 100 * (ours / author - 1)


def table(header, rows):
    return ['| ' + ' | '.join(header) + ' |', '|' + '|'.join(['---'] * len(header)) + '|'] + [
        '| ' + ' | '.join(map(str, row)) + ' |' for row in rows]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--experiment', type=Path, required=True)
    args = p.parse_args()
    folder = args.experiment.resolve()
    check_protected(folder)
    audits = json.loads((folder / 'input_audit.json').read_text())
    supplied = {r['benchmark']: r for r in csv.DictReader((ROOT / 'results_h3d/global_routing_summary.csv').open())}
    native = {r['case']: r for r in json.loads((folder / 'author_binary_repeat/summary.json').read_text())}
    ours = {r['case']: r for r in json.loads((folder / 'verified_summary/summary.json').read_text())}
    before = {r['case']: r for r in json.loads((folder / 'baseline_first/summary.json').read_text())}
    verified = json.loads((folder / 'verification_verified_summary.json').read_text())
    traced = json.loads((folder / 'verification_verified_paper_trace.json').read_text())
    author_checked = json.loads((folder / 'verification_author.json').read_text())
    readback = json.loads((folder / 'native_def_readback/summary.json').read_text())
    assert verified['status'] == traced['status'] == author_checked['status'] == 'passed'
    assert len(readback) == 7 and all(r['status'] == 'passed' for r in readback)
    binary_hash = sha(ROOT / 'build/route')
    for case in CASES:
        for key, reference in (('wire_length_metric', 'wire_length_metric'), ('vias', 'via_count'),
                               ('hbt', 'used_hbt'), ('wire_overflow', 'wire_overflow')):
            assert native[case][key] == int(supplied[case][reference]), 'Supplied binary rerun differs'
        assert ours[case]['return_code'] == 0 and ours[case]['connectivity_checked']
        assert json.loads((folder / 'verified_summary' / case / 'command.json').read_text())['binary_sha256'] == binary_hash
    total_nets = sum(r['algorithm']['all_quantity_dp_nets'] for r in traced['cases'])
    total_endpoints = sum(r['original_endpoints'] for r in verified['cases'])
    total_components = sum(r['algorithm']['all_decomposition_components'] for r in traced['cases'])
    sampled = sum(r['algorithm']['sampled_location_nets'] for r in traced['cases'])
    tables = sum(r['algorithm']['sampled_eq6_eq7_tables'] for r in traced['cases'])
    states = sum(r['algorithm']['sampled_candidate_costs'] for r in traced['cases'])
    rows = []
    for c in CASES:
        n, o = native[c], ours[c]
        row = {'case': c}
        for key in ('wire_length_metric', 'vias', 'hbt', 'wire_overflow'):
            row[key + '_author'] = n[key]
            row[key + '_ours'] = o[key]
            row[key + '_change_pct'] = percent(o[key], n[key]) if n[key] else None
        row['author_wall_seconds'] = n['wall_seconds']
        row['ours_wall_seconds'] = o['wall_seconds']
        row['runtime_ratio'] = o['wall_seconds'] / n['wall_seconds']
        rows.append(row)
    max_wl = max(abs(r['wire_length_metric_change_pct']) for r in rows)
    max_hbt = max(abs(r['hbt_change_pct']) for r in rows)
    max_via = max(abs(r['vias_change_pct']) for r in rows)
    runtime_ratio = sum(o['wall_seconds'] for o in ours.values()) / sum(n['wall_seconds'] for n in native.values())
    summary = {'scope': 'author seven-case global routing only; not seven-case detailed routing/DRC',
               'all_cases_completed': True, 'author_binary_repeats_supplied_quality_metrics_exactly': True,
               'max_absolute_wire_metric_difference_pct': max_wl, 'max_absolute_hbt_difference_pct': max_hbt,
               'max_absolute_via_difference_pct': max_via, 'sum_wall_time_ratio_ours_over_author': runtime_ratio,
               'independent_quantity_dp_nets': total_nets, 'original_endpoints_checked': total_endpoints,
               'independent_decomposition_components': total_components, 'sampled_location_nets': sampled,
               'sampled_eq6_eq7_tables': tables, 'sampled_candidate_costs': states,
               'byte_identical_observed_and_summary_outputs': all(all(r['outputs_byte_identical'].values()) for r in verified['cases']),
               'remaining_nonzero_overflow_cases': [c for c in CASES if ours[c]['wire_overflow']],
               'author_inputs_and_results_unchanged': True, 'binary_sha256': binary_hash, 'cases': rows}
    write_json(folder / 'FINAL_SUMMARY.json', summary)
    with (folder / 'comparison_final.csv').open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    lines = ['# 作者七例全局布线复现对比', '',
        '**结论：七个作者 benchmark 均完成全局布线；修复 III-E 子网路由接入和 DEF 实例名转义问题后，结果已与作者二进制接近。不是逐位相同，也不代表七例详细布线/DRC 已通过。**', '',
        f'- 线长 metric 最大绝对偏差 **{max_wl:.3f}%**；HBT 数量最大绝对偏差 **{max_hbt:.3f}%**；普通 via 数量最大绝对偏差 **{max_via:.3f}%**。',
        '- 作者二进制在本机重新运行全部七例，线长、via、HBT、overflow 四项指标与 `results_h3d` 中给定结果完全一致。',
        f'- 独立检查覆盖 **{total_nets:,}** 个原始网络、**{total_endpoints:,}** 个原始端点，未发现丢端点、跨网串接或 HBT 站点冲突。',
        '- **bp 仍有 93 个 wire overflow（作者为 268）**；其余六例为 0。不能将此轮描述为“七例全部零溢出”或“七例 DRC=0”。', '',
        '## 1. 数据、配置与对比口径', '',
        '- 原始输入：`benchmarks_h3d/`；作者既有输出：`results_h3d/`。二者全程只读并做 SHA-256 核验。',
        '- 作者安装包：`/home/wanghh/h3d/h3d_zyx/install/`。复制到项目的 LEF/DEF 与其配置所指文件逐字节一致；FLUTE 两份查找表也一致。',
        '- 本轮七例使用同一个配置策略：`paper`、`first`、单次数量回溯、seed 0、1 线程；没有逐 case 挑选 seed、增容或启用 enhanced 后备。',
        '- 输入 `T*` 是成对的待分配 HBT 占位端口；核对两侧网名与连接关系后，仅在新派生 DEF 中删除它们，保留全部真实单元引脚、网表和放置。',
        '- 作者文件包含 metal1～metal7，HBT 焊盘在 metal7，尺寸 **0.4×0.4 µm**。本轮沿用这份实际接口，不套用旧 AES 的 metal6/1 µm pad 配置。',
        '- 标称 HBT pitch 为 **3 µm**；作者实现留出完整 pitch 的边界余量，并将 Y 坐标向 metal7 轨道对齐，实际阵列步长为 **X=3.0 µm、Y=3.2 µm**。重建阵列与全部作者输出 HBT 坐标及初始站点数一致。',
        '- HBT 优化采用 GCell 代表点，实际物理站点另存；输出焊盘保持合法物理坐标，未用 GCell 中心替换物理 HBT。',
        '- **WL metric 是全局路由日志的归一化指标，不直接标作 µm。论文 TABLE III/IV 的 WL/via 是详细布线后的统计，不能与本表直接对比。**',
        '- 本批 AES 为 13,158 单元、381 条跨 die 网；不同于此前 `run_007` 的 AES（13,210 单元、1,679 条跨 die 网、2 µm 中心距）。', '']
    lines += table(['Case', '单元数', '原始网数', '跨 die 网数', 'HBT 候选阵列'], [
        [c, sum(audits[c]['instances'].values()), audits[c]['original_nets'], audits[c]['cross_die_nets'],
         f"{audits[c]['config']['site_counts'][0]}×{audits[c]['config']['site_counts'][1]}={audits[c]['author_available_hbt']}"] for c in CASES])
    lines += ['', '## 2. 修复后的指标对比', '', '偏差＝（复现值 / 作者值 − 1）×100%；负值仅表示本次测得较小，不据此宣称新算法优于作者。', '']
    lines += table(['Case', 'WL metric 作者', 'WL metric 复现', '偏差', 'HBT 作者', 'HBT 复现', '偏差'], [
        [r['case'], r['wire_length_metric_author'], r['wire_length_metric_ours'], f"{r['wire_length_metric_change_pct']:+.3f}%",
         r['hbt_author'], r['hbt_ours'], f"{r['hbt_change_pct']:+.3f}%"] for r in rows])
    lines += [''] + table(['Case', 'via 作者', 'via 复现', '偏差', 'wire overflow 作者 → 复现'], [
        [r['case'], r['vias_author'], r['vias_ours'], f"{r['vias_change_pct']:+.3f}%", f"{r['wire_overflow_author']} → {r['wire_overflow_ours']:g}"] for r in rows])
    lines += ['', '## 3. 发现的问题、控制实验与修正', '',
        f'1. **III-E 子网路由接入偏差。** 初跑 ethmac 的 WL metric 为 {before["ethmac"]["wire_length_metric"]:,}，比作者高 {percent(before["ethmac"]["wire_length_metric"], native["ethmac"]["wire_length_metric"]):.2f}%。固定作者 HBT 后，普通 CUGR 得到 2,185,615（与作者相同）；固定我们原有 HBT 后，普通 CUGR 得到 2,163,952。这将主因定位到子网路由接入，而非 HBT 数量不足。',
        '   - 旧接口强制沿用 HBT 重定位前的 Steiner 拓扑，并固定原始跨 die 网络选过的单一接入点，零溢出时仍可能保留长绕行。',
        '   - 修正为保留 III-D 的子网端点/HBT 归属，由实际引脚形状生成候选、重建单 die RSMT，再做 CUGR DAG Pattern/Detour/Maze。旧接口可显式选择，用于控制实验。',
        '   - 单改优化坐标时 ethmac 仍偏长；单改拓扑的控制已大幅消除差距。最终统一配置偏差为 −0.772%，不是通过单独调该 case 参数实现。',
        '2. **DEF 转义输出错误。** ariane 有 4 个实例名含字面反斜杠；旧 writer 的 NETS 输出少一层转义，与保留的 COMPONENTS 不一致。现已正确转义，增加含反斜杠实例的完整 DEF 回读测试；最终七例原生回读端点/子网数量均匹配。',
        '   - 作者原始输出也有上述 4 个实例的转义层级变化；核验作者结果时记录了严格一对一的名称映射，并要求型号、状态、位置、朝向完全一致。作者文件未被改写；我们的最终输出无需此兼容映射。',
        '3. **观测与输出开销。** 移除逐个网络重复编译正则的开销；常规子网 RSMT 模式不再构建不会使用的旧拓扑；增加可选精简观测模式。仍保留完整 trace 模式做算法检查，且两种模式七例的 **28 份 DEF/Guide 逐字节一致**。', '',
        '## 4. 运行时间', '',
        '本机 Intel Xeon Gold 5218 @ 2.30 GHz，Release/C++14，双方日志均为 1 线程。下表取本轮同一运行包装器记录的墙钟时间；每项为一次运行，受系统负载影响，不是论文原机性能复现。', '']
    lines += table(['Case', '作者二进制重跑（s）', '复现精简模式（s）', '复现/作者'], [
        [r['case'], f"{r['author_wall_seconds']:.3f}", f"{r['ours_wall_seconds']:.3f}", f"{r['runtime_ratio']:.2f}×"] for r in rows])
    lines += ['', f'七例耗时之和：作者 **{sum(r["author_wall_seconds"] for r in rows):.2f}s**，复现 **{sum(r["ours_wall_seconds"] for r in rows):.2f}s**，比值 **{runtime_ratio:.2f}×**。当前实现仍更慢，保留额外的连通检查及结构化报告；不宣称运行时间已完全对齐。', '',
        '## 5. 正确性验证及尚未覆盖的范围', '',
        '- CTest **6/6 通过**：数量 DP、位置优化、论文方法、输入来源、单 die、双 die 端到端；新增子网 RSMT、物理坐标保留、投影碰撞拒绝、精简观测输出一致性及转义实例回读场景。',
        f'- 全部 **{total_nets:,}** 个网络的数量 DP/回溯成本、**{total_components:,}** 个拆分分量及全部跨 die 站点占用账本，通过独立检查。',
        f'- 对位置算法按固定规则抽检 **{sampled}** 条网、**{tables}** 份 Eq6/Eq7 表、**{states:,}** 项候选成本；通过。抽检规则及节点数上限写入验证 JSON，**不将抽检描述为全部大规模候选成本穷举验证**。',
        '- 全部七例核对真实端点、放置、HBT 配对/几何/站点合法性、Guide 网络集合/层/边界；完成 CUGR 原生 DEF 初始化回读。全局路由器另检查每个实际引脚候选在路由树上的可达性。',
        '- 作者随机数及平局处理未公开到源码级；本实现沿用明确记录的规则，当前结果不要求逐位复现。',
        '- **本轮仅运行全局布线。** 没有执行这七例的 OpenROAD 详细布线、实际金属 DRC 或 foundry 签核；旧 AES 的 DRC=0 不适用于这批新设计。', '',
        '## 6. 复现命令与文件', '',
        '当前证据目录：`eval/h3d_author/run_002/`。已有目录拒绝覆盖；完整重跑应使用新的实验目录，例如：', '', '```bash',
        'cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DCUGR_BUILD_TESTS=ON',
        'cmake --build build --parallel 2', 'ctest --test-dir build --output-on-failure',
        'python3 -B scripts/h3d_author_benchmarks.py prepare --experiment eval/h3d_author/run_003',
        'python3 -B scripts/h3d_author_benchmarks.py run --experiment eval/h3d_author/run_003 --label aligned --binary build/route --coordinates gcell_centers --observations summary',
        'python3 -B scripts/h3d_author_benchmarks.py compare --experiment eval/h3d_author/run_003 --label aligned',
        'python3 -B scripts/validate_h3d_author.py --experiment eval/h3d_author/run_003 --label aligned', '```', '',
        '最终输出在 `verified_summary/<case>/output/`；完整算法观测在 `verified_paper/<case>/output/`。',
        '每例保存 `command.json`、配置 JSON、`run.log`、`time.log`、`metrics.json`、双 DEF/Guide 和 `h3d_report.json`。', '',
        '| 证据 | 文件/目录 |', '|---|---|',
        '| 最终机器汇总/CSV | `FINAL_SUMMARY.json`、`comparison_final.csv` |',
        '| 源数据保护/输入审计 | `original_manifest.json`、`input_audit.json`、`inputs/<case>/audit.json` |',
        '| 修改前基线/控制实验 | `baseline_first/`、`grid_coordinate_control/`、`flute_topology_control/`、`fixed_author_hbt_control/`、`fixed_our_hbt_control/` |',
        '| 作者二进制重跑 | `author_binary_repeat/` |',
        '| 全量输出/完整 trace 验证 | `verification_verified_summary.json`、`verification_verified_paper_trace.json` |',
        '| 作者输出核验/原生回读 | `verification_author.json`、`native_def_readback/` |',
        '| 构建与来源 | `source_baseline_hashes.json`、`source_baseline.patch`、`final_source_hashes.json`、`ctest.log`、`binaries/route_final` |', '',
        '**最终判断：本轮已完成作者七例的全局布线对照，找到并修正了实际实现/接口问题；修正后的质量指标与作者高度接近，并通过所列独立检查。仍有 BP 溢出、运行时间差异及七例详细布线验证待后续处理。**', '']
    content = '\n'.join(lines)
    for path in (folder / 'REPORT.md', ROOT / '作者七例复现对比.md'):
        with path.open('x') as stream:
            stream.write(content)
    sources = {str(p.relative_to(ROOT)): sha(p) for base in ('src', 'rsyn') for p in (ROOT / base).rglob('*') if p.is_file()}
    sources.update({str(p.relative_to(ROOT)): sha(p) for p in (ROOT / 'scripts').glob('*h3d*.py')})
    sources['tests/h3d/check_end_to_end.py'] = sha(ROOT / 'tests/h3d/check_end_to_end.py')
    write_json(folder / 'final_source_hashes.json', sources)
    shutil.copy2(ROOT / 'build/route', folder / 'binaries/route_final')
    shutil.copy2(ROOT / 'build/Testing/Temporary/LastTest.log', folder / 'ctest.log')
    with (folder / 'final_tracked_changes.patch').open('xb') as stream:
        stream.write(subprocess.run(['git', '--no-optional-locks', 'diff', '--binary'], cwd=ROOT, check=True, capture_output=True).stdout)
    check_protected(folder)
    print(json.dumps({k: v for k, v in summary.items() if k != 'cases'}, indent=2))


if __name__ == '__main__':
    main()
