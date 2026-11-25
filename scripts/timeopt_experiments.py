"""Run independent native performance experiments with unchanged benchmark inputs and budgets."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from native_bench import CASES


FAMILIES={
    'ablation': {
        'no-resource-cache': {'enable_resource_cache':False},
        'no-path-cache': {'enable_path_cache':False},
        'no-hbt-index': {'enable_hbt_index':False},
        'no-hbt-bound': {'enable_hbt_pruning':False},
        'no-conflict': {'enable_conflict_optimizer':False},
        'no-pricing': {'enable_dynamic_pricing':False},
        'no-maze-window': {'enable_bounded_maze':False},
        'no-maze-workspace': {'enable_maze_workspace':False},
    },
    'parameter': {
        **{f'beam-{n}':{'beam_width':n} for n in (4,8,16,32)},
        **{f'hbt-{n}':{'hbt_candidate_limit':n} for n in (4,8,16,32)},
        **{f'local-{n}':{'local_exact_threshold':n} for n in (4,8,16)},
        **{f'seed-{order}':{'seed_order':order} for order in ('input','local_first','cross_first','hpwl','hbt_scarcity')},
        'allow-hbt-growth':{'prefer_hbt_nonincrease':False},
        'no-original-tie-order':{'preserve_seed_ties':False},
    },
    'parallel': {f'workers-{n}':{'enable_group_regeneration':True,'threads':n} for n in (1,2,4,8,16)},
    'repeats': {f'run-{n}':{} for n in (1,2,3)},
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','protocol','options','output'):
        parser.add_argument(name,type=Path)
    parser.add_argument('--family',choices=FAMILIES,required=True)
    parser.add_argument('--variants',nargs='+')
    parser.add_argument('--cases',choices=CASES,nargs='+',default=['aes','ethmac','ariane','bp'])
    a=parser.parse_args()
    defaults=json.loads(a.options.read_text())
    options=FAMILIES[a.family]
    names=a.variants or list(options)
    if any(n not in options for n in names):
        parser.error('Unknown variants for '+a.family)
    a.output.mkdir(parents=True,exist_ok=True)
    script=Path(__file__).with_name('native_bench.py')
    failed=False
    for name in names:
        candidate=dict(defaults,**options[name])
        threads=candidate.pop('threads',1)
        path=a.output/(name+'.json')
        with path.open('x') as stream:
            json.dump(candidate,stream,indent=2)
        with (a.output/(name+'.log')).open('x') as log:
            command=[sys.executable,script,a.binary,a.protocol,a.output/name,'--options',path,'--seed-seconds','1200',
                     '--stage-seconds','15','--master-seconds','15','--threads',str(threads),
                     '--profile','--cases',*a.cases]
            failed|=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT).returncode!=0
    return int(failed)


if __name__=='__main__':
    raise SystemExit(main())
