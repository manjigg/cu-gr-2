"""Replay one Stage 2 checkpoint through the unchanged and coordinated paths."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

base=Path(__file__).resolve().parent
sys.path.insert(0,str(base.parents[1]/'scripts'))
from native_check import check

binary=Path(sys.argv[1]).resolve()
with TemporaryDirectory() as scratch:
    folder=Path(scratch)
    cfg=json.loads((base/'dual.json').read_text())
    for die in cfg['dies']:
        for key in ('lef','def'):
            die[key]=str((base/die[key]).resolve())
    cfg.update(routing_mode='fast',seed_seconds=8,stage_seconds=5,net_seconds=0.3,
               master_seconds=2,stages=[1,2],save_stage2_checkpoint=True)

    def run(name,settings):
        config=folder/(name+'.json')
        output=folder/name
        config.write_text(json.dumps(settings))
        subprocess.run([binary,'-native',config,'-output',output],check=True,stdout=subprocess.DEVNULL)
        assert check(config,output)['hbt_count'] >= 2
        return output

    first=run('initial',cfg)
    snapshot=first/'stage2.checkpoint'
    original=hashlib.sha256(snapshot.read_bytes()).hexdigest()
    cfg.update(stages=[2],save_stage2_checkpoint=False,load_stage2_checkpoint=str(snapshot))
    reference=run('reference',cfg)
    assert (first/'route_edges.tsv').read_bytes()==(reference/'route_edges.tsv').read_bytes()
    cfg.update(optimization_mode='joint',prefer_hbt_nonincrease=False,
               enable_topology_coordination=True,enable_topology_archive=True,
               enable_topology_exchange=True,enable_topology_group_regeneration=True,
               enable_topology_safe_pruning=True,enable_topology_refinement=True,
               enable_hbt_augmentation=True,enable_topology_rotation=True)
    improved=run('joint',cfg)
    stats=json.loads((improved/'routing_statistics.json').read_text())
    assert 'topology_exchange' in stats['stages'][0]
    assert len(stats['stages'])==1 and stats['stages'][0]['stage']==2
    assert stats['stages'][0]['topology_exchange']['blocked_resources']>0
    assert hashlib.sha256(snapshot.read_bytes()).hexdigest()==original
