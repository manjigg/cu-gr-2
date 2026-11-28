"""Aggregate measured maze trials, keeping fixed work and end-to-end runs separate."""
import argparse
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

from new3d_regression import write_tables


def summarize(root):
    fixed, batches = [], []
    for path in sorted((root/'_experiments/fixed_workload').glob('*/*/output/routing_statistics.json')):
        case=path.parents[1].name
        series=defaultdict(list)
        study = json.loads(path.read_text()).get('maze_work_study')
        if not study:
            continue
        for run in study['runs']:
            series[run['threads']].append(run)
        baseline=median(r['seconds'] for r in series[1])
        for threads,group in sorted(series.items()):
            times=[r['seconds'] for r in group]
            work={(r['tasks'],r['checks'],r['astar_expanded_nodes'],r['candidates']) for r in group+series[1]}
            if len(work)!=1 or not all(r['equal_to_serial'] for r in group):
                raise ValueError(f'Non-equivalent candidate computation in {path}')
            t,c,n,k=work.pop()
            speedup=baseline/median(times)
            fixed.append(dict(benchmark=case,threads=threads,repeats=len(group),tasks=t,checks=c,expansions=n,
                candidates=k,median_seconds=median(times),minimum_seconds=min(times),maximum_seconds=max(times),
                search_speedup=speedup,search_efficiency=speedup/threads,path=str(path)))
    for path in sorted((root/'_experiments/parallel_scaling').glob('*/*/output/routing_statistics.json')):
        stats=json.loads(path.read_text())
        stage=next(s for s in stats['stages'] if s['stage']==3)
        row=dict(benchmark=path.parents[1].name,trial=path.parents[2].name,global_seconds=stats['routing_seconds'],
                 stage3_seconds=stage['seconds'],status=stats['status'],path=str(path))
        row.update(stage.get('parallel_maze') or {})
        batches.append(row)
    for records,name in [(fixed,'parallel_summary'),(batches,'parallel_stage_summary')]:
        if records:
            write_tables(root,name,list(dict.fromkeys(k for r in records for k in r)),records)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('experiment_root',type=Path)
    summarize(parser.parse_args().experiment_root)
