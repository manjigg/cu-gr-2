"""Measure seven objective settings with identical input and resource constraints."""
import argparse
import concurrent.futures
import csv
import json
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ('binary','manifest','output'):
        parser.add_argument(n,type=Path)
    parser.add_argument('--jobs',type=int,default=1)
    parser.add_argument('--seed-seconds',type=float,default=180.)
    parser.add_argument('--stage-seconds',type=float,default=15.)
    a=parser.parse_args()
    root=a.output.resolve()
    root.mkdir(parents=True,exist_ok=False)
    binary=a.binary.resolve()
    modes=[('wire',1,0,0,0),('via',0,1,0,0),('hbt',0,0,1,0),('w1',1,1,1,1),
           ('w4',1,1,4,1),('w8',1,1,8,1),('w16',1,1,16,1)]
    def run(mode):
        name,w,v,h,c=mode
        case=root/name
        case.mkdir()
        data=json.loads(a.manifest.read_text())
        for die in data['dies']:
            for n in ['def','lef']:
                die[n]=str((a.manifest.resolve().parent/die[n]).resolve())
        data.update(weights=dict(wire_um=w,via=v,hbt=h,congestion=c),seed_seconds=a.seed_seconds,
                    stage_seconds=a.stage_seconds)
        config=case/'manifest.json'
        config.write_text(json.dumps(data,separators=(',',':')))
        with (case/'router.log').open('w') as log:
            p=subprocess.run([binary,'-native',config,'-output',case/'output'],stdout=log,stderr=subprocess.STDOUT,cwd=binary.parent)
        path=case/'output/routing_statistics.json'
        stats=json.loads(path.read_text()) if path.exists() else {}
        m=stats.get('metrics') or {}
        return [name,w,v,h,c,stats.get('status','PROCESS_ERROR'),p.returncode,m.get('wirelength_um'),
                m.get('via_count'),m.get('used_hbt'),m.get('wire_overflow')]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool,(root/'sensitivity.tsv').open('w') as stream:
        writer=csv.writer(stream,delimiter='\t')
        writer.writerow('mode wire_weight via_weight hbt_weight congestion_weight status exit_code wirelength_um via_count used_hbt wire_overflow'.split())
        for row in pool.map(run,modes):
            writer.writerow(row);stream.flush()

if __name__=='__main__':
    main()
