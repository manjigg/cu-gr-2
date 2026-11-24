"""End-to-end feasibility, sensitivity and commit/guide regressions on physical DEF inputs."""
from pathlib import Path
import copy
import gzip
import hashlib
import json
import re
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from native_check import check
from native_detail import resolve_instances
from native_joint import verify_routes
from native_geometry import instance_names

binary=Path(sys.argv[1]).resolve()
base=root/'tests/native/clusters'
template=json.loads((base/'manifest.json').read_text())
for d in template['dies']:
    for field in ['lef','def']:
        d[field]=str((base/d[field]).resolve())


def run(cfg,dest,success=True):
    dest.mkdir()
    manifest=dest/'manifest.json'
    manifest.write_text(json.dumps(cfg))
    output=dest/'out'
    with (dest/'router.log').open('w') as log:
        p=subprocess.run([binary,'-native',manifest,'-output',output],stdout=log,stderr=subprocess.STDOUT)
    stats=json.loads((output/'routing_statistics.json').read_text())
    if success:
        assert p.returncode==0 and stats['status']=='OPTIMAL_CANDIDATE_POOL',stats
        check(manifest,output)
        assert stats['stages'][-1]['gap']==0
    else:
        assert p.returncode==2 and stats['status']=='INFEASIBLE_CANDIDATE_POOL' and stats['metrics'] is None
    return stats


def check_joint_replay(job):
    # 可控的导体分量回放用例，分别注入断线、错误 landing 与陈旧几何证据。
    data=job/'out'
    detail=job/'test-conductors'
    records=[json.loads(row) for row in (data/'components.jsonl').read_text().splitlines()]
    for die,side in enumerate(('top','bottom')):
        folder=detail/side
        folder.mkdir(parents=True)
        evidence=b'geometry fixture'
        (folder/'routed.def').write_bytes(evidence)
        (folder/'geometry_audit.json').write_text(json.dumps(dict(status='passed',
            routed_def_sha256=hashlib.sha256(evidence).hexdigest())))
        ports=[]
        with gzip.open(folder/'terminal_components.jsonl.gz','wt') as stream:
            for i,c in enumerate(records):
                if c['die']!=die:
                    continue
                terminals=list(c['pins'])
                for h in c['landings']:
                    ports.append(dict(component=c['name'],port=h['name'],net=c['net_id'],site=h['id'],
                                      layer=h['layer'],x=h['xy'][0],y=h['xy'][1]))
                    terminals.append(['PIN',h['name']])
                for inst,pin in terminals:
                    stream.write(json.dumps([c['name'],inst,pin,i])+'\n')
        (folder/'physical_ports.json').write_text(json.dumps(ports))
    assert verify_routes(data,detail)['hbt_pairs']==2
    blob=detail/'top/terminal_components.jsonl.gz'
    valid=blob.read_bytes()
    with gzip.open(blob,'rt') as stream:
        terminals=[json.loads(s) for s in stream]
    terminals[0][3]+=100
    with gzip.open(blob,'wt') as stream:
        for t in terminals:
            stream.write(json.dumps(t)+'\n')
    try:
        verify_routes(data,detail)
        raise AssertionError('Separated physical terminals accepted')
    except ValueError as error:
        assert 'Open conductor' in str(error)
    blob.write_bytes(valid)
    port_file=detail/'top/physical_ports.json'
    pads=json.loads(port_file.read_text())
    pads[0]['x']+=1
    port_file.write_text(json.dumps(pads))
    try:
        verify_routes(data,detail)
        raise AssertionError('Displaced HBT accepted')
    except ValueError as error:
        assert 'landing mismatch' in str(error)
    (detail/'top/routed.def').write_bytes(b'changed geometry')
    try:
        verify_routes(data,detail)
        raise AssertionError('Stale routed DEF accepted')
    except ValueError as error:
        assert 'stale' in str(error)


def regress(out):
    decode=lambda text:re.sub(r'\\(.)',r'\1',text)
    open_db=instance_names([r'cell\\X','normal'],decode)
    assert open_db[r'cell\\X']==open_db[r'cell\X']==r'cell\X'
    try:
        instance_names([r'cell\\X',r'cell\X'],decode)
        raise AssertionError('Ambiguous OpenDB identifier accepted')
    except ValueError:
        pass
    records=[dict(pins=[(r'cell\X','Q')])]
    text='COMPONENTS 1 ;\n- '+r'cell\\X'+' TEST ;\nEND COMPONENTS\n'
    assert list(resolve_instances(text,records))[0]['pins']==[(r'cell\\X','Q')]
    ambiguous=text.replace('COMPONENTS 1','COMPONENTS 2').replace('END COMPONENTS','- '+r'cell\X'+' TEST ;\nEND COMPONENTS')
    try:
        list(resolve_instances(ambiguous,records))
        raise AssertionError('Ambiguous DEF aliases accepted')
    except ValueError:
        pass
    table=[]
    for label,w,v,h in [('wire',1,0,0),('via',0,1,0),('hbt',0,0,1),
                        ('weighted1',1,1,1),('weighted4',1,1,4),('weighted5',1,1,5),('weighted12',1,1,12)]:
        cfg=copy.deepcopy(template)
        cfg['weights'].update(wire_um=w,via=v,hbt=h)
        rep=run(cfg,out/label)
        m=rep['metrics']
        table.append((label,w,v,h,m['wirelength_um'],m['via_count'],m['used_hbt'],rep['stages'][-1]['best_feasible']))
    assert table[3][6]==2 and table[-1][6]==1 and table[3][4]<table[-1][4]
    job=out/'weighted1'
    check_joint_replay(job)
    edges=job/'out/route_edges.tsv'
    ledger=job/'ledger.json'
    replay=[binary.parent/'native_replay',job/'manifest.json']
    subprocess.run([*replay,edges,ledger],stdout=subprocess.DEVNULL,check=True)
    evidence=json.loads(ledger.read_text())
    assert evidence['resource_keys_after_commit']>0 and evidence['resource_keys_after_release']==0
    assert evidence['hbt_count']==2 and evidence['physical_pins']==4 and evidence['unconnected_nets']==0
    rows=edges.read_text().splitlines()
    crossing=next(i for i,row in enumerate(rows) if '\tHBT\t' in row)
    for name,invalid in [('duplicate',rows+[rows[crossing]]),('disconnected',rows[:crossing]+rows[crossing+1:])]:
        bad=job/(name+'.tsv')
        bad.write_text('\n'.join(invalid)+'\n')
        report=job/(name+'.json')
        p=subprocess.run([*replay,bad,report],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        assert p.returncode!=0 and not report.exists(),name
    # 三对投影重合的真实端点；仅运行第一阶段，无启发式初解。最小线长需要三个 HBT。
    three=copy.deepcopy(template)
    three.update(stages=[1],seed_seconds=0)
    three['weights']['hbt']=0.25
    array=three['bonding']
    array.update(pitch=[4200,4200],min_spacing=4200)
    array['sites'].append(dict(array['sites'][0],id=2,xy=[6300,2100]))
    mid='- M + NET N + DIRECTION INPUT + USE SIGNAL + LAYER metal3 ( -100 -100 ) ( 100 100 ) + FIXED ( 6300 1500 ) N ;'
    for d,source in enumerate(three['dies']):
        fixture=out/('three-cluster-'+str(d)+'.def')
        fixture.write_text(Path(source['def']).read_text().replace('PINS 2 ;','PINS 3 ;')
                           .replace('END PINS',mid+'\nEND PINS').replace('( PIN R )','( PIN R ) ( PIN M )'))
        source['def']=str(fixture.resolve())
    triple=run(three,out/'three-hbt')
    assert triple['metrics']['used_hbt']==3 and abs(triple['metrics']['wirelength_um']-4.2)<1e-9
    assert triple['inventory']['real_pins']==6 and triple['stages'][0]['candidate_generation']=='FINISHED'
    cfg=copy.deepcopy(template)
    cfg['bonding']['sites'][0]['fixed_net']=0
    run(cfg,out/'fixed')
    cfg=copy.deepcopy(template)
    cfg['bonding']['sites']=[]
    run(cfg,out/'no-site',False)
    cfg=copy.deepcopy(template)
    cfg['mapping']=[]
    cfg['bonding']['sites']=[]
    assert run(cfg,out/'local')['metrics']['used_hbt']==0
    cfg=copy.deepcopy(template)
    cfg['mapping']=[]
    cfg['same_die_maze_detour']=True
    blocked=out/'cut.def'
    blocked.write_text((base/'top.def').read_text().replace('PINS 2 ;',
        'BLOCKAGES 1 ;\n- LAYER metal3 RECT ( 3900 0 ) ( 4500 12600 ) ;\nEND BLOCKAGES\nPINS 2 ;'))
    cfg['dies'][0]['def']=str(blocked.resolve())
    detour=run(cfg,out/'detour')
    assert detour['stages'][0]['solver_status']=='INFEASIBLE',detour
    assert detour['stages'][1]['solver_status']=='OPTIMAL' and detour['metrics']['used_hbt']==2,detour
    short=out/'short-obstruction.def'
    short.write_text(blocked.read_text().replace('( 4500 12600 )','( 4500 4050 )'))
    repair_cfg=copy.deepcopy(cfg)
    repair_cfg['dies'][0]['def']=str(short.resolve())
    repair_manifest=out/'repair.json'
    repair_manifest.write_text(json.dumps(repair_cfg))
    subprocess.run([binary.parent/'native_pattern_test',repair_manifest,'--branch-test'],
                   stdout=subprocess.DEVNULL,check=True)
    cfg['stages']=[3]
    maze=run(cfg,out/'maze-only')
    assert maze['metrics']['used_hbt']==2 and maze['stages'][0]['stage']==3
    certificate=out/'no-site'/'reachability.json'
    proc=subprocess.run([binary,'-check-native',out/'no-site'/'manifest.json','-output',certificate],
                        stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    assert proc.returncode==2 and len(json.loads(certificate.read_text())['disconnected'])==1
    import csv
    with (out/'sensitivity.tsv').open('w') as f:
        writer=csv.writer(f,delimiter='\t')
        writer.writerow('mode wire_weight via_weight hbt_weight wirelength_um via_count used_hbt objective'.split())
        writer.writerows(table)


if len(sys.argv)==3:
    output=Path(sys.argv[2])
    output.mkdir(parents=True,exist_ok=False)
    regress(output)
else:
    with tempfile.TemporaryDirectory() as work:
        regress(Path(work))
