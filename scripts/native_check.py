"""Independently replay exported physical edges and check the two HBT endpoint records."""
import argparse
import bisect
from collections import defaultdict
import csv
import json
import math
import re
from pathlib import Path
from native_detail import section, resolve_instances


def routing_geometry(manifest, die):
    text=(manifest.parent/die['def']).read_text()
    tech=(manifest.parent/die['lef']).read_text()
    layers=[]
    for header in re.finditer(r'^LAYER (\S+)\s+TYPE ROUTING\s*;',tech,re.M):
        end=re.search(r'^END '+re.escape(header[1])+r'\s*$',tech[header.end():],re.M)
        rule=tech[header.end():header.end()+end.start()]
        layers.append((header[1],{'HORIZONTAL':0,'VERTICAL':1}[re.search(r'DIRECTION (\S+) ;',rule)[1]]))
    area=list(map(int,re.search(r'DIEAREA \( (-?\d+) (-?\d+) \) \( (-?\d+) (-?\d+) \)',text).groups()))
    lines=[[],[]]
    for axis,origin,n,step in re.findall(r'^GCELLGRID ([XY]) (-?\d+) DO (\d+) STEP (\d+) ;',text,re.M):
        lines['XY'.index(axis)].extend(int(origin)+i*int(step) for i in range(1,int(n)))
    has_grid=bool(re.search(r'^GCELLGRID ',text,re.M))
    for axis in [0,1]:
        lo,hi=area[axis],area[axis+2]
        if has_grid:
            lines[axis]=sorted([lo]+lines[axis])
        else:
            lines[axis]=list(range(lo,hi-3000,3000)) or [lo]
        if lines[axis][-1]!=hi:
            lines[axis].append(hi)
    centers=[[int((a+b)/2) for a,b in zip(coords,coords[1:])] for coords in lines]
    dbu=int(re.search(r'UNITS DISTANCE MICRONS (\d+) ;',text)[1])
    return layers,lines,centers,dbu


def check(config, directory):
    model = json.loads(config.read_text())
    stats = json.loads((directory/'routing_statistics.json').read_text())
    die = model['dies']
    geometries=[routing_geometry(config,d) for d in die]
    sites = {s['id']:s for s in model['bonding']['sites']}
    components = [json.loads(s) for s in (directory/'components.jsonl').read_text().splitlines()]
    adjacency = defaultdict(lambda:defaultdict(set))
    net_edges = defaultdict(int)
    um = [0.,0.]
    vias = 0
    hbts = {}
    endpoints = {}
    with (directory/'route_edges.tsv').open() as stream:
        for row in csv.DictReader(stream, delimiter='\t'):
            n = int(row['net_id'])
            a,b = [tuple(int(row[field+i]) for field in ('d','l','x','y')) for i in '01']
            for d,l,x,y in (a,b):
                if d not in (0,1) or not (0<=l<len(geometries[d][0]) and
                        0<=x<len(geometries[d][2][0]) and 0<=y<len(geometries[d][2][1])):
                    raise ValueError('Route vertex outside physical grid')
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
                endpoints[sid]={a[0]:a,b[0]:b}
            elif row['type']=='VIA':
                if a[0]!=b[0] or a[2:]!=b[2:] or abs(a[1]-b[1])!=1:
                    raise ValueError('Invalid via')
                vias+=1
            elif row['type']=='WIRE' and a[:2]==b[:2] and sum(abs(a[i]-b[i]) for i in (2,3))==1:
                d,l,x,y=a
                axis=geometries[d][0][l][1]
                if a[3-axis]!=b[3-axis]:
                    raise ValueError('Wrong preferred wire direction')
                centers=geometries[d][2][axis]
                physical=abs(centers[a[2+axis]]-centers[b[2+axis]])/geometries[d][3]
                if not math.isclose(physical,float(row['wire_um'])):
                    raise ValueError('Recorded edge length disagrees with DEF cell centers')
                um[d]+=physical
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
    occupied=defaultdict(list)
    expected_landings=set()
    array=model['bonding']
    spacing=array['min_spacing']
    for row in rows:
        s=sites[int(row['site_id'])]
        xy=s['xy']
        if xy!=[int(row['bond_x']),int(row['bond_y'])] or [row['top_layer'],row['bottom_layer']] not in s['layers']:
            raise ValueError('Invalid landing pair or bonded XY')
        n=int(row['net_id'])
        if s.get('fixed_net',n)!=n or s['capacity']<1:
            raise ValueError('Invalid site ownership')
        if any((xy[i]-array['origin'][i])%array['pitch'][i] for i in range(2)):
            raise ValueError('HBT off lattice')
        if any(all(lo[i]<=xy[i]<=hi[i] for i in range(2)) for lo,hi in array['keepouts']):
            raise ValueError('Keepout conflict')
        # 间距范围内的两个站点最多相隔一个桶，按封装 DBU 的整数平方距离核对。
        if spacing>0:
            bx,by=(v//spacing for v in xy)
            for i in range(bx-1,bx+2):
                for j in range(by-1,by+2):
                    if any((xy[0]-x)**2+(xy[1]-y)**2<spacing**2 for x,y in occupied.get((i,j),())):
                        raise ValueError('Spacing conflict')
            occupied[bx,by].append(xy)
        for d,side in enumerate(['top','bottom']):
            ds=die[d]
            transform=ds['transform']
            pos=[int(row[side+'_x']),int(row[side+'_y'])]
            scale=model['bonded_dbu']//stats['inventory']['dies'][d]['dbu']
            if [transform['offset'][i]+transform['signs'][i]*scale*pos[transform['axes'][i]] for i in (0,1)]!=xy:
                raise ValueError('Landing transform mismatch')
            layers,lines,_,_=geometries[d]
            _,layer,x,y=endpoints[s['id']][d]
            cell=[min(len(lines[i])-2,bisect.bisect_right(lines[i],pos[i])-1) for i in (0,1)]
            if [x,y]!=cell or layers[layer][0]!=row[side+'_layer']:
                raise ValueError('HBT edge misses physical pad cell or layer')
            expected_landings.add((n,s['id'],d,row[side+'_component'],row[side+'_layer'],*pos))
    actual={(c['net_id'],h['id'],c['die'],c['name'],h['layer'],*h['xy']) for c in components for h in c['landings']}
    if actual!=expected_landings:
        raise ValueError('Component endpoint mismatch')
    real_pins=0
    for d,side in enumerate(['top','bottom']):
        own=[c for c in components if c['die']==d]
        guide=(directory/(side+'.guide')).read_text()
        if set(re.findall(r'^([^\s()]+)\n\(',guide,re.M))!={c['name'] for c in own}:
            raise ValueError('Missing component guide')
        source=(config.parent/die[d]['def']).read_text()
        own=list(resolve_instances(source,own))
        original=section(source,'NETS')[0]
        physical={(name,*pin) for name,body in original.items() if not re.search(r'\+ USE (POWER|GROUND)\b',body)
                  for pin in re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)',body.partition('+')[0])}
        mapped={(m[side]['net'],'PIN',m[side]['cut_port']) for m in model['mapping'] if m[side].get('cut_port')}
        endpins=[(c['local_net'],*p) for c in own for p in c['pins']]
        if set(endpins)!=physical-mapped or len(set(endpins))!=len(endpins):
            raise ValueError('Original terminal identity mismatch')
        real_pins+=len(endpins)
    if real_pins!=stats['inventory']['real_pins']:
        raise ValueError('Inventory disagrees with the original DEF terminal count')
    metrics=stats['metrics']
    if vias!=metrics['via_count'] or len(hbts)!=metrics['used_hbt'] or not math.isclose(sum(um),metrics['wirelength_um']):
        raise ValueError('Reported totals do not match routed edges')
    return dict(original_pins=real_pins, via_count=vias, hbt_count=len(hbts),
                wirelength_um=sum(um), checked_components=len(components))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    print(json.dumps(check(args.manifest,args.output)))
