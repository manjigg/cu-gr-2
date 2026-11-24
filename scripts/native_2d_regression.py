"""Run the unchanged single-die CLI on both dies and compare with a frozen 2D run."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess


def compare(binary,baseline,output,jobs):
    root=Path(__file__).resolve().parents[1]
    protocol=json.loads((root/'tests/native/benchmark-array.json').read_text())
    with baseline.open() as stream:
        expected={(r['case'],r['side']):r for r in csv.DictReader(stream,delimiter='\t')}
    cases=[(case,side) for case in protocol['lef_by_case'] for side in ('top','btm')]
    if set(cases)!=expected.keys():
        raise ValueError('Incomplete 2D reference')
    output.mkdir(parents=True)
    def run(key):
        case,side=key
        dest=output/(case+'_'+side)
        dest.mkdir()
        for table in ('POWV9.dat','POST9.dat'):
            (dest/table).symlink_to(root/'flute'/table)
        lef=root/'benchmarks'/protocol['lef_by_case'][case]
        design=root/'benchmarks'/case/(case+'_'+side+'_hbt.place.def')
        guide=dest/'route.guide'
        log=dest/'router.log'
        with log.open('w') as stream:
            proc=subprocess.run([binary,'-lef',lef,'-def',design,'-output',guide,'-threads','1'],
                                cwd=dest,stdout=stream,stderr=subprocess.STDOUT)
        content=log.read_text()
        row=dict(case=case,side=side,exit=str(proc.returncode))
        for field,label in [('wire_metric','wire length (metric)'),('via_count','total via count'),
                            ('overflow','total wire overflow')]:
            matches=re.findall(re.escape(label)+r'\s*:\s*(\S+)',content)
            row[field]=matches[-1] if matches else ''
        row['guide_sha256']=hashlib.sha256(guide.read_bytes()).hexdigest() if guide.is_file() else ''
        row['match']=all(row[k]==v for k,v in expected[key].items())
        return row
    with ThreadPoolExecutor(jobs) as pool,(output/'comparison.tsv').open('w') as stream:
        writer=csv.DictWriter(stream,['case','side','exit','wire_metric','via_count','overflow','guide_sha256','match'],delimiter='\t')
        writer.writeheader()
        passed=True
        for row in pool.map(run,cases):
            writer.writerow(row)
            stream.flush()
            passed=passed and row['match']
    return passed


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ('binary','baseline','output'):
        parser.add_argument(arg,type=Path)
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    raise SystemExit(0 if compare(args.binary.resolve(),args.baseline,args.output.resolve(),args.jobs) else 1)
