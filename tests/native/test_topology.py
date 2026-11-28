"""Verify local topology operations on the original clusters and a forced junction migration."""
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import subprocess
import sys

BASE=Path(__file__).resolve().parent
BINARY=Path(sys.argv[1]).resolve()
subprocess.run([BINARY,BASE/'clusters/manifest.json'],check=True)

with TemporaryDirectory() as name:
    folder=Path(name)
    cfg=json.loads((BASE/'dual.json').read_text())
    top=folder/'top.def'
    prefix=(BASE/'top.def').read_text().split('PINS 2 ;')[0]
    pins=[]
    for label,layer,x,y in [('L',5,2100,6300),('R',5,10500,6300),('T',4,6300,2100)]:
        pins.append(f'- {label} + NET N + DIRECTION INPUT + USE SIGNAL + LAYER metal{layer} ( -20 -20 ) ( 20 20 ) + FIXED ( {x} {y} ) N ;')
    top.write_text(prefix+'PINS 3 ;\n'+'\n'.join(pins)+
                   '\nEND PINS\nNETS 1 ;\n- N ( PIN L ) ( PIN R ) ( PIN T ) + USE SIGNAL ;\nEND NETS\nEND DESIGN\n')
    for die in cfg['dies']:
        for key in ('lef','def'):
            die[key]=str((BASE/die[key]).resolve())
    cfg['dies'][0]['def']=str(top)
    cfg.update(mapping=[],migration_fixture=True,enable_hbt_merge=False,enable_hbt_relocation=False,
               enable_steiner_migration=True)
    manifest=folder/'manifest.json'
    manifest.write_text(json.dumps(cfg))
    subprocess.run([BINARY,manifest],check=True)
