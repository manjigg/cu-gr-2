"""Exercise budget repair and independently count physical sites in real CLI output."""
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from native_check import check

binary=Path(sys.argv[1]).resolve()
base=root/'tests/native/clusters'
config=json.loads((base/'manifest.json').read_text())
for die in config['dies']:
    for key in ('lef','def'):
        die[key]=str(base/die[key])
config.update(routing_mode='fast',seed_seconds=20,stage_seconds=3,net_seconds=1,stages=[1,2,3],
              enable_parallel_maze=True,prefer_hbt_nonincrease=False,conflict_workers=1)
with tempfile.TemporaryDirectory() as tmp:
    for mode,limit in [('joint',None),('budgeted',1),('budgeted',0)]:
        folder=Path(tmp)/f'{mode}-{limit}'
        manifest=folder.with_suffix('.json')
        cfg=dict(config,optimization_mode=mode)
        if limit is not None:
            cfg['hbt_budget']=limit
        manifest.write_text(json.dumps(cfg))
        proc=subprocess.run([binary,'-native',manifest,'-output',folder,'-threads','2'],
                            stdout=subprocess.DEVNULL)
        stats=json.loads((folder/'routing_statistics.json').read_text())
        with (folder/'hbt_assignment.tsv').open() as source:
            count=len({int(r['site_id']) for r in csv.DictReader(source,delimiter='\t')})
        assert count==stats['metrics']['used_hbt']
        if limit==0:
            assert proc.returncode==2 and stats['budget_status']=='HEURISTIC_BUDGET_UNMET',stats
            assert stats['hbt_budget_excess']==count and stats['stages'][-1]['best_feasible'] is None
        else:
            assert proc.returncode==0,stats
            check(manifest,folder)
            if limit==1:
                assert count==1 and stats['budget_status']=='FEASIBLE',stats
            else:
                assert count==2,stats
