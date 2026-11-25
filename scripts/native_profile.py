"""Aggregate C++ timing evidence from a native_bench run without summing inclusive times."""
import argparse
import csv
import json
from pathlib import Path


def write_table(path,rows):
    if not rows:
        raise ValueError('No profiling observations for '+str(path))
    with path.open('w') as handle:
        tsv=csv.DictWriter(handle,rows[0],delimiter='\t',lineterminator='\n')
        tsv.writeheader()
        tsv.writerows(rows)


def aggregate(source,output):
    measurements={}
    modules=[]
    slow_nets=[]
    with (source/'summary.tsv').open() as stream:
        process=list(csv.DictReader(stream,delimiter='\t'))
    for row in process:
        case=row['benchmark']
        job=source/case
        profile=json.loads((job/'profile.json').read_text())
        routing=json.loads((job/'output/routing_statistics.json').read_text())
        total=profile['modules']['total']['inclusive_seconds']
        exclusive=sum(value['exclusive_seconds'] for value in profile['modules'].values())
        if total<=0 or abs(exclusive-total)>1e-7*total:
            raise ValueError('Exclusive accounting does not cover the measured interval in '+case)
        measurements[case]=dict(profile,router_reported_seconds=routing['routing_seconds'],
                                stages=routing['stages'],process=row)
        for name,value in profile['modules'].items():
            modules.append(dict(benchmark=case,module=name,**value,
                                exclusive_percent=100*value['exclusive_seconds']/total))
        with (job/'slow_nets.tsv').open() as handle:
            slow_nets.extend(dict(benchmark=case,**net) for net in csv.DictReader(handle,delimiter='\t'))
    output.mkdir(parents=True,exist_ok=True)
    (output/'profile_baseline.json').write_text(json.dumps(measurements,indent=2)+'\n')
    write_table(output/'profile_baseline.tsv',modules)
    write_table(output/'slow_nets_baseline.tsv',slow_nets)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path,help='Output folder from native_bench.py --profile')
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    aggregate(args.source,args.output)
