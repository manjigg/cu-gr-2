"""Measure full serial and batched Stage 3 from the same recorded physical state."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from new3d_regression import write_tables


def digest(path):
    sha=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):
            sha.update(chunk)
    return sha.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ['binary','checkpoints','output']:
        parser.add_argument(arg,type=Path)
    parser.add_argument('--cases',nargs='+',default=['aes','ethmac','ariane','bp'])
    parser.add_argument('--threads',nargs='+',type=int,default=[1,2,4,8,16,32])
    parser.add_argument('--repeat',type=int,default=3)
    parser.add_argument('--tasks',type=int,default=128)
    parser.add_argument('--checks',type=int,default=1000)
    parser.add_argument('--protocol',choices=['work','time'],required=True)
    a=parser.parse_args()
    if min([a.repeat,a.tasks,a.checks,*a.threads])<1:
        parser.error('All trial sizes must be positive')
    exe=a.binary.resolve()
    binary_hash=digest(exe)
    out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    rows=[]
    for case in a.cases:
        folder=a.checkpoints.resolve()/case
        frozen=folder/'output/stage3.checkpoint'
        checkpoint_hash=digest(frozen)
        original=json.loads((folder/'manifest.json').read_text())
        for repeat in range(a.repeat):
            order=[('P0',1)]+[(mode,n) for n in a.threads for mode in ('P1','P2')]
            if repeat%2:
                order.reverse()
            for mode,threads in order:
                original_job=out/case/f'{mode}-{threads}-{repeat}'
                job=original_job
                attempt=1
                while job.exists() and not (job/'experiment.json').is_file():
                    # Keep the interrupted DEF, guides and logs for diagnosis.
                    attempt+=1
                    job=original_job.with_name(original_job.name+f'-attempt{attempt}')
                cfg=dict(original,stages=[3],load_maze_checkpoint=str(frozen),save_maze_checkpoint=False,
                         enable_parallel_maze=mode!='P0',enable_conflict_aware_batching=mode=='P2',
                         enable_adaptive_parallelism=mode=='P2',conflict_workers=1)
                cfg['provenance']=dict(cfg['provenance'],requested_threads=threads,profile=False)
                if a.protocol=='work':
                    cfg.update(maze_trial_checks=a.checks,maze_trial_tasks=a.tasks,stage_seconds=3600,net_seconds=60)
                if job.exists():
                    evidence=json.loads((job/'experiment.json').read_text())
                    if (evidence['binary_sha256']!=binary_hash or evidence['checkpoint_sha256']!=checkpoint_hash
                            or cfg!=json.loads((job/'manifest.json').read_text())):
                        raise ValueError(f'Incompatible prior trial {job}')
                else:
                    job.mkdir(parents=True)
                    manifest=job/'manifest.json'
                    manifest.write_text(json.dumps(cfg,separators=(',',':')))
                    args=['/usr/bin/time','-f','%M','-o',str(job/'rss.kb'),str(exe),'-native',str(manifest),
                          '-output',str(job/'output'),'-threads',str(threads)]
                    begin=time.monotonic()
                    env=os.environ.copy();env.pop('CUGR_NATIVE_PROFILE',None)
                    with (job/'router.log').open('w') as log:
                        proc=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,env=env,cwd=job)
                    evidence=dict(binary_sha256=binary_hash,checkpoint_sha256=checkpoint_hash,command=args,
                                  exit_code=proc.returncode,wall_seconds=time.monotonic()-begin)
                    (job/'experiment.json').write_text(json.dumps(evidence,indent=2)+'\n')
                row=dict(benchmark=case,mode=mode,threads=threads,repeat=repeat,protocol=a.protocol,
                         exit_code=evidence['exit_code'],wall_seconds=evidence['wall_seconds'],path=str(job))
                rss=(job/'rss.kb').read_text().splitlines()
                row['peak_rss_kb']=int(rss[-1]) if rss and rss[-1].isdigit() else None
                stats_path=job/'output/routing_statistics.json'
                if stats_path.exists():
                    stats=json.loads(stats_path.read_text())
                    s=stats['stages'][0]
                    row.update(stage_seconds=s['seconds'],tasks=s['nets_attempted'],accepted=s['accepted_nets'],
                               objective=s['best_feasible'],wire_um=s['wirelength_um'],vias=s['via_count'],hbt=s['used_hbt'],
                               status=stats['status'],pool_size=s['pool_size'])
                    row.update(s.get('parallel_maze') or s.get('serial_maze') or {})
                    row['actual_workers']=row.get('actual_workers',1)
                    row['candidate_acceptance_per_task']=s['accepted_nets']/s['nets_attempted'] if s['nets_attempted'] else None
                rows.append(row)
                write_tables(out,'trials',list(dict.fromkeys(k for r in rows for k in r)),rows)
    return int(any(r['exit_code'] for r in rows))


if __name__=='__main__':
    raise SystemExit(main())
