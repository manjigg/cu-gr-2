"""End-to-end feasibility, sensitivity and commit/guide regressions on physical DEF inputs."""
from pathlib import Path
import copy
import json
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from native_check import check

binary=Path(sys.argv[1]).resolve()
base=root/'tests/native/clusters'
template=json.loads((base/'manifest.json').read_text())
for d in template['dies']:
    for field in ['lef','def']:
        d[field]=str((base/d[field]).resolve())


def run(cfg,dest,success=True):
    dest.mkdir()
    manifest=dest/'manifest.json'
    manifest.write_text(json.dumps(cfg))
    output=dest/'out'
    with (dest/'router.log').open('w') as log:
        p=subprocess.run([binary,'-native',manifest,'-output',output],stdout=log,stderr=subprocess.STDOUT)
    stats=json.loads((output/'routing_statistics.json').read_text())
    if success:
        assert p.returncode==0 and stats['status']=='OPTIMAL_CANDIDATE_POOL',stats
        check(manifest,output)
        assert stats['stages'][-1]['gap']==0
    else:
        assert p.returncode==2 and stats['status']=='INFEASIBLE_CANDIDATE_POOL' and stats['metrics'] is None
    return stats


def regress(out):
    table=[]
    for label,w,v,h in [('wire',1,0,0),('via',0,1,0),('hbt',0,0,1),
                        ('weighted1',1,1,1),('weighted4',1,1,4),('weighted5',1,1,5),('weighted12',1,1,12)]:
        cfg=copy.deepcopy(template)
        cfg['weights'].update(wire_um=w,via=v,hbt=h)
        rep=run(cfg,out/label)
        m=rep['metrics']
        table.append((label,w,v,h,m['wirelength_um'],m['via_count'],m['used_hbt'],rep['stages'][-1]['best_feasible']))
    assert table[3][6]==2 and table[-1][6]==1 and table[3][4]<table[-1][4]
    cfg=copy.deepcopy(template)
    cfg['bonding']['sites'][0]['fixed_net']=0
    run(cfg,out/'fixed')
    cfg=copy.deepcopy(template)
    cfg['bonding']['sites']=[]
    run(cfg,out/'no-site',False)
    cfg=copy.deepcopy(template)
    cfg['mapping']=[]
    cfg['bonding']['sites']=[]
    assert run(cfg,out/'local')['metrics']['used_hbt']==0
    cfg=copy.deepcopy(template)
    cfg['mapping']=[]
    cfg['same_die_maze_detour']=True
    blocked=out/'cut.def'
    blocked.write_text((base/'top.def').read_text().replace('PINS 2 ;',
        'BLOCKAGES 1 ;\n- LAYER metal3 RECT ( 3900 0 ) ( 4500 12600 ) ;\nEND BLOCKAGES\nPINS 2 ;'))
    cfg['dies'][0]['def']=str(blocked.resolve())
    detour=run(cfg,out/'detour')
    assert detour['stages'][0]['solver_status']=='INFEASIBLE',detour
    assert detour['stages'][1]['solver_status']=='OPTIMAL' and detour['metrics']['used_hbt']==2,detour
    cfg['stages']=[3]
    maze=run(cfg,out/'maze-only')
    assert maze['metrics']['used_hbt']==2 and maze['stages'][0]['stage']==3
    certificate=out/'no-site'/'reachability.json'
    proc=subprocess.run([binary,'-check-native',out/'no-site'/'manifest.json','-output',certificate],
                        stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    assert proc.returncode==2 and len(json.loads(certificate.read_text())['disconnected'])==1
    import csv
    with (out/'sensitivity.tsv').open('w') as f:
        writer=csv.writer(f,delimiter='\t')
        writer.writerow('mode wire_weight via_weight hbt_weight wirelength_um via_count used_hbt objective'.split())
        writer.writerows(table)


if len(sys.argv)==3:
    output=Path(sys.argv[2])
    output.mkdir(parents=True,exist_ok=False)
    regress(output)
else:
    with tempfile.TemporaryDirectory() as work:
        regress(Path(work))
