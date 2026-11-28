"""Report V3 detailed, conductor and HBT evidence as each real run finishes."""
import argparse
import json
from pathlib import Path
from time import sleep

from native_bench import CASES
from native_summary import detail_record
from new3d_regression import comparisons, write_tables, DETAIL_FIELDS
from new3d_experiments import ROOT, G, X, read, save

D = ROOT/'result-3/new3Ddetail'


def report(final, gd, case):
    path=final/case
    row=detail_record(final, case, gd, {})
    if not (path/'verified_report.json').is_file() or not (path/'joint_geometry.json').is_file():
        row['status']='FAIL' if (path/'report.json').is_file() and not read(path/'report.json').get('passed') else (
            'IN_PROGRESS' if path.is_dir() or (final/(case+'.replay.log')).exists() else 'NOT_STARTED')
        row['reason']='独立联合验证尚无通过记录'
        return row
    joint=read(path/'joint_geometry.json')
    ledger_file=gd/case/'ledger-replay.json'
    ledger=read(ledger_file) if ledger_file.is_file() else {}
    row.update(real_pins=joint['real_pins'],checked_nets=joint['global_nets'],hbt_pairs=joint['hbt_pairs'],
               joint_disconnected=joint['disconnected_nets'],joint_mixed=joint['mixed_nets'])
    ok=(row['status']=='PASSED' and joint['status']=='passed' and joint['disconnected_nets']==0
        and joint['mixed_nets']==0 and joint['hbt_pairs']==ledger.get('hbt_count')
        and joint['real_pins']==ledger.get('physical_pins') and joint['global_nets']==ledger.get('nets')
        and ledger.get('resource_keys_after_release')==0)
    for side in ('top','bottom'):
        path_geom=path/side/'geometry_audit.json'
        if not path_geom.is_file():
            ok=False
            continue
        geometry=read(path_geom)
        physical=geometry['physical_connectivity']
        for field in ('disconnected_net_count','cross_net_contact_pairs','geometry_error_count','floating_routed_components'):
            row[side+'_'+field]=physical.get(field)
            ok=ok and physical.get(field)==0
        ok=ok and geometry['status']=='passed' and geometry['logical_endpoints_preserved'] and geometry['physical_pin_geometry_preserved']
        ok=ok and geometry['placement_and_hbt_fixed_locations_preserved'] and row[side+'_drc']==0
    row['status']='PASS' if ok else 'FAIL'
    row['independent_check']='passed' if ok else 'failed'
    return row


def safe_report(final,gd,c):
    try:
        return report(final,gd,c)
    except (ValueError,KeyError,TypeError,OSError,IndexError) as error:
        return dict(benchmark=c,status='INCONCLUSIVE',output_directory=str(final/c),reason=str(error))


def update():
    final=D/'final'
    data=[safe_report(final,G/'final',c) for c in CASES]
    import csv
    if (final/'summary.tsv').is_file():
        with (final/'summary.tsv').open() as f:
            driver={r['benchmark']:r['status'] for r in csv.DictReader(f,delimiter='\t')}
        for r in data:
            task=driver.get(r['benchmark'])
            if task not in (None,'PENDING','PASSED'):
                r.update(status='FAIL',reason='Detailed batch: '+task)
    passed=sum(r['status']=='PASS' for r in data)
    verified_fields='real_pins checked_nets hbt_pairs joint_disconnected joint_mixed top_disconnected_net_count bottom_disconnected_net_count top_cross_net_contact_pairs bottom_cross_net_contact_pairs top_geometry_error_count bottom_geometry_error_count top_floating_routed_components bottom_floating_routed_components'.split()
    write_tables(D,'summary',DETAIL_FIELDS+verified_fields,data)
    old=[safe_report(D/'_experiments/baseline',X/'baseline/global',c) for c in CASES]
    comp=comparisons(old,data,'total_wire_length_um total_via_count hbt_count detail_wall_seconds'.split(),'detail_wall_seconds')
    write_tables(D,'comparison',list(comp[0]),comp)
    state=dict(passed=passed,designs=len(CASES),cases={r['benchmark']:r['status'] for r in data})
    import math
    if passed==len(CASES) and all(r.get('speedup') for r in comp):
        state['geomean_speedup']=math.exp(sum(math.log(r['speedup']) for r in comp)/len(comp))
    save(D/'verification_status.json',state)
    text=['# V3 详细布线及物理连通性验证','',
        f'正式批次使用 new3D/final 的 Guide、物理 HBT 和分量映射。当前 {passed}/7 个设计通过下表全部验证。七个设计均保留行，N/A 表示缺少实际测量。','',
        '| 设计 | 状态 | Top DRC | Bottom DRC | Top 几何 | Bottom 几何 | 验证真实 Pin | HBT 对数 | 两侧断网数 | 两侧跨网接触数 | 联合未通 / 混网 |',
        '|---|---|---:|---:|---|---|---:|---:|---|---|---|']
    def f(v):
        return 'N/A' if v is None else str(v)
    for r in data:
        def pair(a,b):
            return f(r.get(a))+' / '+f(r.get(b))
        v=[r['benchmark'],r['status'],r.get('top_drc'),r.get('bottom_drc'),r.get('top_geometry'),
           r.get('bottom_geometry'),r.get('real_pins'),r.get('hbt_pairs'),
           pair('top_disconnected_net_count','bottom_disconnected_net_count'),
           pair('top_cross_net_contact_pairs','bottom_cross_net_contact_pairs'),pair('joint_disconnected','joint_mixed')]
        text.append('| '+' | '.join(map(f,v))+' |')
    text+=['','每侧均采用 OpenROAD/TritonRoute，以八线程执行详细布线，两颗 Die 可并行。总表分别保留两侧运行时间、其和、实际详细布线墙钟，以及上游全局布线时间。','',
        '每个 final/<设计>/ 目录保留独立派生 DEF、真实导体、DRC（设计规则检查）报告和日志。OpenDB 导体审计将同层矩形相交关系、各 Via 切孔与包围金属、真实接入点纳入同侧连接图。native_joint 依据物理 HBT 端点和同侧分量重建全局网，检查断路和错误跨网连接。本报告另与已回放账本逐项核对实际网数、Pin 数及物理 HBT 数。','',
        '验证覆盖原 LEF/DEF 信号网络与当前矩形导体模型。完整制造签核还需键合焊盘工艺、对准误差、可靠性和适用的跨 Die 签核规则。全局资源零溢出、两侧 DRC、实际导体通断在表中独立记录。',
        '', '[逐侧线长、Via、DRC 与运行时间](summary.tsv) · [与同机冻结 TimeOpt 的同阶段比较](comparison.tsv)']
    if passed:
        group=[r for r in data if r['status']=='PASS']
        text+=['',f'已通过的 {passed} 个设计详细布线合计 {sum(r["total_wire_length_um"] for r in group):,.0f} µm、{sum(r["total_via_count"] for r in group):,} 个普通 Via 和 {sum(r["hbt_count"] for r in group):,} 个 HBT。']
    (D/'verification_report.md').write_text('\n'.join(text)+'\n')
    return state


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--monitor',action='store_true')
    args=parser.parse_args()
    while True:
        state=update()
        if not args.monitor or all(s in ('PASS','FAIL') for s in state['cases'].values()):
            break
        sleep(45)
