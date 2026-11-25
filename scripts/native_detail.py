"""Build component-correct placed DEFs and run TritonRoute separately on both dies."""
import argparse
import collections
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import concurrent.futures
from native_joint import verify_routes


def section(text, name):
    match = re.search(r'^'+name+r' (\d+) ;\s*(.*?)^END '+name+r'\b', text, re.M | re.S)
    if not match:
        raise ValueError('Missing section '+name)
    items = {}
    for entry in re.finditer(r'^\s*-\s+(\S+)\s+(.*?);', match[2], re.S | re.M):
        if entry[1] in items:
            raise ValueError('Duplicate '+entry[1])
        items[entry[1]] = entry[2].strip()
    if len(items) != int(match[1]):
        raise ValueError('Unsupported DEF '+name+' syntax or count')
    return items, match.span()


def resolve_instances(text, components):
    """Match Rsyn instance names to raw DEF tokens, keeping the original spelling for output."""
    raw_names=section(text,'COMPONENTS')[0].keys()
    aliases={}
    for raw in raw_names:
        decoded=re.sub(r'\\(.)',r'\1',raw)
        if decoded!=raw:
            if decoded in raw_names or decoded in aliases:
                raise ValueError('Ambiguous escaped component name '+decoded)
            aliases[decoded]=raw
    claimed={}
    for c in components:
        pins=[]
        for inst,pin in c['pins']:
            if inst=='PIN':
                pins.append((inst,pin))
                continue
            raw=aliases.get(inst,inst)
            if raw not in raw_names or claimed.setdefault(raw,inst)!=inst:
                raise ValueError('Unknown or non-bijective DEF component '+inst)
            pins.append((raw,pin))
        yield dict(c,pins=pins)


def render(name, items):
    return (f'{name} {len(items)} ;\n' +
            ''.join(f'- {key} {value} ;\n' for key, value in items.items())+'END '+name)


def derive(original, components, cuts, half):
    text = original.read_text()
    pins, pspan = section(text, 'PINS')
    nets, nspan = section(text, 'NETS')
    components=list(resolve_instances(text,components))
    terminal = re.compile(r'\(\s*(\S+)\s+(\S+)\s*\)')
    pending = {}
    attrs = {}
    for name, data in nets.items():
        body, plus, props = data.partition('+')
        pending[name] = collections.Counter(terminal.findall(body))
        attrs[name] = plus+props
        if terminal.sub('', body).strip() or re.search(r'\b(ROUTED|FIXED|COVER|SHIELD|NONDEFAULTRULE|SUBNET)\b', props):
            raise ValueError('Unsupported pre-routed or constrained DEF net '+name)
    new = {}
    claimed = set()
    physical = {}
    for c in components:
        name, local = c['name'], c['local_net']
        if name in new or name in nets:
            raise ValueError('Generated net name collision '+name)
        members = []
        for a, b in c['pins']:
            if pending[local][a,b] <= 0:
                raise ValueError(f'Unknown or repeated terminal {(local, a, b)}')
            pending[local][a,b] -= 1
            if a == 'PIN':
                pins[b], count = re.subn(r'(?<=\+ NET )\S+', lambda _: name, pins[b], count=1)
                if count != 1:
                    raise ValueError('Missing PORT net property')
            members.append((a, b))
        for h in c['landings']:
            port = h['name']
            # 复用已确认的切分 PORT 身份及方向；在派生 DEF 中替换其 landing 几何。
            witness = cuts.get(local)
            if witness and witness not in claimed:
                claimed.add(witness)
                port = witness
                if pending[local]['PIN', port] != 1:
                    raise ValueError('Invalid mapping witness')
                pending[local]['PIN', port] -= 1
                direction = re.search(r'\+ DIRECTION (\S+)', pins[port])[1]
            else:
                if port in pins:
                    raise ValueError('Generated PORT name collision '+port)
                direction = 'INOUT'
            x, y = h['xy']
            layer = h['layer']
            pins[port] = (f'+ NET {name} + DIRECTION {direction} + USE SIGNAL + LAYER {layer} '
                          f'( {-half} {-half} ) ( {half} {half} ) + FIXED ( {x} {y} ) N')
            physical[c['net_id'], h['id']] = (name, port, layer, x, y)
            members.append(('PIN', port))
        new[name] = ' '.join(f'( {a} {b} )' for a,b in members)+' '+attrs.get(local,'+ USE SIGNAL')
    for name, counter in pending.items():
        if any(counter.values()):
            if any(c['local_net']==name for c in components):
                raise ValueError('Unassigned real terminal on '+name)
            # Unmodelled nets may only be power or ground.
            if not re.search(r'\+ USE (POWER|GROUND)\b', attrs[name]):
                raise ValueError('Signal net missing from router output '+name)
            new[name] = nets[name]
    if set(cuts.values()) != claimed:
        raise ValueError('A mapped cut port has no physical landing')
    for (lo, hi), replacement in sorted([(pspan, render('PINS', pins)), (nspan, render('NETS', new))], reverse=True):
        text = text[:lo]+replacement+text[hi:]
    return text, physical


TCL = '''if {[catch {
    read_lef $::env(CUGR_LEF)
    read_def $::env(CUGR_DEF)
    set_thread_count $::env(CUGR_THREADS)
    set_routing_layers -signal $::env(CUGR_LAYERS)
    read_guides $::env(CUGR_GUIDE)
    detailed_route -output_drc drc.rpt -output_guide_coverage coverage.csv -or_seed 0 -verbose 1
    puts "NATIVE_DESIGN_ROUTED=[design_is_routed -verbose]"
    write_def routed.def
} problem options]} {puts stderr [dict get $options -errorinfo]; exit 1}
exit 0
'''


def main():
    started=time.perf_counter()
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ['openroad','manifest','global_dir','output']:
        parser.add_argument(arg, type=Path)
    parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--parallel-dies',action='store_true')
    parser.add_argument('--geometry-tool',type=Path,help='Existing OpenDB validation directory with audit.py, support/ and export_geometry.tcl')
    args = parser.parse_args()
    exe = args.openroad.resolve()
    manifest = json.loads(args.manifest.read_text())
    base = args.manifest.resolve().parent
    gr = args.global_dir.resolve()
    components = [json.loads(s) for s in (gr/'components.jsonl').read_text().splitlines()]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    assignments = list(csv.DictReader((gr/'hbt_assignment.tsv').open(), delimiter='\t'))
    def file_digest(path):
        digest=hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(1048576),b''):
                digest.update(block)
        return digest.hexdigest()
    evidence={name:file_digest(gr/name) for name in ('top.guide','bottom.guide','components.jsonl','hbt_assignment.tsv')}
    (output/'guide_provenance.json').write_text(json.dumps(dict(source=str(gr),sha256=evidence,
        openroad_binary_sha256=file_digest(exe)),indent=2)+'\n')
    report = {}
    if args.geometry_tool:
        tool=args.geometry_tool.resolve()
        (output/'bin').mkdir()
        (output/'bin/openroad').symlink_to(exe)
        for name in ['support','export_geometry.tcl']:
            (output/name).symlink_to(tool/name)
        jobs=[]
        for d,side in enumerate(['top','bottom']):
            folder=output/side
            jobs.append(dict(id=side,directory=str(folder),input_def=str(folder/'placed.def'),lef=str((base/manifest['dies'][d]['lef']).resolve())))
        (output/'plan.json').write_text(json.dumps(dict(jobs=jobs)))
    def route_die(d):
        started=time.perf_counter()
        side=('top','bottom')[d]
        die = manifest['dies'][d]
        lef = (base/die['lef']).resolve()
        selected = [c for c in components if c['die']==d]
        cuts = {m[side]['net']:m[side]['cut_port'] for m in manifest['mapping'] if m[side].get('cut_port')}
        half = die.get('landing_half_width_dbu')
        if half is None:
            # 由同一侧所有已确认切分端口的方形 landing 推导已有工艺尺寸。
            placed,_ = section((base/die['def']).read_text(),'PINS')
            sizes=set()
            for port in cuts.values():
                shape=re.findall(r'\+ LAYER \S+ \( (-?\d+) (-?\d+) \) \( (-?\d+) (-?\d+) \)',placed[port])
                if len(shape)!=1:
                    raise ValueError('Provide landing_half_width_dbu for non-square or multiple pin shapes')
                x0,y0,x1,y1=map(int,shape[0])
                if x0!=y0 or x0!=-x1 or x1!=y1:
                    raise ValueError('Provide landing_half_width_dbu for asymmetric pin geometry')
                sizes.add(x1)
            if len(sizes)!=1:
                raise ValueError('Ambiguous HBT pad dimensions')
            half=sizes.pop()
        if not isinstance(half, int) or half<1:
            raise ValueError('Expected a positive landing half-width in local DBU')
        text, pins = derive(base/die['def'], selected, cuts, half)
        expected = {(int(r['net_id']), int(r['site_id'])):
                    (r[side+'_component'],r[side+'_layer'],int(r[side+'_x']),int(r[side+'_y'])) for r in assignments}
        if len(expected)!=len(assignments) or {k:(v[0],*v[2:]) for k,v in pins.items()}!=expected:
            raise ValueError('Component and assignment landing mismatch on '+side)
        job = output/side
        job.mkdir()
        (job/'placed.def').write_text(text)
        (job/'physical_ports.json').write_text(json.dumps([dict(net=n,site=s,component=v[0],port=v[1],
            layer=v[2],x=v[3],y=v[4]) for (n,s),v in pins.items()], indent=2))
        (job/'route.tcl').write_text(TCL)
        layers = re.findall(r'^LAYER (\S+)\s+TYPE ROUTING', lef.read_text(), re.M)
        env = dict(os.environ, CUGR_THREADS=str(args.threads), CUGR_LEF=str(lef), CUGR_DEF=str(job/'placed.def'),
                   CUGR_GUIDE=str(gr/(side+'.guide')), CUGR_LAYERS=layers[0]+'-'+layers[-1])
        with (job/'router.log').open('w') as log:
            proc = subprocess.run([exe,'-no_init','-exit','route.tcl'], cwd=job,env=env,stdout=log,stderr=subprocess.STDOUT)
        log = (job/'router.log').read_text(errors='replace')
        drc = re.findall(r'Number of violations\s*=\s*(\d+)', log, re.I)
        length=re.findall(r'^Total wire length = ([0-9.]+) um',log,re.M)
        vias=re.findall(r'^Total number of vias = (\d+)',log,re.M)
        result = dict(wall_seconds=time.perf_counter()-started,wirelength_um=float(length[-1]) if length else None,
                            via_count=int(vias[-1]) if vias else None, landing_half_width_dbu=half, exit_code=proc.returncode, routed='NATIVE_DESIGN_ROUTED=1' in log,
                            drc=int(drc[-1]) if drc else None, hbt_endpoints=len(pins))
        if args.geometry_tool and proc.returncode==0 and (job/'routed.def').is_file():
            with (job/'geometry.log').open('w') as log:
                replay=subprocess.run([sys.executable,'-B',Path(__file__).resolve().with_name('native_geometry.py'),
                                       tool,output,side],
                                      stdout=log,stderr=subprocess.STDOUT)
            result['geometry']='passed' if replay.returncode==0 else 'failed'
        (job/'detail.json').write_text(json.dumps(result,indent=2)+'\n')
        return side,result
    with concurrent.futures.ThreadPoolExecutor(2 if args.parallel_dies else 1) as pool:
        report.update(pool.map(route_die,(0,1)))
    report['assignment_endpoints_checked']=len(assignments)*2
    report['passed']=all(report[s]['exit_code']==0 and report[s]['routed'] and report[s]['drc']==0 for s in ('top','bottom'))
    if args.geometry_tool:
        if all(report[s].get('geometry')=='passed' for s in ('top','bottom')):
            try:
                joint=verify_routes(gr,output)
            except (ValueError,KeyError,OSError) as error:
                joint=dict(status='failed',error=str(error))
            (output/'joint_geometry.json').write_text(json.dumps(joint,indent=2)+'\n')
            report['joint_connectivity']=joint['status']
        else:
            report['joint_connectivity']='failed'
        report['passed']=report['passed'] and report['joint_connectivity']=='passed'
    report['detail_wall_seconds']=time.perf_counter()-started
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
