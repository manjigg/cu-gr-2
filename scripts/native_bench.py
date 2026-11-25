"""Run unchanged benchmark DEFs with an explicit, reproducible HBT array."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

CASES = ['aes', 'tinyRocket', 'ethmac', 'mor1kx', 'jpegencode', 'ariane', 'bp']


def design_manifest(reference, seconds, case=None):
    old = json.loads(reference.read_text())
    expected = {}
    if 'lef_by_case' in old:
        root = (reference.parent/old['benchmarks']).resolve()
        for side,suffix in [('top','top'),('bottom','btm')]:
            lef = old['lef_by_case'][case]
            design = f'{case}/{case}_{suffix}_hbt.place.def'
            old[side] = dict(lef=str(root/lef), layer=old['landing_layer'], **{'def':str(root/design)})
            for name in (lef,design):
                expected[str(root/name)] = old['input_sha256'][name]
    if not old.get('paired_ports'):
        raise ValueError('Expected a paired-port experiment')
    die_inputs, boundaries, ports, fingerprints = [], [], [], {}
    for side in ('top', 'bottom'):
        d = old[side]
        path = Path(d['def'])
        raw = path.read_bytes()
        fingerprints[str(path)] = hashlib.sha256(raw).hexdigest()
        fingerprints[d['lef']] = hashlib.sha256(Path(d['lef']).read_bytes()).hexdigest()
        data = raw.decode()
        pins = re.search(r'^PINS \d+ ;(.*?)^END PINS', data, re.M | re.S)[1]
        ports.append(dict(re.findall(r'^\s*-\s+(T\d+)\s+\+ NET (\S+)', pins, re.M)))
        area = re.search(r'DIEAREA \( (-?\d+) (-?\d+) \) \( (-?\d+) (-?\d+) \)', data)
        boundaries.append(list(map(int, area.groups())))
        units = int(re.search(r'UNITS DISTANCE MICRONS (\d+)', data)[1])
        if units != old['package_dbu'] or 'transform' in d:
            raise ValueError('This adapter handles the original identity-transform experiment only')
        die_inputs.append(dict(lef=d['lef'], **{'def': str(path)}, via_capacity='track_intersections',
                               transform=dict(axes=[0, 1], signs=[1, 1], offset=[0, 0])))
    if expected and fingerprints != expected:
        raise ValueError('Original benchmark fingerprint mismatch for '+case)
    if ports[0] != ports[1] or not ports[0] or len(set(ports[0].values())) != len(ports[0]):
        raise ValueError('Ambiguous cut-port identities')
    mapping = []
    for name, net in ports[0].items():
        side = dict(net=net, cut_port=name)
        mapping.append(dict(top=side, bottom=side))
    array = old['hbt']
    points = []
    for a in range(2):
        low = max(area[a] for area in boundaries)
        high = min(area[a+2] for area in boundaries)
        origin, pitch = array['origin'][a], array['pitch'][a]
        points.append(range(low + (origin-low) % pitch, high+1, pitch))
    pairs = [[old['top']['layer'], old['bottom']['layer']]]
    sites = [dict(id=i, xy=[x, y], layers=pairs, capacity=1)
             for i, (x, y) in enumerate((x, y) for x in points[0] for y in points[1])]
    return dict(bonded_dbu=old['package_dbu'], dies=die_inputs, mapping=mapping, stage_seconds=seconds,
                weights=dict(wire_um=1, via=1, hbt=array['cost'], congestion=1),
                bonding=dict(origin=array['origin'], pitch=array['pitch'], min_spacing=array['min_spacing'],
                             keepouts=[], sites=sites),
                provenance=dict(reference=str(reference), input_sha256=fingerprints,
                                scope='experimental array, aggregate track-intersection via capacity'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('references', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--stage-seconds', type=float, default=60)
    parser.add_argument('--master-seconds', type=float, help='Master selection time limit, defaulting to the stage limit')
    parser.add_argument('--seed-seconds', type=float, default=60)
    parser.add_argument('--threads', type=int, default=1, help='Number of requested routing threads')
    parser.add_argument('--profile',action='store_true',help='Record nested C++ module times and the 100 slowest nets')
    parser.add_argument('--options',type=Path,help='JSON file with additional native algorithm settings')
    parser.add_argument('--cases', nargs='+', choices=CASES, default=CASES)
    parser.add_argument('--jobs', type=int, default=1)
    args = parser.parse_args()
    binary = args.binary.resolve()
    options=json.loads(args.options.read_text()) if args.options else {}
    if not isinstance(options,dict) or any(k in options for k in ('dies','mapping','bonding','bonded_dbu','weights','provenance')):
        parser.error('Options must be an object and cannot replace the benchmark protocol')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    def run(case):
        job = out/case
        job.mkdir()
        source = args.references if args.references.is_file() else args.references/case/'f2f.json'
        manifest = design_manifest(source.resolve(), args.stage_seconds,case)
        manifest.update(options)
        manifest['seed_seconds'] = args.seed_seconds
        manifest['master_seconds'] = args.master_seconds if args.master_seconds is not None else args.stage_seconds
        manifest['provenance'].update(profile=args.profile,requested_threads=args.threads)
        config = job/'manifest.json'
        config.write_text(json.dumps(manifest, separators=(',', ':')))
        start = time.monotonic()
        with (job/'router.log').open('w') as log:
            env = os.environ.copy()
            if args.profile:
                env['CUGR_NATIVE_PROFILE']='1'
            else:
                env.pop('CUGR_NATIVE_PROFILE',None)
            proc = subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(job/'rss.kb'),
                                   binary, '-native', config, '-output', job/'output', '-threads',str(args.threads)],
                                  cwd=job, env=env, stdout=log, stderr=subprocess.STDOUT)
        elapsed = time.monotonic()-start
        stats_path = job/'output/routing_statistics.json'
        stats = json.loads(stats_path.read_text()) if stats_path.exists() else {}
        stages=stats.get('stages',[])
        if stages:
            with (job/'iteration_statistics.tsv').open('w') as stream:
                fields=list(dict.fromkeys(k for stage in stages for k in stage))
                writer=csv.DictWriter(stream,fields,delimiter='\t',lineterminator='\n')
                writer.writeheader()
                for stage in stages:
                    writer.writerow({k:json.dumps(v,separators=(',',':')) if isinstance(v,dict) else v
                                     for k,v in stage.items()})
        inventory = stats.get('inventory', {})
        metrics = stats.get('metrics') or {}
        record=dict(benchmark=case, status=stats.get('status', 'PROCESS_ERROR'), exit_code=proc.returncode,
                    global_nets=inventory.get('global_nets'), real_pins=inventory.get('real_pins'),
                    cross_die=len(manifest['mapping']), legal_sites=len(manifest['bonding']['sites']),
                    wirelength_um=metrics.get('wirelength_um'), via_count=metrics.get('via_count'),
                    used_hbt=metrics.get('used_hbt'), wire_overflow=metrics.get('wire_overflow'), drc=None,
                    wall_seconds=round(elapsed, 3), max_rss_kb=(job/'rss.kb').read_text().splitlines()[-1])
        (job/'process.json').write_text(json.dumps(record,indent=2)+'\n')
        if args.profile and proc.returncode in (0,2):
            for name in ('profile.json','slow_nets.tsv'):
                (job/'output'/name).rename(job/name)
        return record

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, (out/'summary.tsv').open('w') as stream:
        writer = None
        failed = False
        for row in pool.map(run, args.cases):
            failed |= row['exit_code'] != 0
            if writer is None:
                writer = csv.DictWriter(stream, fieldnames=row.keys(), delimiter='\t')
                writer.writeheader()
            writer.writerow(row)
            stream.flush()
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
