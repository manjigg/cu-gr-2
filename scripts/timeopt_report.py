"""Generate scope-matched comparison tables from native runs and the published H3D row data."""
import argparse
import csv
import json
from pathlib import Path

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
                    dp_labels=counters.get('dp_labels_generated'),hbt_sites_visited=counters.get('hbt_sites_visited')))
        if table:
            write_table(root/name,table)


def generate(root,detail,native_detail):
    original=rows(root/'_baseline/summary.tsv')
    optimized=rows(root/'summary.tsv')
    h3d=rows(root/'_profiles/h3d_local/summary.tsv')
    if optimized:
        write_table(root/'comparison_native.tsv',[comparison(c,metrics(original.get(c,{})),
            metrics(optimized.get(c,{})),'global_process_wall_seconds') for c in CASES])
        if h3d:
            reference=[dict(benchmark=c,h3d_local_threads=h3d[c]['threads'],h3d_local_wall_seconds=h3d[c]['wall_seconds'],
                      h3d_local_wire_metric=h3d[c]['wirelength_h3d_metric'],h3d_local_via_count=h3d[c]['via_count'],
                      h3d_legal_site_count=h3d[c]['available_hbt'],native_legal_site_count=optimized[c]['legal_hbt_sites'],
                      h3d_local_hbt_count=h3d[c]['used_hbt'],timeopt_global_wall_seconds=optimized[c]['wall_seconds'],
                      scope='same inputs, H3D tool-specific grid and HBT eligibility') for c in CASES if c in h3d and c in optimized]
            if reference:
                write_table(root/'comparison_h3d.tsv',reference)
        render_summary(root,['benchmark','status','wirelength_um','via_count','used_hbt',
                             'wall_seconds','max_rss_kb','unconnected_nets'],'# TimeOpt Native 总体布线')
    experiment_tables(root)
    new=rows(detail/'summary.tsv')
    if not new:
        return
    old=rows(native_detail/'summary.tsv')
    write_table(detail/'comparison_native.tsv',[comparison(c,metrics(old.get(c,{}),True),
        metrics(new.get(c,{}),True),'detailed_geometry_and_sum_of_die_wall_seconds') for c in CASES])
    refs=[]
    all_rows=[]
    for case in CASES:
        fresh=new.get(case,{})
        prior=old.get(case,{})
        paper=PAPER[case]
        measured=metrics(fresh,True)
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
    write_table(detail/'comparison_h3d.tsv',refs)
    write_table(detail/'comparison_all.tsv',all_rows)
    render_summary(detail,['benchmark','status','top_wire_length_um','bottom_wire_length_um','total_wire_length_um',
                           'total_via_count','hbt_count','total_drc','joint_connectivity'],'# TimeOpt Native 详细布线')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('global_dir','detail_dir','baseline_detail'):
        parser.add_argument(name,type=Path)
    a=parser.parse_args()
    generate(a.global_dir.resolve(),a.detail_dir.resolve(),a.baseline_detail.resolve())
