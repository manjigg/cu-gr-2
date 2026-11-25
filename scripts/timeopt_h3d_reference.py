"""Measure the installed H3D binary with the original, hash-checked benchmark inputs."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
from time import perf_counter
from native_bench import CASES, design_manifest


def last(pattern,text):
    matches=re.findall(pattern,text)
    return matches[-1] if matches else None


def run(binary,protocol,output,cases):
    output.mkdir(parents=True)
    (output/'binary.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
    with (output/'summary.tsv').open('w') as table:
        writer=None
        for case in cases:
            folder=output/case
            folder.mkdir()
            for lut in ('POWV9.dat','POST9.dat'):
                (folder/lut).symlink_to(binary.parent/lut)
            manifest=design_manifest(protocol,15,case)
            top,bottom=manifest['dies']
            config=dict(lef=top['lef'],tdef=top['def'],bdef=bottom['def'],output=str(folder/(case+'.guide')))
            (folder/'config.json').write_text(json.dumps(config,indent=2)+'\n')
            begin=perf_counter()
            log=folder/'router.log'
            with log.open('w') as stream:
                process=subprocess.run(['/usr/bin/time','-f','%M','-o',folder/'rss.kb',binary,'--config','config.json'],
                                       cwd=folder,stdout=stream,stderr=subprocess.STDOUT)
            elapsed=perf_counter()-begin
            text=log.read_text()
            row=dict(benchmark=case,exit_code=process.returncode,wall_seconds=elapsed,
                     threads=last(r'threads\s*:\s*(\d+)',text),wirelength_h3d_metric=last(r'wire length \(metric\):\s*(\d+)',text),
                     via_count=last(r'total via count:\s*(\d+)',text),available_hbt=last(r'Num of available HBT:\s*(\d+)',text),
                     used_hbt=last(r'num used HBT:\s*(\d+)',text),max_rss_kb=(folder/'rss.kb').read_text().splitlines()[-1])
            (folder/'process.json').write_text(json.dumps(row,indent=2)+'\n')
            if writer is None:
                writer=csv.DictWriter(table,row,delimiter='\t')
                writer.writeheader()
            writer.writerow(row)
            table.flush()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('binary','protocol','output'):
        parser.add_argument(key,type=Path)
    parser.add_argument('--cases',nargs='+',choices=CASES,default=CASES)
    a=parser.parse_args()
    run(a.binary.resolve(),a.protocol.resolve(),a.output.resolve(),a.cases)
