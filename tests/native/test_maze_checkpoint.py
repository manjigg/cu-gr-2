"""Round-trip routes, ownership, work order and prices before Stage 3."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

base=Path(__file__).resolve().parent
binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    cfg=json.loads((base/'dual.json').read_text())
    for die in cfg['dies']:
        for key in ('lef','def'):
            die[key]=str((base/die[key]).resolve())
    cfg.update(routing_mode='fast',stages=[1,3],save_maze_checkpoint=True)
    def run(name,success=True):
        file=root/(name+'.json')
        file.write_text(json.dumps(cfg))
        p=subprocess.run([binary,'-native',file,'-output',root/name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        assert (p.returncode==0)==success,(name,p.returncode)
    run('create')
    cfg.update(stages=[3],save_maze_checkpoint=False,load_maze_checkpoint=str(root/'create/stage3.checkpoint'))
    run('replay')
    assert (root/'create/route_edges.tsv').read_bytes()==(root/'replay/route_edges.tsv').read_bytes()
    cfg['bonding']['min_spacing']+=100
    run('wrong-technology',False)
