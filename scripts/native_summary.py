"""Summarize the native router, physical resource replay and TritonRoute evidence."""
import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
from native_bench import CASES


def load(path):
    return json.loads(path.read_text())


def write_table(path, records):
    keys=list(dict.fromkeys(key for record in records for key in record))
    temporary=path.with_suffix('.tmp')
    with temporary.open('w') as stream:
        t=csv.DictWriter(stream,keys,delimiter='\t',lineterminator='\n')
        t.writeheader()
        t.writerows(records)
    temporary.replace(path)


def elapsed_process(job, case):
    path=job/'process.json'
    if path.exists():
        return load(path)
    # 早期批次把 time(1) 的退出码和耗时保存在实际运行目录的总表。
    with (job.resolve().parent/'summary.tsv').open() as stream:
        items=[r for r in csv.DictReader(stream,delimiter='\t') if r['benchmark']==case]
    if len(items)!=1:
        raise ValueError('Missing unique process record for '+case)
    return items[0]


def replay_edges(job, executable):
    """Only publish a report after all trees have been committed and released."""
    current=job/'ledger-replay.json'
    with TemporaryDirectory(dir=job) as temp:
        fresh=Path(temp)/'replay.json'
        with (job/'ledger-replay.log').open('w') as log:
            subprocess.run([executable,job/'manifest.json',job/'output/route_edges.tsv',fresh],
                           cwd=temp,stdout=log,stderr=subprocess.STDOUT,check=True)
        if current.exists() and load(current)!=load(fresh):
            raise ValueError('Resource replay changed for '+job.name)
        if not current.exists():
            fresh.replace(current)


def global_record(directory,case,replay_binary=None):
    job=directory/case
    process=elapsed_process(job,case)
    stats_path=job/'output/routing_statistics.json'
    if not stats_path.exists():
        return dict(benchmark=case,status='PROCESS_ERROR',exit_code=process['exit_code'],
                    wall_seconds=process['wall_seconds'],max_rss_kb=process['max_rss_kb'],
                    output_directory=str(job/'output'))
    stats=load(stats_path)
    m=stats.get('metrics')
    row=dict(benchmark=case,status=stats['status'])
    log=(job/'router.log').read_text(errors='replace')
    row['threads']=int(re.search(r'threads\s*:\s*(\d+)',log)[1])
    if m:
        if replay_binary:
            replay_edges(job,replay_binary)
        replay=load(job/'ledger-replay.json')
        inventory=stats['inventory']
        if (not math.isclose(sum(replay['wire_um_by_die']),m['wirelength_um'],rel_tol=1e-9) or
            sum(replay['via_by_die'])!=m['via_count'] or replay['hbt_count']!=m['used_hbt'] or
            replay['nets']!=inventory['global_nets'] or replay['physical_pins']!=inventory['real_pins'] or
            replay['resource_keys_after_release']!=0):
            raise ValueError('Resource replay and global report disagree for '+case)
        row.update(wire_length_metric=sum(replay['wire_metric_by_die']),wirelength_um=sum(replay['wire_um_by_die']),
                   via_count=sum(replay['via_by_die']),wire_overflow=sum(replay['wire_overflow_by_die']),
                   used_hbt=replay['hbt_count'])
    row.update(router_reported_seconds=stats['routing_seconds'],wall_seconds=float(process['wall_seconds']),
               max_rss_kb=int(process['max_rss_kb']),exit_code=int(process['exit_code']),
               global_nets=stats['inventory']['global_nets'],cross_die_nets=stats['inventory']['cross_die_nets'],
               real_pins=stats['inventory']['real_pins'])
    if m:
        with (job/'output/hbt_assignment.tsv').open() as f:
            crossings=Counter(int(r['net_id']) for r in csv.DictReader(f,delimiter='\t'))
        if sum(crossings.values())!=replay['hbt_count']:
            raise ValueError('HBT assignment and resource replay disagree for '+case)
        row.update(top_wire_length_um=replay['wire_um_by_die'][0],bottom_wire_length_um=replay['wire_um_by_die'][1],
                   top_via_count=replay['via_by_die'][0],bottom_via_count=replay['via_by_die'][1],
                   top_wire_overflow=replay['wire_overflow_by_die'][0],bottom_wire_overflow=replay['wire_overflow_by_die'][1],
                   max_wire_overflow=max(replay['max_wire_overflow_by_die']),unconnected_nets=replay['unconnected_nets'],
                   via_capacity_violations=replay['via_capacity_violations'],hbt_violations=replay['hbt_violations'],
                   nets_using_hbt=len(crossings),multi_hbt_nets=sum(k>1 for k in crossings.values()),
                   max_hbts_per_net=max(crossings.values(),default=0))
    stage=stats['stages'][-1]
    row.update(solver_status=stage['solver_status'],best_feasible_objective=stage['best_feasible'],
               candidate_pool_lower_bound=stage['lower_bound'],candidate_pool_gap=stage['gap'],
               unrestricted_gap=stats.get('unrestricted_optimality_gap'),output_directory=str(job/'output'))
    return row


DETAIL_FIELDS=('benchmark status completed_dies passed_dies top_status bottom_status top_drc bottom_drc total_drc '
               'top_wire_length_um bottom_wire_length_um total_wire_length_um top_via_count bottom_via_count '
               'total_via_count hbt_count top_wall_seconds bottom_wall_seconds sum_wall_seconds top_geometry '
               'bottom_geometry joint_connectivity output_directory').split()


def detail_record(directory,case,global_dir,earlier):
    job=directory/case
    row=dict.fromkeys(DETAIL_FIELDS)
    row.update(benchmark=case,output_directory=str(job))
    source=global_dir/case/'output/routing_statistics.json'
    stats=load(source) if source.exists() else {}
    metrics=stats.get('metrics') or {}
    row['hbt_count']=metrics.get('used_hbt')
    path=job/'verified_report.json'
    if not path.exists():
        path=job/'report.json'
    report=load(path) if path.exists() else {}
    completed=passed=0
    for side in ('top','bottom'):
        result=job/side/'detail.json'
        res=report.get(side) or (load(result) if result.exists() else None)
        if not res:
            row[side+'_status']='NOT_COMPLETED'
            continue
        done=res['exit_code']==0 and (job/side/'routed.def').is_file()
        good=done and res['routed'] and res['drc']==0 and res.get('geometry')=='passed'
        completed+=done
        passed+=good
        row[side+'_status']='PASSED' if good else 'FAILED'
        row[side+'_drc']=res['drc']
        row[side+'_wire_length_um']=res['wirelength_um']
        row[side+'_via_count']=res['via_count']
        row[side+'_wall_seconds']=res['wall_seconds']
        row[side+'_geometry']=res.get('geometry')
    if report:
        row['status']='PASSED' if passed==2 and report['passed'] else 'FAILED'
    else:
        row['status']=earlier.get(case,'NOT_COMPLETED')
        if row['status']=='PASSED':
            raise ValueError('Missing detailed routing report for '+case)
    joint=job/'joint_geometry.json'
    verified=load(joint)['status'] if joint.exists() else report.get('joint_connectivity')
    if verified=='failed':
        row['status']='FAILED'
    row.update(completed_dies=completed,passed_dies=passed,joint_connectivity=verified)
    for key in ('drc','wire_length_um','via_count','wall_seconds'):
        values=[row[side+'_'+key] for side in ('top','bottom')]
        row[('sum_' if key=='wall_seconds' else 'total_')+key]=None if None in values else sum(values)
    return row


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('global_dir',type=Path)
    p.add_argument('--detail-dir',type=Path)
    p.add_argument('--replay',type=Path,help='Rebuild resource evidence from every emitted physical tree with native_replay')
    a=p.parse_args()
    global_dir=a.global_dir.resolve()
    binary=a.replay.resolve() if a.replay else None
    write_table(global_dir/'summary.tsv',[global_record(global_dir,c,binary) for c in CASES])
    if a.detail_dir:
        detail_dir=a.detail_dir.resolve()
        path=detail_dir/'summary.tsv'
        earlier={}
        if path.exists():
            with path.open() as stream:
                earlier={r['benchmark']:r['status'] for r in csv.DictReader(stream,delimiter='\t')}
        write_table(path,[detail_record(detail_dir,c,global_dir,earlier) for c in CASES])
