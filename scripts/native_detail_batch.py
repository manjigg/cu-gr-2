"""Validate every completed native run, preserving infeasible inputs and failed DRT jobs."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from native_bench import CASES


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('openroad','global_dir','output','geometry_tool'):
        parser.add_argument(name,type=Path)
    parser.add_argument('--jobs',type=int,default=3)
    parser.add_argument('--threads',type=int,default=8)
    parser.add_argument('--cases',nargs='+',choices=CASES,default=CASES)
    parser.add_argument('--wait',action='store_true')
    parser.add_argument('--reuse-global',type=Path)
    parser.add_argument('--reuse-detail',type=Path)
    args=parser.parse_args()
    if bool(args.reuse_global)!=bool(args.reuse_detail):
        parser.error('Supply both reuse paths')
    root=args.global_dir.resolve()
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    scripts=Path(__file__).resolve().parent
    def validate(case):
        job=root/case
        stats_path=job/'output/routing_statistics.json'
        while args.wait and not stats_path.exists():
            if (root/'summary.tsv').exists() and any(r['benchmark']==case for r in
                  csv.DictReader((root/'summary.tsv').open(),delimiter='\t')):
                break
            time.sleep(5)
        if not stats_path.exists():
            return case,dict(status='GLOBAL_RUN_ERROR')
        stats=json.loads(stats_path.read_text())
        if not stats.get('metrics'):
            return case,dict(status='GLOBAL_'+stats['status'])
        directory=out/case
        with (out/(case+'.replay.log')).open('w') as log:
            r=subprocess.run([sys.executable,scripts/'native_check.py',job/'manifest.json',job/'output'],
                             stdout=log,stderr=subprocess.STDOUT)
        if r.returncode:
            return case,dict(status='GLOBAL_TREE_CHECK_FAILED')
        reused=False
        if args.reuse_global:
            prior=(args.reuse_global/case).resolve()
            prior_detail=(args.reuse_detail/case).resolve()
            source=prior/'output'
            def digest(path):
                h=hashlib.sha256()
                with path.open('rb') as stream:
                    for chunk in iter(lambda:stream.read(1048576),b''):
                        h.update(chunk)
                return h.hexdigest()
            files=('top.guide','bottom.guide','hbt_assignment.tsv','components.jsonl')
            if (source/'routing_statistics.json').is_file() and all((source/name).is_file() for name in files):
                material=('dies','mapping','bonded_dbu','bonding')
                old=json.loads((prior/'manifest.json').read_text())
                new=json.loads((job/'manifest.json').read_text())
                same=all(old[k]==new[k] for k in material) and old['provenance']['input_sha256']==new['provenance']['input_sha256']
                if same:
                    hashes={name:digest(job/'output'/name) for name in files}
                    same=all(hashes[name]==digest(source/name) for name in files)
                if same and prior_detail.is_dir():
                    while args.wait and not (prior_detail/'report.json').exists():
                        summary=args.reuse_detail/'summary.tsv'
                        if summary.exists() and any(r['benchmark']==case and r['status']!='PENDING' for r in
                                csv.DictReader(summary.open(),delimiter='\t')):
                            break
                        time.sleep(5)
                    if (prior_detail/'report.json').is_file():
                        directory.symlink_to(prior_detail,target_is_directory=True)
                        (out/(case+'.equivalence.json')).write_text(json.dumps(dict(source=str(prior_detail),sha256=hashes),indent=2))
                        reused=True
        if reused:
            passed=json.loads((directory/'report.json').read_text()).get('passed',False)
        else:
            with (out/(case+'.log')).open('w') as log:
                p=subprocess.run([sys.executable,scripts/'native_detail.py',args.openroad,job/'manifest.json',
                    job/'output',directory,'--threads',str(args.threads),'--parallel-dies','--geometry-tool',args.geometry_tool],
                    stdout=log,stderr=subprocess.STDOUT)
            passed=p.returncode==0
            if passed:
                with (out/(case+'.conductor-replay.log')).open('w') as log:
                    checked=subprocess.run([sys.executable,scripts/'native_joint.py',job/'output',
                                            directory,'--record'],stdout=log,stderr=subprocess.STDOUT)
                passed=checked.returncode==0
        row=dict(status='PASSED' if passed else 'FAILED',hbt_count=stats['metrics']['used_hbt'],
                 output_directory=str(directory))
        path=directory/'report.json'
        if path.exists():
            rep=json.loads(path.read_text())
            row['joint_connectivity']=rep.get('joint_connectivity')
            for side in ('top','bottom'):
                for key in ('drc','wirelength_um','via_count','wall_seconds','geometry'):
                    row[side+'_'+key]=rep[side].get(key)
            for key in ('drc','wirelength_um','via_count','wall_seconds'):
                values=[row.get(side+'_'+key) for side in ('top','bottom')]
                row['total_'+key]=sum(values) if None not in values else None
        return case,row
    records={}
    fields=['benchmark','status','hbt_count','top_drc','bottom_drc','total_drc','top_wirelength_um',
            'bottom_wirelength_um','total_wirelength_um','top_via_count','bottom_via_count','total_via_count',
            'top_wall_seconds','bottom_wall_seconds','total_wall_seconds','top_geometry','bottom_geometry',
            'joint_connectivity','output_directory']
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        tasks=[pool.submit(validate,case) for case in args.cases]
        for task in concurrent.futures.as_completed(tasks):
            case,record=task.result()
            records[case]=record
            with (out/'summary.tmp').open('w') as stream:
                w=csv.DictWriter(stream,fields,delimiter='\t')
                w.writeheader()
                for name in args.cases:
                    w.writerow(dict(benchmark=name,**records.get(name,dict(status='PENDING'))))
            (out/'summary.tmp').replace(out/'summary.tsv')
    return int(any(r['status']!='PASSED' for r in records.values()))

if __name__=='__main__':
    raise SystemExit(main())
