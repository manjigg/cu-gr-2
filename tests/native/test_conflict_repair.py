"""Exercise shared pads and cumulative competition for HBT landing resources."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

base=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp)
    cfg=json.loads((base/'dual.json').read_text())
    for i,d in enumerate(cfg['dies']):
        src=(base/d['def']).read_text()
        lines=[line.replace('LAYER metal2','LAYER metal3') if line.startswith('- B + NET B ') else line
               for line in src.splitlines()]
        dest=folder/f'{i}.def'
        dest.write_text('\n'.join(lines)+'\n')
        d.update(lef=str((base/d['lef']).resolve()),via_capacity=0)
        d['def']=str(dest)
    cfg['bonding']['sites'][1]['layers']=[['metal2','metal2']]
    cfg.update(routing_mode='fast',enable_failure_driven_repair=True,enable_iterative_resource_pricing=True)
    path=folder/'manifest.json'
    path.write_text(json.dumps(cfg))
    subprocess.run([sys.argv[1],path],check=True)

    cfg=json.loads((base/'dual.json').read_text())
    for i,die in enumerate(cfg['dies']):
        src=(base/die['def']).read_text()
        lines=[]
        for line in src.splitlines():
            lines.append({'PINS 2 ;':'PINS 3 ;','NETS 2 ;':'NETS 3 ;'}.get(line,line))
            if line.startswith('- B + NET B '):
                lines.append(line.replace('- B + NET B ','- C + NET C ',1))
            if line.startswith('- B ( PIN B ) '):
                lines.append(line.replace('- B ( PIN B ) ','- C ( PIN C ) ',1))
        dest=folder/f'accumulated-{i}.def'
        dest.write_text('\n'.join(lines)+'\n')
        die.update(lef=str((base/die['lef']).resolve()),via_capacity=2)
        die['def']=str(dest)
    cfg['mapping'].append({d:{'net':'C'} for d in ('top','bottom')})
    cfg['bonding']['sites'].append(dict(id=3,xy=[3100,1500],layers=[['metal2','metal2']],capacity=1))
    # All three metal-2 landings share the same gcell and lower-die via resource.
    # Reduce pitch only in this synthetic fixture to introduce another distinct site.
    cfg['bonding']['pitch']=[200,1000]
    cfg['bonding']['min_spacing']=200
    cfg['bonding']['transition_footprints']=dict(source='synthetic three-way capacity test',rules=[
        dict(layers=['metal2','metal2'],reservations=[
            dict(die=1,offset=[0,0],layer_offset=0,kind='VIA',demand=0.75)]),
        dict(layers=['metal3','metal3'],reservations=[])])
    cfg.update(routing_mode='fast',enable_failure_driven_repair=True,enable_hbt_footprint=True)
    path=folder/'cumulative.json'
    path.write_text(json.dumps(cfg))
    subprocess.run([sys.argv[1],path],check=True)
