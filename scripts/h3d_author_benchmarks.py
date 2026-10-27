#!/usr/bin/env python3
"""Preserve, adapt, run and compare the seven author-supplied H3D cases.

Paired T* ports in these inputs describe provisional inter-die terminals, not
additional circuit endpoints. Only verified paired proxies are removed in new
derived DEFs. Original benchmarks and reference outputs are never rewritten.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CASES = ('aes', 'tinyRocket', 'ethmac', 'mor1kx', 'ariane', 'jpegencode', 'bp')
AUTHOR = Path('/home/wanghh/h3d/h3d_zyx/install')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write('\n')


def section(text, name):
    match = re.search(r'(?m)^\s*' + name + r'\s+(\d+)\s*;', text)
    if not match:
        raise ValueError('Missing DEF section ' + name)
    end = re.search(r'(?m)^\s*END\s+' + name + r'\b', text[match.end():])
    if not end:
        raise ValueError('Unterminated DEF section ' + name)
    stop = match.end() + end.start()
    body = text[match.end():stop]
    records = {}
    for record in re.finditer(r'(?:^|\n)\s*-\s+(\S+)\s+(.*?);', body, re.S):
        key = record[1]
        if key in records:
            raise ValueError('Duplicate DEF record ' + key)
        records[key] = record[2]
    if len(records) != int(match[1]):
        raise ValueError(f'{name}: declares {match[1]}, found {len(records)}')
    return records, (match.start(), stop + end.end() - end.start())


def connections(records):
    result = {}
    for name, body in records.items():
        head = body.split('+', 1)[0]
        pairs = tuple(re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', head))
        if len(pairs) != len(set(pairs)):
            raise ValueError('Duplicate endpoint in ' + name)
        result[name] = pairs
    return result


def pins(records):
    result = {}
    for name, body in records.items():
        net = re.search(r'\+\s*NET\s+(\S+)', body)
        layer = re.search(r'\+\s*LAYER\s+(\S+)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)', body)
        pos = re.search(r'\+\s*(FIXED|PLACED)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+(\S+)', body)
        if not (net and layer and pos):
            raise ValueError('Unsupported port ' + name)
        result[name] = {'net': net[1], 'layer': layer[1], 'rect': list(map(int, layer.groups()[1:])),
                        'position': list(map(int, pos.groups()[1:3])), 'orientation': pos[4]}
    return result


def components(records):
    result = {}
    for name, body in records.items():
        p = re.search(r'\+\s*(PLACED|FIXED|COVER)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+(\S+)', body)
        if not p:
            raise ValueError('Missing component placement ' + name)
        result[name] = (body.split()[0], *p.groups())
    return result


def input_paths():
    return sorted(p for folder in ('benchmarks_h3d', 'results_h3d') for p in (ROOT / folder).rglob('*') if p.is_file())


def protected():
    return {str(p.relative_to(ROOT)): {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in input_paths()}


def check_protected(folder):
    before = json.loads((folder / 'original_manifest.json').read_text())
    after = protected()
    if before != after:
        raise RuntimeError('Author inputs/reference outputs changed')


def prepare(folder):
    folder.mkdir(parents=True, exist_ok=False)
    write_json(folder / 'original_manifest.json', protected())
    (folder / 'inputs').mkdir()
    (folder / 'binaries').mkdir()
    shutil.copy2(ROOT / 'build/route', folder / 'binaries/route_baseline')
    write_json(folder / 'source_baseline_hashes.json', {str(p.relative_to(ROOT)): sha(p)
        for base in ('src', 'rsyn') for p in (ROOT / base).rglob('*') if p.is_file()})
    dirty = subprocess.run(['git', '--no-optional-locks', 'diff', '--binary'], cwd=ROOT, capture_output=True, check=True)
    with (folder / 'source_baseline.patch').open('xb') as stream:
        stream.write(dirty.stdout)
    audit = {}
    for case in CASES:
        output = folder / 'inputs' / case
        output.mkdir()
        cfg_path = AUTHOR / 'configs' / (case + '.json')
        cfg = json.loads(cfg_path.read_text())
        lef_name = Path(cfg['lef']).name
        lef = ROOT / 'benchmarks_h3d' / lef_name
        if sha(lef) != sha((AUTHOR / 'bin' / cfg['lef']).resolve()):
            raise ValueError('Copied LEF differs from author installation')
        data = {}
        for die, suffix, cfg_key in (('top', 'top', 'tdef'), ('bottom', 'btm', 'bdef')):
            path = ROOT / 'benchmarks_h3d' / case / f'{case}_{suffix}_hbt.place.def'
            if sha(path) != sha((AUTHOR / 'bin' / cfg[cfg_key]).resolve()):
                raise ValueError('Copied DEF differs from author installation: ' + str(path))
            text = path.read_text()
            pin_records, pin_span = section(text, 'PINS')
            net_records, net_span = section(text, 'NETS')
            comp_records, _ = section(text, 'COMPONENTS')
            data[die] = {'text': text, 'pins': pins(pin_records), 'nets': connections(net_records),
                         'net_records': net_records, 'pin_span': pin_span, 'net_span': net_span,
                         'components': components(comp_records)}
        top, bottom = data['top'], data['bottom']
        if top['pins'].keys() != bottom['pins'].keys():
            raise ValueError('Unpaired provisional ports in ' + case)
        common = top['nets'].keys() & bottom['nets'].keys()
        proxy_nets = set()
        pad_sizes = set()
        for name, p in top['pins'].items():
            q = bottom['pins'][name]
            if not re.fullmatch(r'T\d+', name) or p['net'] != q['net'] or p['net'] not in common:
                raise ValueError('Not a verified paired cross-die proxy: ' + name)
            if p['layer'] != 'metal7' or q['layer'] != 'metal7' or p['rect'] != q['rect']:
                raise ValueError('Unexpected HBT proxy geometry')
            for d in data.values():
                if ('PIN', name) not in d['nets'][p['net']]:
                    raise ValueError('Proxy not connected to its declared net')
            proxy_nets.add(p['net'])
            pad_sizes.add((p['rect'][2] - p['rect'][0], p['rect'][3] - p['rect'][1]))
        if proxy_nets != common or len(pad_sizes) != 1:
            raise ValueError('Proxy network set differs from shared network set')
        retained = {}
        for die, d in data.items():
            retained[die] = {n: tuple((i, p) for i, p in pairs if i != 'PIN') for n, pairs in d['nets'].items()}
            if any(not t for t in retained[die].values()):
                raise ValueError('Removing proxies created an empty local net')
            lines = ['NETS ' + str(len(d['nets'])) + ' ;']
            for name, body in d['net_records'].items():
                body = re.sub(r'\(\s*PIN\s+(\S+)\s*\)', '', body)
                lines.append('- ' + name + ' ' + body + ' ;')
            lines.append('END NETS')
            changes = [(d['pin_span'], 'PINS 0 ;\nEND PINS'), (d['net_span'], '\n'.join(lines))]
            text = d['text']
            for (begin, end), replacement in sorted(changes, reverse=True):
                text = text[:begin] + replacement + text[end:]
            target = output / (die + '.def')
            with target.open('x') as stream:
                stream.write(text)
            parsed, _ = section(text, 'NETS')
            if connections(parsed) != retained[die] or components(section(text, 'COMPONENTS')[0]) != d['components']:
                raise ValueError('Derived input changed real endpoints or placement')
        text = top['text']
        bounds = list(map(int, re.search(r'DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)', text).groups()))
        dbu = int(re.search(r'UNITS DISTANCE MICRONS\s+(\d+)', text)[1])
        tracks = re.search(r'TRACKS Y\s+(-?\d+)\s+DO\s+(\d+)\s+STEP\s+(\d+)\s+LAYER metal7', text)
        if not tracks:
            raise ValueError('Missing author metal7 track alignment')
        track_origin, _, track_pitch = map(int, tracks.groups())
        nominal_pitch = 3 * dbu
        xs = list(range(bounds[0] + nominal_pitch, bounds[2] - nominal_pitch + 1, nominal_pitch))
        ys, drift = [], 0
        for desired in range(bounds[1] + nominal_pitch, bounds[3] - nominal_pitch + 1, nominal_pitch):
            aligned = track_origin + math.ceil((desired + drift - track_origin) / track_pitch) * track_pitch
            if aligned > bounds[3] - nominal_pitch:
                break
            drift += aligned - (desired + drift)
            ys.append(aligned)
        if len(set(b - a for a, b in zip(ys, ys[1:]))) != 1:
            raise ValueError('Author HBT Y grid is not regular')
        author_log = (ROOT / 'results_h3d' / case / 'run.log').read_text()
        available = int(re.search(r'Num of available HBT:\s*(\d+)', author_log)[1])
        if len(xs) * len(ys) != available:
            raise ValueError(f'{case}: derived resource count differs from author: {len(xs)*len(ys)} != {available}')
        author_ports = {}
        for die, suffix in (('top', 'top'), ('bottom', 'btm')):
            p = ROOT / 'results_h3d' / case / f'{case}_{suffix}_hbt.place.def'
            author_ports[die] = pins(section(p.read_text(), 'PINS')[0])
            if any(v['position'][0] not in xs or v['position'][1] not in ys for v in author_ports[die].values()):
                raise ValueError('Author output uses points outside the reconstructed HBT grid')
        geometry = lambda value: {name: {k: v for k, v in pin.items() if k != 'net'} for name, pin in value.items()}
        if geometry(author_ports['top']) != geometry(author_ports['bottom']):
            raise ValueError('Author output HBT pairing differs')
        pad = list(next(iter(pad_sizes)))
        config = {'schema_version': 1, 'dbu_per_micron': dbu, 'coordinate_frame': 'bonded_xy',
                  'top_transform': 'identity', 'bottom_transform': 'identity',
                  'origin_dbu': [xs[0], ys[0]], 'pitch_dbu': [xs[1] - xs[0], ys[1] - ys[0]],
                  'site_counts': [len(xs), len(ys)], 'pad_size_dbu': pad, 'blocked_sites': [],
                  'landing_layers': {'top': 'metal7', 'bottom': 'metal7'},
                  'routing_layers': ['metal' + str(i) for i in range(1, 8)],
                  'author_nominal_hbt_pitch_um': 3.0,
                  'author_alignment_note': 'Full-pitch edge margin; Y is incrementally snapped up to metal7 tracks, matching native grid construction and all reference output HBTs.'}
        write_json(output / 'hbt.json', config)
        row = {'lef': str(lef), 'dbu': dbu, 'die_bounds_dbu': bounds, 'config': config,
               'original_nets': len(top['nets'].keys() | bottom['nets'].keys()), 'cross_die_nets': len(common),
               'instances': {d: len(value['components']) for d, value in data.items()},
               'real_endpoints': sum(len(t) for d in retained.values() for t in d.values()),
               'removed_proxy_ports_per_die': len(top['pins']), 'author_used_hbt': len(author_ports['top']),
               'author_available_hbt': available, 'proxy_mapping_checked': True,
               'derived_endpoints_and_placement_preserved': True}
        write_json(output / 'audit.json', row)
        audit[case] = row
        print(json.dumps({'case': case, 'nets': row['original_nets'], 'cross': len(common),
                          'grid': config['site_counts'], 'pitch_dbu': config['pitch_dbu'], 'available': available}), flush=True)
    write_json(folder / 'input_audit.json', audit)
    write_json(folder / 'binary_provenance.json', {'baseline': sha(folder / 'binaries/route_baseline'),
        'author': sha(AUTHOR / 'bin/route'), 'author_configs': {c: sha(AUTHOR / 'configs' / (c + '.json')) for c in CASES}})
    check_protected(folder)


def run(folder, label, binary, cases, ties, seed, coordinates, observations):
    check_protected(folder)
    runs = folder / label
    runs.mkdir(exist_ok=False)
    rows = []
    for case in cases:
        p = folder / 'inputs' / case
        config = json.loads((p / 'audit.json').read_text())
        case_out = runs / case
        case_out.mkdir()
        hbt_config = json.loads((p / 'hbt.json').read_text())
        hbt_config['hbt_optimization_coordinates'] = coordinates
        hbt_config['write_routing_traces'] = observations == 'full'
        write_json(case_out / 'hbt.json', hbt_config)
        cmd = [str(binary), '-lef', config['lef'], '-top-def', str(p / 'top.def'), '-bottom-def', str(p / 'bottom.def'),
               '-hbt-config', str(case_out / 'hbt.json'), '-output-dir', str(case_out / 'output'), '-threads', '1',
               '-h3d-method', 'paper', '-h3d-paper-ties', ties, '-h3d-assignment-trials', '1', '-h3d-seed', str(seed)]
        spec = {'argv': cmd, 'cwd': str(ROOT / 'flute'), 'binary_sha256': sha(binary),
                'hbt_config_sha256': sha(case_out / 'hbt.json')}
        write_json(case_out / 'command.json', spec)
        start = time.monotonic()
        with (case_out / 'run.log').open('x') as log:
            result = subprocess.run(['/usr/bin/time', '-v', '-o', str(case_out / 'time.log'), *cmd],
                                    cwd=ROOT / 'flute', stdout=log, stderr=subprocess.STDOUT, timeout=1800)
        row = {'case': case, 'return_code': result.returncode, 'wall_seconds': time.monotonic() - start}
        report_path = case_out / 'output/h3d_report.json'
        if report_path.exists():
            report = json.loads(report_path.read_text())
            routing = report['routing']
            row.update(status=report['status'], total_seconds=report['total_seconds'], hbt=report['physical_hbt_count'],
                       topological_hbt=report['topological_hbt_count'],
                       wirelength_dbu=sum(r['wirelength_dbu'] for r in routing.values()),
                       vias=sum(r['via_count'] for r in routing.values()),
                       wire_overflow=sum(r['wire_overflow_tracks'] for r in routing.values()),
                       demand_overflow=sum(r['demand_overflow_tracks'] for r in routing.values()),
                       cross_die_nets=report['cross_die_net_count'], nets=report['completed_input_nets'],
                       connectivity_checked=report['connectivity_checked'], available=report['initial_available_hbt_sites'])
            row['wirelength_um'] = row['wirelength_dbu'] / config['dbu']
            row['wire_length_metric'] = row['wirelength_dbu'] // round(0.19 * config['dbu'])
        else:
            failed = case_out / 'output/h3d_failure_report.json'
            row.update(status='failed', failure=json.loads(failed.read_text())['error'] if failed.exists() else 'see log')
        write_json(case_out / 'metrics.json', row)
        rows.append(row)
        print(json.dumps(row), flush=True)
    write_json(runs / 'summary.json', rows)
    check_protected(folder)


def compare(folder, label):
    refs = {r['benchmark']: r for r in csv.DictReader((ROOT / 'results_h3d/global_routing_summary.csv').open())}
    rows = []
    for ours in json.loads((folder / label / 'summary.json').read_text()):
        ref = refs[ours['case']]
        row = {'case': ours['case'], 'status': ours['status']}
        for key, native in (('hbt', 'used_hbt'), ('wire_length_metric', 'wire_length_metric'), ('vias', 'via_count'), ('wire_overflow', 'wire_overflow')):
            original = float(ref[native])
            row.update({key + '_author': original, key + '_ours': ours.get(key),
                        key + '_change_pct': (ours[key] / original - 1) * 100 if original and key in ours else None})
        row.update(author_wall_seconds=float(ref['wall_seconds']), ours_wall_seconds=ours['wall_seconds'])
        rows.append(row)
    write_json(folder / label / 'comparison.json', rows)
    with (folder / label / 'comparison.csv').open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(json.dumps(rows, indent=2))


def native_reference(folder, label, cases):
    """Recheck the supplied binary in a new working/output directory."""
    check_protected(folder)
    output = folder / label
    output.mkdir(exist_ok=False)
    work = output / 'work'
    work.mkdir()
    for name in ('POWV9.dat', 'POST9.dat'):
        shutil.copy2(AUTHOR / 'bin' / name, work / name)
    write_json(output / 'lookup_tables.json', {name: {'author': sha(work / name), 'ours': sha(ROOT / 'flute' / name)}
                                              for name in ('POWV9.dat', 'POST9.dat')})
    rows = []
    for case in cases:
        destination = output / case
        destination.mkdir()
        config = json.loads((AUTHOR / 'configs' / (case + '.json')).read_text())
        command = [str(AUTHOR / 'bin/route'), '--lef', str(ROOT / 'benchmarks_h3d' / Path(config['lef']).name),
                   '--bdef', str(ROOT / 'benchmarks_h3d' / case / Path(config['bdef']).name),
                   '--tdef', str(ROOT / 'benchmarks_h3d' / case / Path(config['tdef']).name),
                   '--output', str(destination / (case + '.guide'))]
        write_json(destination / 'command.json', {'argv': command, 'cwd': str(work), 'binary_sha256': sha(AUTHOR / 'bin/route')})
        start = time.monotonic()
        with (destination / 'run.log').open('x') as log:
            result = subprocess.run(['/usr/bin/time', '-v', '-o', str(destination / 'time.log'), *command],
                                    cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=600)
        text = (destination / 'run.log').read_text()
        if result.returncode or 'Terminated.' not in text:
            raise RuntimeError('Author binary rerun failed: ' + case)
        row = {'case': case, 'return_code': result.returncode, 'wall_seconds': time.monotonic() - start,
               'wire_length_metric': int(re.findall(r'wire length \(metric\):\s*(\d+)', text)[-1]),
               'vias': int(re.findall(r'total via count:\s*(\d+)', text)[-1]),
               'wire_overflow': int(re.findall(r'total wire overflow:\s*(\d+)', text)[-1]),
               'hbt': int(re.findall(r'num used HBT:\s*(\d+)', text)[-1]),
               'router_elapsed_seconds': float(re.findall(r'\[\s*([\d.]+)\]\s*Terminated\.', text)[-1])}
        rows.append(row)
        write_json(destination / 'metrics.json', row)
        print(json.dumps(row), flush=True)
    write_json(output / 'summary.json', rows)
    check_protected(folder)


def fixed_hbt_control(folder, label, binary, cases, source='author'):
    """Isolate single-die routing by retaining all author-placed HBTs/subnets."""
    check_protected(folder)
    output = folder / label
    output.mkdir(exist_ok=False)
    rows = []
    for case in cases:
        audit = json.loads((folder / 'inputs' / case / 'audit.json').read_text())
        row = {'case': case, 'wire_length_metric': 0, 'vias': 0, 'wire_overflow': 0}
        for die, suffix in (('bottom', 'btm'), ('top', 'top')):
            prefix = output / (case + '_' + die)
            input_def = (ROOT / 'results_h3d' / case / f'{case}_{suffix}_hbt.place.def') if source == 'author' else (folder / source / case / 'output' / (die + '.def'))
            command = [str(binary), '-lef', audit['lef'], '-def', str(input_def),
                       '-output', str(prefix.with_suffix('.guide')), '-threads', '1']
            write_json(prefix.with_suffix('.command.json'), {'argv': command, 'binary_sha256': sha(binary)})
            with prefix.with_suffix('.log').open('x') as log:
                p = subprocess.run(command, cwd=ROOT / 'flute', stdout=log, stderr=subprocess.STDOUT, timeout=600)
            text = prefix.with_suffix('.log').read_text()
            if p.returncode or 'Terminated.' not in text:
                raise RuntimeError('Fixed-HBT routing failed: ' + str(prefix))
            row['wire_length_metric'] += int(re.findall(r'wire length \(metric\):\s*(\d+)', text)[-1])
            row['vias'] += int(re.findall(r'total via count:\s*(\d+)', text)[-1])
            row['wire_overflow'] += int(re.findall(r'total wire overflow:\s*(\d+)', text)[-1])
        rows.append(row)
        print(json.dumps(row), flush=True)
    write_json(output / 'summary.json', rows)
    check_protected(folder)


def readback(folder, label, binary, cases, source):
    check_protected(folder)
    output = folder / label
    output.mkdir(exist_ok=False)
    verified = {r['case']: r for r in json.loads((folder / ('verification_' + source + '.json')).read_text())['cases']}
    rows = []
    for case in cases:
        source_dir = folder / source / case
        audit = json.loads((folder / 'inputs' / case / 'audit.json').read_text())
        case_out = output / case
        case_out.mkdir()
        command = [str(binary), '-lef', audit['lef'], '-top-def', str(source_dir / 'output/top.def'),
                   '-bottom-def', str(source_dir / 'output/bottom.def'), '-hbt-config', str(source_dir / 'hbt.json'),
                   '-output-dir', str(case_out / 'output'), '-h3d-init-only', '-threads', '1']
        write_json(case_out / 'command.json', {'argv': command, 'binary_sha256': sha(binary)})
        with (case_out / 'run.log').open('x') as stream:
            p = subprocess.run(command, cwd=ROOT / 'flute', stdout=stream, stderr=subprocess.STDOUT, timeout=300)
        if p.returncode:
            raise RuntimeError('Native DEF readback failed: ' + case)
        stats = json.loads((case_out / 'output/input_statistics.json').read_text())
        check = verified[case]
        if stats['original_endpoint_count'] != check['original_endpoints'] + 2 * check['hbt_pairs']:
            raise RuntimeError('DEF re-import lost terminals: ' + case)
        if stats['original_net_count'] != sum(check['subnets'].values()):
            raise RuntimeError('DEF re-import lost subnets: ' + case)
        row = {'case': case, 'status': 'passed', 'loaded_terminals': stats['original_endpoint_count'],
               'loaded_subnets': stats['original_net_count'], 'mode': 'initialization_only; no routing/DRC claim'}
        rows.append(row)
        print(json.dumps(row), flush=True)
    write_json(output / 'summary.json', rows)
    check_protected(folder)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=('prepare', 'run', 'compare', 'fixed_hbt', 'reference', 'readback'))
    p.add_argument('--experiment', type=Path, default=ROOT / 'eval/h3d_author/run_001')
    p.add_argument('--label', default='baseline_first')
    p.add_argument('--binary', type=Path)
    p.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    p.add_argument('--ties', choices=('first', 'coalesce'), default='first')
    p.add_argument('--seed', type=int, default=0)
    p.add_argument('--coordinates', choices=('physical', 'gcell_centers'), default='physical')
    p.add_argument('--observations', choices=('full', 'summary'), default='full')
    p.add_argument('--background', action='store_true')
    p.add_argument('--fixed-source', default='author')
    p.add_argument('--input-label', default='verified_summary')
    args = p.parse_args()
    folder = args.experiment.resolve()
    if args.phase == 'prepare':
        prepare(folder)
    elif args.phase == 'run':
        if args.background:
            command = [sys.executable, '-B', str(Path(__file__).resolve()), *[a for a in sys.argv[1:] if a != '--background']]
            with (folder / (args.label + '.launcher.log')).open('x') as stream:
                worker = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            write_json(folder / (args.label + '.launcher.json'), {'pid': worker.pid, 'argv': command})
            print(json.dumps({'pid': worker.pid, 'label': args.label}))
        else:
            run(folder, args.label, (args.binary or folder / 'binaries/route_baseline').resolve(), args.cases, args.ties, args.seed, args.coordinates, args.observations)
    elif args.phase == 'fixed_hbt':
        fixed_hbt_control(folder, args.label, (args.binary or ROOT / 'build/route').resolve(), args.cases, args.fixed_source)
    elif args.phase == 'reference':
        native_reference(folder, args.label, args.cases)
    elif args.phase == 'readback':
        readback(folder, args.label, (args.binary or ROOT / 'build/route').resolve(), args.cases, args.input_label)
    else:
        compare(folder, args.label)
