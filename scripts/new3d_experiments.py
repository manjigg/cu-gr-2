"""Run supplementary V3 experiments serially, keeping every case and its command record."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from native_bench import CASES

ROOT = Path(__file__).resolve().parents[1]
G = ROOT / 'result-3/new3D'
X = G / '_experiments'
REF = ROOT / 'tests/native/benchmark-array.json'
FINAL = ROOT / 'tests/native/new3d-options.json'
SERIAL = ROOT / 'tests/native/new3d-baseline-options.json'
GUIDED = ROOT / 'tests/native/new3d-guided-options.json'
FROZEN = X / 'module_regression/route-memory'
LARGE = ['aes', 'ethmac', 'ariane', 'bp']


def sha(path):
    with path.open('rb') as stream:
        h = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1048576), b''):
            h.update(chunk)
        return h.hexdigest()


def read(path):
    return json.loads(path.read_text())


def save(path, data):
    text = json.dumps(data, indent=2) + '\n'
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(text)
    temp.replace(path)


def record(label, directory, cmd, inputs):
    # An interrupted run keeps its original evidence. A fresh attempt gets a new directory.
    directory.mkdir(parents=True, exist_ok=True)
    signature = {'command': list(map(str, cmd)), 'input_sha256': {str(p): sha(p) for p in inputs}}
    result = directory / 'experiment.json'
    if result.exists():
        old = read(result)
        if old['signature'] != signature:
            raise ValueError('Different protocol in ' + str(directory))
        return old['exit_code']
    if (directory / 'started.json').exists():
        raise RuntimeError('Unfinished experiment preserved at ' + str(directory))
    save(directory/'started.json', signature)
    start = time.monotonic()
    with (directory / 'experiment.log').open('x') as log:
        code = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
    save(result, dict(label=label, signature=signature, wall_seconds=time.monotonic()-start, exit_code=code))
    print(label, code, flush=True)
    return code


def bench(directory, binary, options, cases, threads, profile=False, **overrides):
    directory.mkdir(parents=True, exist_ok=True)
    config = dict(read(options), **overrides)
    path = directory / 'options.json'
    if path.exists() and read(path) != config:
        raise ValueError('Changed options in ' + str(directory))
    if not path.exists():
        save(path, config)
    cmd = [sys.executable, ROOT/'scripts/native_bench.py', binary, REF, directory/'runs', '--options', path,
           '--threads', str(threads), '--jobs', '1', '--stage-seconds', '15', '--seed-seconds', '1200',
           '--master-seconds', '15', '--cases', *cases]
    if profile:
        cmd.append('--profile')
    code=record(directory.name, directory, cmd, [binary, path, REF])
    if code and not all((directory/'runs'/c/'process.json').is_file() and
            read(directory/'runs'/c/'process.json')['status'] in ('FEASIBLE_FAST','ROUTED_BUDGET_UNMET')
            for c in cases):
        raise RuntimeError('Unexpected routing failure, see '+str(directory))
    return code


def experiments(suite):
    if suite == 'repeats':
        # First observations are the original frozen and formal seven-design runs.
        for i, order in [(2, ['new', 'old']), (3, ['old','new'])]:
            for name in order:
                old = name == 'old'
                bench(X/'repeats'/f'{name}-r{i}', X/'baseline/route' if old else FROZEN,
                      ROOT/'tests/native/timeopt-options.json' if old else FINAL, CASES, 1 if old else 4)
        bench(X/'ablation/A9-no-maze', FROZEN, FINAL, CASES, 4, enable_parallel_maze=False,
              enable_conflict_aware_batching=False, enable_adaptive_parallelism=False,
              enable_maze_memory_budget=False)
    elif suite == 'budget':
        # Floor integer arithmetic. Keep unsuccessful budgets and all physically routed trees.
        for percent in [100, 90, 110, 120]:
            for case in CASES:
                base = read(X/'baseline/global'/case/'ledger-replay.json')['hbt_count']
                bench(X/'pareto'/f'{percent}pct'/case, FROZEN, SERIAL, [case], 1,
                      optimization_mode='budgeted', prefer_hbt_nonincrease=False, hbt_budget=base*percent//100)
    elif suite == 'sensitivity':
        for k in (2,4,8,16,32):
            bench(X/'parameter_sweep'/f'beam-{k}',FROZEN,SERIAL,['aes','ethmac'],1,beam_width=k,
                  enable_adaptive_beam=True)
        for k in (4,8,16,32,64):
            bench(X/'parameter_sweep'/f'hbt-{k}',FROZEN,SERIAL,['aes','ethmac'],1,hbt_candidate_limit=k,
                  enable_progressive_hbt_search=True)
        params = [('batch-1',{'maze_batch_factor':1}),('batch-4',{'maze_batch_factor':4}),
                  ('low-contention-threshold',{'maze_conflict_low':0.025,'maze_conflict_high':0.1}),
                  ('high-contention-threshold',{'maze_conflict_low':0.1,'maze_conflict_high':0.4}),
                  ('retry-0',{'maze_max_retries':0}),('retry-4',{'maze_max_retries':4}),
                  ('net-015',{'net_seconds':0.015}),('net-060',{'net_seconds':0.06})]
        for label,opts in params:
            bench(X/'parameter_sweep'/label,FROZEN,GUIDED,['aes','ethmac'],4,**opts)
        for k in [2,4,8]:
            bench(X/'parameter_sweep'/f'local-{k}',FROZEN,X/'ablation/options/A4-topology.json',
                  ['aes','ethmac'],1,topology_attempt_limit=k)
    elif suite == 'detail-ablation':
        # A3/A4 differ only by the local topology refinement options.
        detail_root=ROOT/'result-3/new3Ddetail/_experiments/ablation'
        openroad=Path('/home/wanghh/h3d/OpenROAD/bazel-bin/openroad')
        geom=Path('/home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002')
        cohorts=[(name,X/'ablation'/name,['aes']) for name in ('A3-joint','A4-topology')]
        cohorts.append(('budget-90-mor1kx',X/'pareto/90pct/mor1kx/runs',['mor1kx']))
        for label,global_dir,cases in cohorts:
            directory=detail_root/(label+'-trial')
            files=[openroad,geom/'audit.py',geom/'export_geometry.tcl']
            files.extend(global_dir/case/'manifest.json' for case in cases)
            files.extend(global_dir/case/'output'/name for case in cases
                         for name in ('top.guide','bottom.guide','hbt_assignment.tsv','components.jsonl'))
            record(label,directory,[sys.executable,ROOT/'scripts/native_detail_batch.py',openroad,
                global_dir,directory/'runs',geom,'--threads','8','--jobs','1','--cases',*cases],files)
    elif suite == 'profile':
        binary=X/'module_regression/label-memory-build/route'
        bench(X/'profile/v3',binary,FINAL,CASES,4,profile=True)
        bench(X/'profile/dp-reference',binary,SERIAL,LARGE,1,profile=True)
        bench(X/'profile/incremental-dp',binary,SERIAL,LARGE,1,profile=True,enable_incremental_dp=True)
        bench(X/'profile/progressive-hbt',binary,SERIAL,LARGE,1,profile=True,enable_progressive_hbt_search=True)
    elif suite == 'topology-validation':
        binary=X/'module_regression/label-memory-build/route'
        for label in ('A3-joint','A4-topology'):
            bench(X/'ablation'/f'{label}-v2',binary,X/'ablation/options'/(label+'.json'),LARGE,1)
    elif suite == 'correctness':
        for label in ('seed-ref','seed-prim'):
            bench(X/'correctness'/('bp-fixed-'+label), FROZEN,X/'ablation/options'/(label+'.json'),
                  ['bp'],1,seed_site_seconds=0.)
        binary = ROOT/'run-new3d/route'
        prior = ROOT/'results-3/native-3d/M0/summary.tsv'
        directory = X/'correctness/final-2d'
        if record('final-2d',directory,[sys.executable,ROOT/'scripts/native_2d_regression.py',
                  binary,prior,directory/'runs','--jobs','1'],[binary,prior]):
            raise RuntimeError('2D regression failed in '+str(directory))
        directory = X/'h3d_reference'
        binary = Path('/home/wanghh/h3d/h3d_zyx/install/bin/route')
        code=record('H3D',directory,[sys.executable,ROOT/'scripts/timeopt_h3d_reference.py',
                    binary,REF,directory/'runs'],[binary,REF])
        if code or any(read(directory/'runs'/case/'process.json')['exit_code'] for case in CASES):
            raise RuntimeError('H3D reference failed in '+str(directory))
    elif suite in ('work','time'):
        binary = FROZEN
        dest = X/('fixed_workload' if suite=='work' else 'fixed_time')/'transaction-r1'
        checkpoints = X/'fixed_workload/checkpoints'
        # The child validates its input hashes and successful records when resumed.
        cmd = [sys.executable,ROOT/'scripts/new3d_checkpoint_trials.py',binary,checkpoints,dest,'--protocol',suite]
        code = subprocess.run(cmd,cwd=ROOT).returncode
        print('checkpoint',suite,code,flush=True)
        if code:
            raise RuntimeError('Checkpoint experiments failed in '+str(dest))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suites',nargs='+',choices=['repeats','correctness','budget','work','time','sensitivity',
                                                  'profile','topology-validation','detail-ablation'])
    parser.add_argument('--after-detail',action='store_true',
                        help='Wait until all seven final detailed cases reach a terminal status to isolate timing')
    args=parser.parse_args()
    if args.after_detail:
        summary=ROOT/'result-3/new3Ddetail/final/summary.tsv'
        while True:
            if summary.is_file():
                with summary.open() as f:
                    rows=list(csv.DictReader(f,delimiter='\t'))
                if {r['benchmark'] for r in rows}==set(CASES) and all(r['status']!='PENDING' for r in rows):
                    break
            time.sleep(15)
    failures={}
    for suite in args.suites:
        try:
            experiments(suite)
        except (ValueError, RuntimeError, OSError) as error:
            failures[suite]=str(error)
            print(f'{suite}: {error}',file=sys.stderr,flush=True)
    if failures:
        raise SystemExit(1)


if __name__=='__main__':
    main()
