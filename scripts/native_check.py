"""Independently replay exported physical edges and check the two HBT endpoint records."""
import argparse
from collections import defaultdict
import csv
import json
import math
import re
from pathlib import Path
from native_detail import section


def check(config, directory):
    model = json.loads(config.read_text())
    stats = json.loads((directory/'routing_statistics.json').read_text())
    die = model['dies']
    sites = {s['id']:s for s in model['bonding']['sites']}
    components = [json.loads(s) for s in (directory/'components.jsonl').read_text().splitlines()]
    adjacency = defaultdict(lambda:defaultdict(set))
    net_edges = defaultdict(int)
    um = [0.,0.]
    vias = 0
    hbts = {}
    with (directory/'route_edges.tsv').open() as stream:
        for row in csv.DictReader(stream, delimiter='\t'):
            n = int(row['net_id'])
            a,b = [tuple(int(row[field+i]) for field in ('d','l','x','y')) for i in '01']
            if b in adjacency[n][a] or a==b:
                raise ValueError('Duplicate physical edge')
            adjacency[n][a].add(b)
            adjacency[n][b].add(a)
            net_edges[n] += 1
            if row['type']=='HBT':
                sid=int(row['site_id'])
                if a[0]==b[0] or sid in hbts:
                    raise ValueError('Invalid crossing or duplicate HBT owner')
                hbts[sid]=n
            elif row['type']=='VIA':
                if a[0]!=b[0] or a[2:]!=b[2:] or abs(a[1]-b[1])!=1:
                    raise ValueError('Invalid via')
                vias+=1
            elif row['type']=='WIRE' and a[:2]==b[:2] and sum(abs(a[i]-b[i]) for i in (2,3))==1:
                um[a[0]]+=float(row['wire_um'])
            else:
                raise ValueError('Invalid wire')
    for n,adj in adjacency.items():
        seen=set()
        stack=[next(iter(adj))]
        while stack:
            p=stack.pop()
            if p in seen:
                continue
            seen.add(p)
            stack.extend(adj[p]-seen)
        if len(seen)!=len(adj) or len(adj)!=net_edges[n]+1:
            raise ValueError('Disconnected or cyclic physical graph')
    with (directory/'hbt_assignment.tsv').open() as stream:
        rows=list(csv.DictReader(stream, delimiter='\t'))
    if {int(r['site_id']):int(r['net_id']) for r in rows}!=hbts or len(rows)!=len(hbts):
        raise ValueError('HBT tree / assignment mismatch')
    occupied=[]
    expected_landings=set()
    for row in rows:
        s=sites[int(row['site_id'])]
        xy=s['xy']
        if xy!=[int(row['bond_x']),int(row['bond_y'])] or [row['top_layer'],row['bottom_layer']] not in s['layers']:
            raise ValueError('Invalid landing pair or bonded XY')
        n=int(row['net_id'])
        if s.get('fixed_net',n)!=n or s['capacity']<1:
            raise ValueError('Invalid site ownership')
        array=model['bonding']
        if any((xy[i]-array['origin'][i])%array['pitch'][i] for i in range(2)):
            raise ValueError('HBT off lattice')
        if any(all(lo[i]<=xy[i]<=hi[i] for i in range(2)) for lo,hi in array['keepouts']):
            raise ValueError('Keepout conflict')
        if any(math.hypot(xy[0]-x,xy[1]-y)<array['min_spacing'] for x,y in occupied):
            raise ValueError('Spacing conflict')
        occupied.append(xy)
        for d,side in enumerate(['top','bottom']):
            ds=die[d]
            transform=ds['transform']
            pos=[int(row[side+'_x']),int(row[side+'_y'])]
            scale=model['bonded_dbu']//stats['inventory']['dies'][d]['dbu']
            if [transform['offset'][i]+transform['signs'][i]*scale*pos[transform['axes'][i]] for i in (0,1)]!=xy:
                raise ValueError('Landing transform mismatch')
            expected_landings.add((n,s['id'],d,row[side+'_component'],row[side+'_layer'],*pos))
    actual={(c['net_id'],h['id'],c['die'],c['name'],h['layer'],*h['xy']) for c in components for h in c['landings']}
    if actual!=expected_landings:
        raise ValueError('Component endpoint mismatch')
    for d,side in enumerate(['top','bottom']):
        own=[c for c in components if c['die']==d]
        guide=(directory/(side+'.guide')).read_text()
        if set(re.findall(r'^([^\s()]+)\n\(',guide,re.M))!={c['name'] for c in own}:
            raise ValueError('Missing component guide')
        original=section((config.parent/die[d]['def']).read_text(),'NETS')[0]
        physical={(name,*pin) for name,body in original.items() if not re.search(r'\+ USE (POWER|GROUND)\b',body)
                  for pin in re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)',body.partition('+')[0])}
        mapped={(m[side]['net'],'PIN',m[side]['cut_port']) for m in model['mapping'] if m[side].get('cut_port')}
        endpins=[(c['local_net'],*p) for c in own for p in c['pins']]
        if set(endpins)!=physical-mapped or len(set(endpins))!=len(endpins):
            raise ValueError('Original terminal identity mismatch')
    metrics=stats['metrics']
    if vias!=metrics['via_count'] or len(hbts)!=metrics['used_hbt'] or not math.isclose(sum(um),metrics['wirelength_um']):
        raise ValueError('Reported totals do not match routed edges')
    return dict(original_pins=stats['inventory']['real_pins'], via_count=vias, hbt_count=len(hbts),
                wirelength_um=sum(um), checked_components=len(components))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    print(json.dumps(check(args.manifest,args.output)))
