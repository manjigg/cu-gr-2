"""Generate scope-matched comparison tables from native runs and the published H3D row data."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics

from native_bench import CASES
from native_profile import write_table

# H3D ICCAD Table III, 3 µm. WL and Via refer to detailed routing.
PAPER={
    'aes':(151657,123480,526,1.335),
    'tinyRocket':(336830,205913,2179,2.441),
    'ethmac':(439322,223486,4954,2.975),
    'mor1kx':(1330973,562741,7664,7.684),
    'ariane':(3142161,1396919,19504,18.736),
    'jpegencode':(2514129,1413384,19507,18.648),
    'bp':(4700736,1986244,24315,35.960),
}
METRICS=('wirelength_um','via_count','hbt_count','seconds')


def rows(path):
    if not path.is_file():
        return {}
    with path.open() as stream:
        return {r['benchmark']:r for r in csv.DictReader(stream,delimiter='\t')}


def number(s):
    return float(s) if s not in ('',None) else None


def metrics(row,detail=False):
    names=('total_wire_length_um','total_via_count','hbt_count','sum_wall_seconds') if detail else (
        'wirelength_um','via_count','used_hbt','wall_seconds')
    return tuple(number(row.get(k)) for k in names)


def comparison(case,old,new,scope):
    row=dict(benchmark=case,comparison_scope=scope)
    for name,a,b in zip(METRICS,old,new):
        row['old_'+name]=a
        row['new_'+name]=b
        row[name+'_change_pct']=100*(b-a)/a if a and b is not None else None
    row['speedup']=old[-1]/new[-1] if old[-1] and new[-1] else None
    return row


def case_comparison(before,after,detail=False):
    scope='detailed_geometry_and_sum_of_die_wall_seconds' if detail else 'global_process_wall_seconds'
    pairs=[(metrics(before.get(c,{}),detail),metrics(after.get(c,{}),detail)) for c in CASES]
    table=[comparison(c,old,new,scope) for c,(old,new) in zip(CASES,pairs)]
    def totals(index):
        return tuple(sum(col) if all(value is not None for value in col) else None
                     for col in zip(*(pair[index] for pair in pairs)))
    table.append(comparison('TOTAL',totals(0),totals(1),scope))
    return table


def sha256(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda:stream.read(1048576),b''):
            digest.update(data)
    return digest.hexdigest()


def provenance(root,detail):
    binary=root/'route'
    if not binary.is_file():
        return
    global_runs={}
    detail_runs={}
    for case in CASES:
        job=root/case
        manifest=job/'manifest.json'
        if not manifest.is_file():
            continue
        data=json.loads(manifest.read_text())
        names=('top.guide','bottom.guide','route_edges.tsv','hbt_assignment.tsv','components.jsonl','routing_statistics.json')
        global_runs[case]=dict(manifest_sha256=sha256(manifest),
            input_sha256=data['provenance']['input_sha256'],
            outputs={f:sha256(job/'output'/f) for f in names if (job/'output'/f).is_file()})
        end=detail/case
        files=('top/routed.def','bottom/routed.def','report.json','verified_report.json','joint_geometry.json')
        if end.is_dir():
            source_path=end/'guide_provenance.json'
            source=json.loads(source_path.read_text()) if source_path.is_file() else None
            if source and any(global_runs[case]['outputs'][name]!=value for name,value in source['sha256'].items()):
                raise ValueError('DRT was run with different global guides or landing endpoints for '+case)
            detail_runs[case]=dict(guide_provenance=source,
                outputs={f:sha256(end/f) for f in files if (end/f).is_file()})
    record=dict(executable_sha256=sha256(binary),cases=global_runs)
    revision=root/'code_revision.txt'
    if revision.exists():
        record['router_source_commit']=revision.read_text().strip()
    (root/'run_provenance.json').write_text(json.dumps(record,indent=2)+'\n')
    if detail_runs:
        (detail/'run_provenance.json').write_text(json.dumps(detail_runs,indent=2)+'\n')


def render_summary(root,fields,title):
    data=list(rows(root/'summary.tsv').values())
    if not data:
        return
    with (root/'summary.csv').open('w') as stream:
        writer=csv.DictWriter(stream,data[0],lineterminator='\n')
        writer.writeheader()
        writer.writerows(data)
    lines=[title,'','| '+' | '.join(fields)+' |','| '+' | '.join('---' for _ in fields)+' |']
    for row in data:
        lines.append('| '+' | '.join(row.get(k,'') for k in fields)+' |')
    (root/'summary.md').write_text('\n'.join(lines)+'\n')


def experiment_tables(root):
    breakdown=[]
    for case in CASES:
        path=root/case/'profile.json'
        if not path.is_file():
            continue
        profile=json.loads(path.read_text())
        for name,values in profile['modules'].items():
            seconds=profile['modules']['total']['inclusive_seconds']
            breakdown.append(dict(benchmark=case,module=name,**values,
                exclusive_percent=100*values['exclusive_seconds']/seconds))
    if breakdown:
        write_table(root/'runtime_breakdown.tsv',breakdown)
    families={'ablation_summary.tsv':[root/'_baseline',root/'_profiles/m1',root/'_single',root,
                                    *sorted((root/'_ablation').glob('*/'))],
              'scalability_summary.tsv':sorted((root/'_parallel').glob('*/')),
              'parameter_summary.tsv':sorted((root/'_parameter_sweep').glob('*/'))}
    for name,folders in families.items():
        table=[]
        for folder in folders:
            for case,row in rows(folder/'summary.tsv').items():
                if case not in CASES:
                    continue
                profile=folder/case/'profile.json'
                counters=json.loads(profile.read_text())['counters'] if profile.is_file() else {}
                stats=folder/case/'output/routing_statistics.json'
                report=json.loads(stats.read_text()) if stats.is_file() else {}
                stage=report.get('stages',[])
                manifest=json.loads((folder/case/'manifest.json').read_text())
                replay_path=folder/case/'ledger-replay.json'
                replay=json.loads(replay_path.read_text()) if replay_path.is_file() else None
                replay_valid=(replay is not None and replay['unconnected_nets']==0 and
                              replay['via_capacity_violations']==0 and replay['hbt_violations']==0 and
                              replay['resource_keys_after_release']==0 and sum(replay['wire_overflow_by_die'])<=1e-12)
                table.append(dict(experiment=str(folder.relative_to(root)) or '.',benchmark=case,status=row['status'],
                    threads=manifest.get('provenance',{}).get('requested_threads',1),
                    beam_width=manifest.get('beam_width'),hbt_limit=manifest.get('hbt_candidate_limit'),
                    seed_order=manifest.get('seed_order'),local_exact_threshold=manifest.get('local_exact_threshold'),
                    wall_seconds=row.get('wall_seconds'),max_rss_kb=row.get('max_rss_kb'),wirelength_um=row.get('wirelength_um'),
                    via_count=row.get('via_count'),hbt_count=row.get('used_hbt'),wire_overflow=row.get('wire_overflow'),
                    router_seconds=report.get('routing_seconds'),
                    objective=stage[-1].get('best_feasible') if stage else None,
                    candidate_count=sum(s.get('pool_size',0) for s in stage),
                    active_workers=max(((s.get('local_optimizer') or {}).get('workers',1) for s in stage),default=1),
                    updated_nets=sum(s.get('accepted_nets',0) for s in stage if s['stage']>1),
                    dp_labels=counters.get('dp_labels_generated'),hbt_sites_visited=counters.get('hbt_sites_visited'),
                    ledger_replay='PASSED' if replay_valid else ('FAILED' if replay is not None else '')))
        if table:
            if name=='scalability_summary.tsv':
                one={r['benchmark']:number(r['wall_seconds']) for r in table if r['threads']==1}
                for row in table:
                    base=one.get(row['benchmark'])
                    elapsed=number(row['wall_seconds'])
                    row['speedup_vs_one_worker']=base/elapsed if base and elapsed else None
            write_table(root/name,table)
    for name,runs in (
        ('repeat_summary.tsv',[root,*sorted((root/'_repeats').glob('*/'))]),
        ('repeat_baseline_summary.tsv',[root/'_baseline',*sorted((root/'_baseline_repeats').glob('*/'))])):
        samples=repeat_records(runs)
        if samples:
            write_table(root/name,samples)


def repeat_records(runs):
    samples=[]
    for case in CASES:
        times=[]
        trial_count=0
        for run in runs:
            result=rows(run/'summary.tsv').get(case)
            if result is not None:
                trial_count+=1
                if result.get('exit_code')=='0' and result.get('wall_seconds'):
                    times.append(float(result['wall_seconds']))
        if times:
            samples.append(dict(benchmark=case,total_observations=trial_count,successful=len(times),
                median_wall_seconds=statistics.median(times),min_wall_seconds=min(times),max_wall_seconds=max(times),
                peak_to_peak_seconds=max(times)-min(times),wall_samples_seconds=','.join(str(x) for x in times)))
    if samples:
        total=[]
        for run in runs:
            observed=rows(run/'summary.tsv')
            if all(c in observed and observed[c].get('exit_code')=='0' for c in CASES):
                total.append(sum(float(observed[c]['wall_seconds']) for c in CASES))
        if total:
            samples.append(dict(benchmark='TOTAL',total_observations=len(runs),successful=len(total),
                median_wall_seconds=statistics.median(total),min_wall_seconds=min(total),max_wall_seconds=max(total),
                peak_to_peak_seconds=max(total)-min(total),wall_samples_seconds=','.join(str(x) for x in total)))
    return samples


def generate(root,detail,native_detail):
    original=rows(root/'_baseline/summary.tsv')
    optimized=rows(root/'summary.tsv')
    h3d=rows(root/'_profiles/h3d_local/summary.tsv')
    if optimized:
        write_table(root/'comparison_native.tsv',case_comparison(original,optimized))
        if h3d:
            reference=[dict(benchmark=c,h3d_local_threads=h3d[c]['threads'],h3d_local_wall_seconds=h3d[c]['wall_seconds'],
                      h3d_local_wire_metric=h3d[c]['wirelength_h3d_metric'],h3d_local_via_count=h3d[c]['via_count'],
                      h3d_legal_site_count=h3d[c]['available_hbt'],native_legal_site_count=optimized[c]['legal_hbt_sites'],
                      h3d_local_hbt_count=h3d[c]['used_hbt'],timeopt_global_wall_seconds=optimized[c]['wall_seconds'],
                      h3d_local_max_rss_kb=h3d[c]['max_rss_kb'],timeopt_wire_length_metric=optimized[c].get('wire_length_metric'),
                      timeopt_global_wirelength_um=optimized[c].get('wirelength_um'),timeopt_global_via_count=optimized[c].get('via_count'),
                      scope='same inputs, H3D tool-specific grid and HBT eligibility') for c in CASES if c in h3d and c in optimized]
            if reference:
                write_table(root/'comparison_h3d.tsv',reference)
        render_summary(root,['benchmark','status','wirelength_um','via_count','used_hbt',
                             'wall_seconds','max_rss_kb','unconnected_nets'],'# TimeOpt Native 总体布线')
    experiment_tables(root)
    new=rows(detail/'summary.tsv')
    provenance(root,detail)
    if not new:
        return
    old=rows(native_detail/'summary.tsv')
    detailed_comparison=case_comparison(old,new,detail=True)
    write_table(detail/'comparison_native.tsv',detailed_comparison)
    refs=[]
    all_rows=[]
    paper_totals=[0.,0.,0.]
    native_totals=[0.,0.,0.]
    measured_count=0
    for case in CASES:
        fresh=new.get(case,{})
        prior=old.get(case,{})
        paper=PAPER[case]
        measured=metrics(fresh,True)
        if fresh.get('status')=='PASSED' and all(v is not None for v in measured[:3]):
            measured_count+=1
            for i in range(3):
                paper_totals[i]+=paper[i]
                native_totals[i]+=measured[i]
        scope='detailed_geometry_and_separately_reported_global_runtime'
        delta=comparison(case,(*paper[:3],None),(*measured[:3],None),scope)
        delta.update(h3d_paper_global_seconds=paper[-1],h3d_local_global_wall_seconds=h3d.get(case,{}).get('wall_seconds'),
                     timeopt_global_router_seconds=fresh.get('global_router_seconds'))
        refs.append(delta)
        for name,q,global_seconds,report,rss in (
            ('H3D paper',paper,paper[-1],{},None),
            ('Native baseline',metrics(prior,True),original.get(case,{}).get('wall_seconds'),prior,
             original.get(case,{}).get('max_rss_kb')),
            ('TimeOpt Native',measured,optimized.get(case,{}).get('wall_seconds'),fresh,
             optimized.get(case,{}).get('max_rss_kb'))):
            all_rows.append(dict(benchmark=case,variant=name,detailed_wirelength_um=q[0],detailed_vias=q[1],
                hbt_count=q[2],global_seconds=global_seconds,global_time_scope='paper' if name=='H3D paper' else 'local_wall',
                drc=report.get('total_drc'),joint_connectivity=report.get('joint_connectivity'),local_global_max_rss_kb=rss))
    if measured_count==len(CASES):
        total=comparison('TOTAL',(*paper_totals,None),(*native_totals,None),
                         'detailed_geometry_and_separately_reported_global_runtime')
        total.update(h3d_paper_global_seconds=sum(row[-1] for row in PAPER.values()),
                     h3d_local_global_wall_seconds=sum(float(h3d[c]['wall_seconds']) for c in CASES) if len(h3d)==len(CASES) else None,
                     timeopt_global_router_seconds=sum(float(new[c]['global_router_seconds']) for c in CASES))
        refs.append(total)
    write_table(detail/'comparison_h3d.tsv',refs)
    write_table(detail/'comparison_all.tsv',all_rows)
    render_summary(detail,['benchmark','status','top_wire_length_um','bottom_wire_length_um','total_wire_length_um',
                           'total_via_count','hbt_count','total_drc','joint_connectivity'],'# TimeOpt Native 详细布线')
    lines=['# TimeOpt 详细布线验证','',
           '统计使用正式全局运行的 Guide、HBT 分配与真实端点分量。在两颗 Die 上分别运行 TritonRoute，随后核验导体分量与双侧 HBT 连接。','',
           f'通过的双 Die case 为 {measured_count}/{len(CASES)}。']
    if measured_count==len(CASES):
        summary=detailed_comparison[-1]
        total_drc=sum(int(row['total_drc']) for row in new.values())
        lines.extend([
            f"两侧 DRC 合计 {total_drc}，全部 case 联合连通检查通过。",
            f"详细布线总线长 {native_totals[0]:,.0f} µm，普通 Via {native_totals[1]:,.0f} 个，HBT {native_totals[2]:,.0f} 个。",
            f"相对原始 Native 的详细布线，线长变化 {summary['wirelength_um_change_pct']:+.3f}%，"
            f"普通 Via 变化 {summary['via_count_change_pct']:+.3f}%，HBT 变化 {summary['hbt_count_change_pct']:+.3f}%。"])
    lines.extend(['','[正式总表](summary.tsv) · [与原始 Native 比较](comparison_native.tsv) · '
                   '[与 H3D 论文比较](comparison_h3d.tsv) · [原始文件及工具哈希](run_provenance.json)'])
    (detail/'experiment_report.md').write_text('\n'.join(lines)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('global_dir','detail_dir','baseline_detail'):
        parser.add_argument(name,type=Path)
    a=parser.parse_args()
    generate(a.global_dir.resolve(),a.detail_dir.resolve(),a.baseline_detail.resolve())
