"""Run the physical-site exchange and a separate pair coupled only by HBT budget."""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

base=Path(__file__).resolve().parent
binary=Path(sys.argv[1]).resolve()
subprocess.run([binary,base/'dual.json'],check=True)

with TemporaryDirectory() as scratch:
    root=Path(scratch)
    fixture=base/'clusters'
    cfg=json.loads((fixture/'manifest.json').read_text())
    for i,die in enumerate(cfg['dies']):
        source=(fixture/die['def']).read_text()
        prefix,body=source.split('PINS 2 ;',1)
        pins=[line for line in body.split('END PINS')[0].splitlines() if line.startswith('- ')]
        new=[line.replace('- ',f'- {net}',1).replace('NET N ',f'NET {net} ') for net in ('A','B') for line in pins]
        nets=[f'- {net} ( PIN {net}L ) ( PIN {net}R ) + USE SIGNAL ;' for net in ('A','B')]
        out=root/f'die{i}.def'
        out.write_text(prefix+'PINS 4 ;\n'+'\n'.join(new)+'\nEND PINS\nNETS 2 ;\n'+'\n'.join(nets)+'\nEND NETS\nEND DESIGN\n')
        die['def']=str(out)
        die['lef']=str(fixture/die['lef'])
    cfg['mapping']=[{side:{'net':net} for side in ('top','bottom')} for net in ('A','B')]
    cfg['bonding'].update(pitch=[1000,1000],min_spacing=1000,sites=[
        dict(id=i,xy=xy,capacity=1,layers=[['metal3','metal3']]) for i,xy in
        enumerate([[2100,2100],[10100,2100],[3100,3100],[11100,3100]])])
    cfg['weights'].update(via=0,hbt=0)
    cfg.update(optimization_mode='budgeted',hbt_budget=3,budget_fixture=True)
    manifest=root/'budget.json'
    manifest.write_text(json.dumps(cfg))
    subprocess.run([binary,manifest],check=True)
