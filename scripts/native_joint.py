"""Join measured OpenDB conductor components using the two endpoints of each physical HBT."""
import argparse
import csv
import gzip
import hashlib
import json
from pathlib import Path
import re


def geometry_directory(detail,side):
    folder=detail/side
    recheck=folder/'geometry-recheck'/side
    return recheck if recheck.is_dir() else folder


def verify_routes(global_dir, detail):
    inventory=json.loads((global_dir/'routing_statistics.json').read_text())['inventory']
    with (global_dir/'hbt_assignment.tsv').open() as stream:
        assignments={}
        for row in csv.DictReader(stream,delimiter='\t'):
            site=int(row['site_id'])
            if site in assignments:
                raise ValueError('Duplicate physical HBT '+str(site))
            assignments[site]=row
    physical_roots={}
    owners={}
    observed_pins=0
    for die,side in enumerate(('top','bottom')):
        folder=detail/side
        source=geometry_directory(detail,side)
        audit=json.loads((source/'geometry_audit.json').read_text())
        digest=hashlib.sha256()
        with (folder/'routed.def').open('rb') as stream:
            for chunk in iter(lambda:stream.read(1048576),b''):
                digest.update(chunk)
        if audit['status']!='passed' or digest.hexdigest()!=audit['routed_def_sha256']:
            raise ValueError('Missing or stale conductor audit on '+side)
        pins={}
        with (global_dir/'components.jsonl').open() as stream:
            for line in stream:
                component=json.loads(line)
                if component['die']!=die:
                    continue
                name=component['name']
                key=(die,name)
                if key in owners:
                    raise ValueError('Duplicate local component '+name)
                owners[key]=component['net_id']
                pins[name]={tuple(p) for p in component['pins']}
                if len(pins[name])!=len(component['pins']):
                    raise ValueError('Repeated original terminal on '+name)
                observed_pins+=len(pins[name])
        seen_sites=set()
        for pad in json.loads((folder/'physical_ports.json').read_text()):
            site=pad['site']
            row=assignments[site]
            name=pad['component']
            if (site in seen_sites or name!=row[side+'_component'] or
                    pad['net']!=int(row['net_id']) or owners[die,name]!=pad['net'] or
                    (pad['layer'],pad['x'],pad['y'])!=(row[side+'_layer'],int(row[side+'_x']),int(row[side+'_y']))):
                raise ValueError('Physical HBT landing mismatch on '+side)
            seen_sites.add(site)
            terminal=('PIN',pad['port'])
            if terminal in pins[name]:
                raise ValueError('Repeated physical landing terminal')
            pins[name].add(terminal)
        if seen_sites!=assignments.keys():
            raise ValueError('Incomplete physical HBT endpoints on '+side)
        with gzip.open(source/'terminal_components.jsonl.gz','rt') as stream:
            for line in stream:
                name,inst,pin,root=json.loads(line)
                pair=(inst,pin)
                if pair not in pins.get(name,()):
                    raise ValueError('Unexpected physical terminal '+str((side,name,inst,pin)))
                pins[name].remove(pair)
                if physical_roots.setdefault((die,name),(die,root))!=(die,root):
                    raise ValueError('Open conductor component '+name)
        if any(pins.values()) or any((die,name) not in physical_roots for name in pins):
            raise ValueError('Missing conductor terminals on '+side)
    if observed_pins!=inventory['real_pins'] or len(set(owners.values()))!=inventory['global_nets']:
        raise ValueError('Global inventory disagrees with measured endpoints')

    # 两颗 Die 的几何连通分量编号是局部的。只允许经真实同一 site 的两个 landing 合并。
    parent={}
    def root(vertex):
        while vertex in parent:
            following=parent[vertex]
            if following in parent:
                parent[vertex]=parent[following]
            vertex=following
        return vertex
    for row in assignments.values():
        a=root(physical_roots[0,row['top_component']])
        b=root(physical_roots[1,row['bottom_component']])
        if a!=b:
            parent[a]=b
    root_by_net={}
    net_by_root={}
    for key,owner in owners.items():
        joined=root(physical_roots[key])
        if root_by_net.setdefault(owner,joined)!=joined:
            raise ValueError('Disconnected physical global net '+str(owner))
        if net_by_root.setdefault(joined,owner)!=owner:
            raise ValueError('Mixed global nets on physical conductor '+str(joined))
    return dict(status='passed',global_nets=len(root_by_net),real_pins=observed_pins,
                local_components=len(owners),hbt_pairs=len(assignments),disconnected_nets=0,mixed_nets=0)


def record_reports(global_dir,detail):
    joint=verify_routes(global_dir,detail)
    report={}
    for side in ('top','bottom'):
        folder=detail/side
        result=json.loads((folder/'detail.json').read_text())
        geometry=geometry_directory(detail,side)
        audit=json.loads((geometry/'geometry_audit.json').read_text())
        log=(folder/'router.log').read_text()
        drc=re.findall(r'Number of violations\s*=\s*(\d+)',log)
        via=re.findall(r'^Total number of vias = (\d+)',log,re.M)
        length=re.findall(r'^Total wire length = ([0-9.]+) um',log,re.M)
        if (not drc or int(drc[-1])!=result['drc'] or not via or int(via[-1])!=result['via_count']
                or not length or float(length[-1])!=result['wirelength_um']
                or result['routed']!=('NATIVE_DESIGN_ROUTED=1' in log)
                or joint['hbt_pairs']!=result['hbt_endpoints']
                or audit['physical_connectivity']['via_instances']!=result['via_count']):
            raise ValueError('Detailed routing measurement disagrees with logs or physical geometry on '+side)
        report[side]=dict(result,geometry=audit['status'],geometry_evidence=str(geometry.relative_to(detail)))
    report['joint_connectivity']=joint['status']
    report['assignment_endpoints_checked']=2*joint['hbt_pairs']
    report['passed']=all(report[s]['routed'] and report[s]['exit_code']==0 and report[s]['drc']==0
                         and report[s]['geometry']=='passed' for s in ('top','bottom')) and joint['status']=='passed'
    for name,record in [('joint_geometry',joint),('verified_report',report)]:
        path=detail/(name+'.tmp')
        path.write_text(json.dumps(record,indent=2)+'\n')
        path.replace(detail/(name+'.json'))
    # 保存首次失败的原报告，再把同一份 routed.def 的独立复查发布为当前结果。
    if report['passed'] and (detail/'report.json').exists():
        for side in ('top','bottom'):
            src=detail/side/'detail.json'
            if report[side]['geometry_evidence']!=side and json.loads(src.read_text())['geometry']!='passed':
                publish_recheck(src,report[side])
        src=detail/'report.json'
        if not json.loads(src.read_text())['passed']:
            publish_recheck(src,report)
    return report


def publish_recheck(path,record):
    first=path.with_suffix('.first.json')
    original=path.read_bytes()
    if first.exists():
        if first.read_bytes()!=original:
            raise ValueError('A different initial audit already exists at '+str(first))
    else:
        with first.open('xb') as copy:
            copy.write(original)
    temp=path.with_suffix('.rechecked.tmp')
    temp.write_text(json.dumps(record,indent=2)+'\n')
    temp.replace(path)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('global_dir',type=Path)
    parser.add_argument('detail_dir',type=Path)
    parser.add_argument('--record',action='store_true',help='Save a joined report and verify the original DRT measurements')
    args=parser.parse_args()
    if args.record:
        result=record_reports(args.global_dir,args.detail_dir)
        print(json.dumps(result))
        raise SystemExit(0 if result['passed'] else 1)
    print(json.dumps(verify_routes(args.global_dir,args.detail_dir)))
