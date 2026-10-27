#!/usr/bin/env python3
"""Independent endpoint/HBT/Guide and algorithm-trace checks for author cases."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import time

from h3d_author_benchmarks import ROOT, CASES, check_protected, connections, section, pins, components, sha, write_json
from validate_h3d_aes import independent_mask_dp, assignment_cost
from validate_h3d_paper import verify_location, verify_partition


def require(value, message):
    if not value:
        raise ValueError(message)


class Union:
    def __init__(self):
        self.parent = {}

    def root(self, key):
        self.parent.setdefault(key, key)
        trail = []
        while self.parent[key] != key:
            trail.append(key)
            key = self.parent[key]
        for p in trail:
            self.parent[p] = key
        return key

    def join(self, a, b):
        self.parent[self.root(a)] = self.root(b)


def instance_name(name):
    return re.sub(r'\\(.)', r'\1', name)


def normalized_components(raw):
    result = {instance_name(n): value for n, value in raw.items()}
    require(len(result) == len(raw), 'Component identifier decoding collision')
    return result


def normalized_connections(raw, aliases=None):
    aliases = aliases or {}
    return {n: tuple((aliases.get(instance_name(i), instance_name(i)) if i != 'PIN' else i, p) for i, p in terms)
            for n, terms in connections(raw).items()}


def source(folder, case):
    result, expected = {}, defaultdict(list)
    for die in ('top', 'bottom'):
        text = (folder / 'inputs' / case / (die + '.def')).read_text()
        ns = normalized_connections(section(text, 'NETS')[0])
        raw_components = components(section(text, 'COMPONENTS')[0])
        result[die] = {'components': normalized_components(raw_components), 'raw_components': raw_components,
                       'nets': ns, 'text': text}
        for n, terms in ns.items():
            expected[n].extend((die, i, p) for i, p in terms)
    return result, expected


def check_guides(path, expected, bounds):
    names, rectangles = set(), 0
    with path.open() as stream:
        while True:
            line = stream.readline()
            if not line:
                break
            name = line.strip()
            if not name:
                continue
            require(name in expected and name not in names, 'Unknown/duplicate Guide net: ' + name)
            names.add(name)
            require(stream.readline().strip() == '(', 'Missing Guide opening parenthesis')
            count = 0
            for line in stream:
                if line.strip() == ')':
                    break
                fields = line.split()
                require(len(fields) == 5 and fields[4] in {'metal' + str(i) for i in range(1, 8)}, 'Malformed Guide rectangle')
                x1, y1, x2, y2 = map(int, fields[:4])
                require(bounds[0] <= x1 < x2 <= bounds[2] and bounds[1] <= y1 < y2 <= bounds[3], 'Guide rectangle out of bounds')
                count += 1
            else:
                raise ValueError('Unterminated Guide')
            require(count > 0, 'Empty Guide block')
            rectangles += count
    require(names == set(expected), 'Guide/DEF net sets differ')
    return rectangles


def output_check(folder, case, label):
    original, expected = source(folder, case)
    audit = json.loads((folder / 'inputs' / case / 'audit.json').read_text())
    config = audit['config']
    output = ROOT / 'results_h3d' / case if label == 'author' else folder / label / case / 'output'
    net_map, ports, endpoint_net, rectangles, renames = {}, {}, {}, {}, []
    union = Union()
    original_endpoints = {term for terms in expected.values() for term in terms}
    for die, suffix in (('top', 'top'), ('bottom', 'btm')):
        def_name = f'{case}_{suffix}_hbt.place.def' if label == 'author' else die + '.def'
        guide_name = f'{case}_{suffix}.guide' if label == 'author' else die + '.guide'
        text = (output / def_name).read_text()
        raw_components = components(section(text, 'COMPONENTS')[0])
        observed_components = normalized_components(raw_components)
        aliases = {}
        if label == 'author':
            # The supplied binary's writer emits decoded component names (one
            # escape level lost). Require a strict one-to-one spelling mapping
            # AND identical master/status/position/orientation; do not silently
            # equate arbitrary names or hide a real placement change.
            for raw_name, attributes in original[die]['raw_components'].items():
                candidates = {raw_name, instance_name(raw_name)} & raw_components.keys()
                candidates = {n for n in candidates if raw_components[n] == attributes}
                require(len(candidates) == 1, 'Author component cannot be matched unambiguously')
                observed_name = candidates.pop()
                aliases[instance_name(observed_name)] = instance_name(raw_name)
                if raw_name != observed_name:
                    renames.append({'die': die, 'input_spelling': raw_name, 'author_output_spelling': observed_name})
            require(len(aliases) == len(raw_components), 'Author added/lost components')
            observed_components = {aliases[n]: v for n, v in observed_components.items()}
        require(observed_components == original[die]['components'], 'Placement or master changed')
        net_map[die] = normalized_connections(section(text, 'NETS')[0], aliases)
        ports[die] = pins(section(text, 'PINS')[0])
        for net, terms in net_map[die].items():
            require(terms, 'Empty output net')
            union.root((die, net))
            for inst, pin in terms:
                key = die, inst, pin
                require(key not in endpoint_net, 'Duplicated physical endpoint')
                endpoint_net[key] = net
                if inst == 'PIN':
                    require(pin in ports[die] and ports[die][pin]['net'] == net, 'Port NET attribute differs from NETS')
        rectangles[die] = check_guides(output / guide_name, net_map[die], audit['die_bounds_dbu'])
    require(ports['top'].keys() == ports['bottom'].keys(), 'Unpaired HBTs')
    used_positions = set()
    sx, sy = config['pad_size_dbu']
    ox, oy = config['origin_dbu']
    px, py = config['pitch_dbu']
    nx, ny = config['site_counts']
    for name, p in ports['top'].items():
        q = ports['bottom'][name]
        require({k: v for k, v in p.items() if k != 'net'} == {k: v for k, v in q.items() if k != 'net'}, 'HBT paired geometry differs')
        x, y = p['position']
        require(p['layer'] == 'metal7' and p['rect'] == [-sx // 2, -sy // 2, sx - sx // 2, sy - sy // 2], 'HBT shape/layer differs')
        require((x - ox) % px == (y - oy) % py == 0 and 0 <= (x - ox) // px < nx and 0 <= (y - oy) // py < ny,
                'HBT is outside author-aligned legal array')
        require((x, y) not in used_positions, 'Two HBT ports occupy the same physical site')
        used_positions.add((x, y))
        require(('top', 'PIN', name) in endpoint_net and ('bottom', 'PIN', name) in endpoint_net, 'Unused HBT port')
        union.join(('top', p['net']), ('bottom', q['net']))
    generated = {(die, 'PIN', name) for die in ports for name in ports[die]}
    require(set(endpoint_net) == original_endpoints | generated, 'Lost or unexpected original endpoint')
    roots = set()
    for name, terms in expected.items():
        connected = {union.root((die, endpoint_net[die, inst, pin])) for die, inst, pin in terms}
        require(len(connected) == 1, 'Disconnected original net: ' + name)
        root = connected.pop()
        require(root not in roots, 'Different original nets shorted together')
        roots.add(root)
    require(all(union.root((die, net)) in roots for die in net_map for net in net_map[die]), 'Floating subnet with no original net')
    return {'status': 'passed', 'case': case, 'label': label, 'original_nets': len(expected),
            'original_endpoints': len(original_endpoints), 'hbt_pairs': len(used_positions),
            'subnets': {d: len(n) for d, n in net_map.items()}, 'guide_rectangles': rectangles,
            'placement_preserved': True, 'lost_endpoints': 0, 'disconnected_original_nets': 0,
            'mixed_original_nets': 0, 'hbt_site_conflicts': 0,
            'author_instance_spelling_changes': renames}


def routing_points(text, bounds):
    axes = []
    for axis, low, high in (('X', bounds[0], bounds[2]), ('Y', bounds[1], bounds[3])):
        lines = {low, high}
        for start, count, step in re.findall(r'GCELLGRID\s+' + axis + r'\s+(-?\d+)\s+DO\s+(\d+)\s+STEP\s+(\d+)', text):
            lines.update(int(start) + i * int(step) for i in range(1, int(count)))
        ordered = sorted(lines)
        axes.append([(a + b) // 2 for a, b in zip(ordered, ordered[1:])])
    return {y * len(axes[0]) + x: [px, py] for y, py in enumerate(axes[1]) for x, px in enumerate(axes[0])}


def trace_check(folder, case, label, samples):
    original, expected = source(folder, case)
    output = folder / label / case / 'output'
    report = json.loads((output / 'h3d_report.json').read_text())
    grid = json.loads((output / 'hbt_grid_initial.json').read_text())['sites']
    points = {s['id']: s['position_dbu'] for s in grid}
    owners = {s['id']: s['initial_owner'] for s in grid}
    physical = {s['id']: s.get('physical_position_dbu', s['position_dbu']) for s in grid}
    bounds = json.loads((folder / 'inputs' / case / 'audit.json').read_text())['die_bounds_dbu']
    routing = routing_points(original['bottom']['text'], bounds)
    cross_total = report['cross_die_net_count']
    step = max(1, math_ceil(cross_total, samples))
    completed, cross, components_checked, tables, states, locations_checked = 0, 0, 0, 0, 0, 0
    free = sum(owner == -1 for owner in owners.values())
    seen, claims = set(), set()
    with (output / 'routing_traces.jsonl').open() as stream:
        for line in stream:
            r = json.loads(line)
            require(r['status'] == 'completed' and r['name'] not in seen, 'Failed or duplicate trace')
            seen.add(r['name'])
            is_cross = r['name'] in original['top']['nets'] and r['name'] in original['bottom']['nets']
            require(r['hbt_count_dp_applied'] == is_cross, 'Wrong single/multi-die dispatch')
            tree = r['planar']
            require(independent_mask_dp(tree['nodes'], tree['edges']) == r['hbt_count'], 'Independent quantity DP differs')
            require(assignment_cost(tree['nodes'], tree['edges'], r['edge_dies']) == r['hbt_count'], 'Assignment traceback not optimal')
            components_checked += verify_partition(r)
            if is_cross:
                require(r['hbt_free_sites_before'] == free, 'HBT free-site accounting differs')
                if cross % step == 0 and len(r['expanded']['nodes']) <= 256:
                    a, b, _ = verify_location(r, points, owners, routing)
                    tables += a
                    states += b
                    locations_checked += 1
                for node, site in zip(r['hbt_nodes'], r['hbt_sites']):
                    require(owners[site] == -1 and r['positions_dbu'][node] == points[site], 'HBT occupancy or grid projection differs')
                for site in set(r['hbt_sites']):
                    owners[site] = r['id']
                    claims.add(site)
                    free -= 1
                cross += 1
            else:
                require(r['hbt_count'] == 0 and not r['hbt_sites'], 'Single-die net allocated an HBT')
            completed += 1
    require(seen == set(expected) and completed == report['completed_input_nets'], 'Incomplete algorithm trace')
    require(cross == cross_total and len(claims) == report['physical_hbt_count'], 'HBT final counts differ')
    for h in report['hbts']:
        require(h['position_dbu'] == physical[h['site']] and owners[h['site']] == h['owner_net_id'], 'Exported physical HBT differs from optimized resource identity')
    return {'status': 'passed', 'all_quantity_dp_nets': completed, 'all_decomposition_components': components_checked,
            'all_cross_die_occupancy_nets': cross, 'sampled_location_nets': locations_checked,
            'sampled_eq6_eq7_tables': tables, 'sampled_candidate_costs': states,
            'location_sampling': f'every ceil(cross_net_count/{samples})-th net, expanded node count <=256; no claim of exhaustive real-case Eq7 verification'}


def math_ceil(value, divisor):
    return (value + divisor - 1) // divisor


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--experiment', type=Path, required=True)
    p.add_argument('--label', default='aligned_paper')
    p.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    p.add_argument('--trace', action='store_true')
    p.add_argument('--location-samples', type=int, default=12)
    p.add_argument('--compare-label', help='Require byte-identical DEF/Guide outputs against another profile')
    args = p.parse_args()
    folder = args.experiment.resolve()
    check_protected(folder)
    rows = []
    for case in args.cases:
        start = time.monotonic()
        row = output_check(folder, case, args.label)
        if args.trace:
            row['algorithm'] = trace_check(folder, case, args.label, args.location_samples)
        if args.compare_label:
            row['outputs_byte_identical'] = {name: sha(folder / args.label / case / 'output' / name) ==
                sha(folder / args.compare_label / case / 'output' / name) for name in ('top.def', 'bottom.def', 'top.guide', 'bottom.guide')}
            require(all(row['outputs_byte_identical'].values()), 'Observation/performance mode changed routing outputs')
        row['wall_seconds'] = time.monotonic() - start
        rows.append(row)
        print(json.dumps(row), flush=True)
    destination = folder / ('verification_' + args.label + ('_trace' if args.trace else '') + '.json')
    write_json(destination, {'status': 'passed', 'cases': rows, 'author_inputs_and_results_unchanged': True})
    check_protected(folder)


if __name__ == '__main__':
    main()
