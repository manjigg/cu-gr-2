#!/usr/bin/env python3
"""Convert Open3DBench Place-LoL AES 3D placement into die-wise DEF files.

No HBT is committed here. Each die contains the original net name with the
local endpoints; 3d_nets.json is the authoritative bridge between both dies.
External DEF PINS are assigned to Bottom Die by an explicit convention.
Run from Place-LoL: python h3d_aes_convert.py --root . --out ./h3d_aes_input
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import shutil
from decimal import Decimal
from pathlib import Path

ENTRY_RE = re.compile(r'^\s*-\s+(\S+)\s+(.*)$', re.DOTALL)
CONNECTION_RE = re.compile(r'\(\s*(\S+)\s+(\S+)\s*\)')
PLACEMENT_RE = re.compile(r'\+\s+(?:PLACED|FIXED|COVER|UNPLACED)\s*\(\s*-?\d+\s+-?\d+\s*\)\s+[A-Z]+', re.IGNORECASE)
ORIENT = {'R0':'N', 'R90':'W', 'R180':'S', 'R270':'E', 'R360':'N',
          'N':'N', 'S':'S', 'E':'E', 'W':'W', 'FN':'FN', 'FS':'FS', 'FE':'FE', 'FW':'FW'}
# Official converter maps R90->E, R270->W; keep its convention to compare
ORIENT['R90'] = 'E'
ORIENT['R270'] = 'W'


def load_def(path: Path):
    text = path.read_text(encoding='utf-8')
    lines = text.splitlines()
    spans = {}
    for section in ('COMPONENTS','PINS','NETS'):
        starts = [i for i, line in enumerate(lines) if re.match(fr'^{section}\s+\d+\s*;', line)]
        assert len(starts)==1, f'Expected one {section} section in {path}'
        start = starts[0]
        ends = [i for i in range(start+1,len(lines)) if lines[i].strip()==f'END {section}']
        assert ends, f'Missing END {section}'
        spans[section]=(start,ends[0])
    entries={}
    for sect,(start,end) in spans.items():
        arr=[];chunk=[]
        for line in lines[start+1:end]:
            if re.match(r'^\s*-\s+',line):
                if chunk:
                    assert chunk[-1].strip().endswith(';'),f'Unterminated {sect}: {chunk[0]}'
                    arr.append('\n'.join(chunk));chunk=[]
            if line.strip():chunk.append(line)
        if chunk:arr.append('\n'.join(chunk))
        expected=int(lines[start].split()[1])
        if expected!=len(arr):
            print(f'[NOTICE] {path.name} {sect}: header says {expected}, actual {len(arr)}; rebuilding counts')
        entries[sect]=arr
    return lines,spans,entries


def entry_name(entry):
    found = ENTRY_RE.search(entry)
    assert found, f'Bad DEF item: {entry[:80]}'
    return found.group(1)


def read_placement(path: Path):
    partition={}; coords={}; terminal={}; counts={}; tier=None
    for raw in path.read_text(encoding='utf-8').splitlines():
        v=raw.split()
        if not v:continue
        if v[0] in ('TopDiePlacement','BottomDiePlacement'):
            tier='top' if v[0].startswith('Top') else 'bottom'
            counts[tier]=int(v[1]);continue
        if v[0]=='Inst':
            assert tier is not None and len(v) in (4,5),v
            cid=v[1]
            assert cid not in partition,cid
            assert v[4] in ORIENT if len(v)==5 else True, v
            partition[cid]=tier
            coords[cid]=(20*int(v[2]),20*int(v[3]),ORIENT[v[4]] if len(v)==5 else 'N')
        if v[0]=='Terminal':
            assert len(v)==4 and v[1] not in terminal,v
            terminal[v[1]]=(20*int(v[2]),20*int(v[3]))
    assert counts==dict(collections.Counter(partition.values())),(counts,collections.Counter(partition.values()))
    return partition,coords,terminal


def parse_component(item):
    words=item.split()
    assert words[0]=='-' and len(words)>4,(words[:6])
    return words[1],words[2]


def parse_net(item):
    match=ENTRY_RE.match(item)
    assert match
    net_name,other=match.groups()
    assert item.rstrip().endswith(';'),net_name
    head=re.split(r'\s\+\s',other, maxsplit=1)
    endpoints=CONNECTION_RE.findall(head[0])
    assert endpoints,f'No endpoints on {net_name}'
    # The provided original AES nets only carry + USE; we retain this section.
    props=' + '+head[1].strip() if len(head)>1 else ''
    if not props.strip():props=''
    props=props.rstrip(';').strip()
    return net_name,endpoints,props


def parse_pin(item):
    match=ENTRY_RE.match(item)
    assert match
    net=re.search(r'\+\s+NET\s+(\S+)',item)
    assert net,f'No NET attached to DEF port {match.group(1)}'
    return match.group(1),net.group(1)


def read_lef_macros(path:Path):
    macros={};name=None
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('MACRO '):
            assert name is None
            name=line.split()[1];macros[name]=set()
        elif name and re.match(r'^\s+PIN\s+\S+',line):
            macros[name].add(line.split()[1])
        elif name and line.strip()==f'END {name}':
            name=None
    return macros


def emit_def(path, header, components, ports, nets, die_name):
    # Header is original source up to COMPONENTS; all TECH/TRACKS records retained.
    # If the source uses 10 routing layers, we output tracks 1-6 for the pilot.
    x=[]
    for line in header:
        if line.startswith('DESIGN '):
            name=line.split()[1]
            x.append(f'DESIGN {name}_{die_name} ;')
        elif line.startswith('TRACKS ') and re.search(r'\bLAYER\s+metal(?:[7-9]|10)\b',line):
            continue
        else:
            x.append(line)
    x.extend([f'COMPONENTS {len(components)} ;'])
    x.extend(components)
    x.extend(['END COMPONENTS', f'PINS {len(ports)} ;'])
    x.extend(ports)
    x.extend(['END PINS',f'NETS {len(nets)} ;'])
    x.extend(nets)
    x.extend(['END NETS','END DESIGN',''])
    path.write_text('\n'.join(x),encoding='utf-8')


def hbt_grid_spec(header, placer_input_text=None, pitch_um=None):
    """Resolve a research grid from an actual input spec or an explicit override.

    TerminalSize+TerminalSpacing is the placer model's minimum center spacing,
    not proof of a prefabricated regular array or a verified bonding process.
    """
    unit = re.search(r'UNITS DISTANCE MICRONS\s+(\d+)', header)
    area = re.search(r'DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)', header)
    if not unit or not area:
        raise ValueError('Missing source DEF units or DIEAREA')
    dbu = int(unit[1]); diearea = list(map(int, area.groups()))
    if dbu != 2000:
        raise ValueError('This converter uses 20 DBU per 0.01 um placer unit and requires DEF DBU=2000')
    source = None
    if placer_input_text is not None:
        size = re.search(r'^TerminalSize\s+(\d+)\s+(\d+)', placer_input_text, re.M)
        spacing = re.search(r'^TerminalSpacing\s+(\d+)', placer_input_text, re.M)
        bounds = re.search(r'^DieSize\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)', placer_input_text, re.M)
        if not size or not spacing or not bounds:
            raise ValueError('Placer input lacks TerminalSize, TerminalSpacing or DieSize')
        core = [20 * int(value) for value in bounds.groups()]
        if not (diearea[0] <= core[0] < core[2] <= diearea[2] and diearea[1] <= core[1] < core[3] <= diearea[3]):
            raise ValueError('Placer DieSize must lie inside source DEF DIEAREA')
        sx, sy, gap = int(size[1]), int(size[2]), int(spacing[1])
        if sx <= 0 or sy <= 0 or gap < 0 or sx != sy:
            raise ValueError('Expected positive square terminals and nonnegative spacing for the scalar-pitch schema')
        source = {'terminal_size_dbu': [20 * sx, 20 * sy], 'terminal_spacing_dbu': 20 * gap,
                  'minimum_center_spacing_dbu': 20 * (sx + gap), 'placement_core_bounds_dbu': core}
    if pitch_um is None and source is None:
        raise ValueError('HBT pitch is not known: provide the actual placer .input via --placer-input, or explicitly set --hbt-pitch-um; no implicit 3 um default')
    if pitch_um is not None:
        scaled = Decimal(str(pitch_um)) * dbu
        if not scaled.is_finite() or scaled <= 0 or scaled != scaled.to_integral_value():
            raise ValueError('HBT pitch must be positive and exactly representable in source DBU')
        pitch = int(scaled)
        kind = 'explicit_research_pitch_override'
    else:
        pitch = source['minimum_center_spacing_dbu']
        kind = 'placer_minimum_spacing_research_array'
    if source and pitch < source['minimum_center_spacing_dbu']:
        raise ValueError('Requested grid pitch is smaller than source TerminalSize+TerminalSpacing')
    offset = max(pitch // 2, source['terminal_size_dbu'][0] // 2 + source['terminal_spacing_dbu']) if source else pitch // 2
    return {'pitch_dbu': pitch, 'diearea_dbu': diearea, 'dbu_per_micron': dbu,
            'origin_dbu': [diearea[0] + offset, diearea[1] + offset],
            'provenance': {'kind': kind, 'placer_terminal_model': source,
                           'array_origin': 'max(half pitch, half terminal size + spacing) from DEF lower bounds; regular-array modeling convention, not source terminal coordinates',
                           'bounds': 'DEF DIEAREA is the outline; placer DieSize may be a contained row-based placement core'}}


def run(root:Path,out:Path,placer_input=None,hbt_pitch_um=None):
    place_file=root/'binaries/iccad2022/cadb1021/output/default/aes.txt'
    org_def=root/'benchmarks/3D_input/aes/2_2_floorplan_io.def'
    ref_def=root/'binaries/converted_output/default/cadb1021/aes.def'
    tech_lef=root/'benchmarks/nangate45/lef/NangateOpenCellLibrary.tech.lef'
    cell_lef=root/'benchmarks/nangate45/lef/NangateOpenCellLibrary.macro.mod.lef'
    for p in (place_file,org_def,ref_def,tech_lef,cell_lef):
        if not p.is_file():raise FileNotFoundError(f'Required: {p}')
    rows,sections,src=load_def(org_def)
    header=rows[:sections['COMPONENTS'][0]]
    candidate=placer_input if placer_input is not None else root/'binaries/converted_input/default/aes.input'
    if placer_input is not None and not candidate.is_file():raise FileNotFoundError(candidate)
    spec=hbt_grid_spec('\n'.join(header),candidate.read_text() if candidate.is_file() else None,hbt_pitch_um)
    if candidate.is_file():spec['provenance']['placer_input_path']=str(candidate.resolve())
    out.mkdir(parents=True,exist_ok=False)
    _,_,converted=load_def(ref_def)
    partitions,placements,prior_hbts=read_placement(place_file)
    old_comp=src['COMPONENTS']
    conv_comp=[x for x in converted['COMPONENTS'] if not entry_name(x).startswith('HBT_')]
    assert len(old_comp)==len(conv_comp)==len(partitions),'DEF to placer node count mismatch'
    assert [entry_name(x) for x in old_comp]==[entry_name(x) for x in conv_comp], 'DEF order mismatch; C_i mapping invalid'
    assert len(prior_hbts)==sum(entry_name(x).startswith('HBT_') for x in converted['COMPONENTS'])

    original_names=[]; real_inst_die={};inst_types={};out_components={'top':[],'bottom':[]};map_c_to_orig={}
    for idx,(item,check_item) in enumerate(zip(old_comp,conv_comp),1):
        cid='C'+str(idx)
        name,macro=parse_component(item)
        tier=partitions[cid]
        x,y,o=placements[cid]
        ref_pos=re.search(r'\+\s+PLACED\s*\(\s*(\d+)\s+(\d+)\s*\)\s+(\S+)',check_item)
        assert ref_pos and (x,y,o)==(int(ref_pos[1]),int(ref_pos[2]),ref_pos[3]),f'Placement mismatch: {cid}, {name}'
        check_macro=parse_component(check_item)[1]
        assert check_macro==macro+('_upper' if tier=='top' else '_bottom'),f'Wrong die mapping for {name}'
        real_inst_die[name]=tier
        inst_types[name]=macro
        map_c_to_orig[cid]=name
        original_names.append(name)
        placement=re.search(r'\+\s+(?:PLACED|FIXED)\s*\(\s*[-\d]+\s+[-\d]+\s*\)\s+\w+',item)
        assert placement, f'Unsupported placement: {name}'
        # 3D placement stage positions replace unplaced 2D source coordinates.
        new_item=item[:placement.start()]+f'+ PLACED ( {x} {y} ) {o}'+item[placement.end():]
        out_components[tier].append(new_item)

    lib_macros=read_lef_macros(cell_lef)
    for n,typ in inst_types.items():
        assert typ in lib_macros,f'Missing LEF macro {typ} ({n})'

    orig_pins={}; pins_bottom=[]
    for item in src['PINS']:
        name,net=parse_pin(item)
        assert name not in orig_pins
        orig_pins[name]=net
        pins_bottom.append(item)

    out_nets={'top':[],'bottom':[]}
    original_nets={}; cross=[]; cross_ids=[]; die_connections={'top':0,'bottom':0}
    term_count=0;io_cross=0;skipped_terminal=[]
    for net_idx,item in enumerate(src['NETS']):
        net_name,endpoints,props=parse_net(item)
        assert net_name not in original_nets,f'Duplicate NET {net_name}'
        original_nets[net_name]=endpoints
        by_die={'top':[],'bottom':[]};has_io=False
        for inst,pin in endpoints:
            if inst=='PIN':
                assert pin in orig_pins and orig_pins[pin]==net_name,(net_name,pin,orig_pins.get(pin))
                tier='bottom';has_io=True
            else:
                assert inst in real_inst_die,(net_name,inst)
                tier=real_inst_die[inst]
                assert pin in lib_macros[inst_types[inst]],f'Unknown LEF pin {inst}/{pin} on {net_name}'
            by_die[tier].append((inst,pin))
            die_connections[tier]+=1
        for tier,local_ends in by_die.items():
            if local_ends:
                conn=' '.join(f'( {i} {p} )' for i,p in local_ends)
                suffix=f' {props}' if props else ''
                out_nets[tier].append(f'  - {net_name} {conn}{suffix} ;')
        if by_die['top'] and by_die['bottom']:
            # For a 2022 contest Net N[k], k starts at one and maps
            # to original DEF net at zero-based index k-1.
            term_key='N'+str(net_idx+1)
            known=term_key in prior_hbts
            if known:term_count+=1
            else:skipped_terminal.append(net_name)
            if has_io:io_cross+=1
            cross_ids.append(net_idx)
            cross.append({
                'original_net_name':net_name,
                'source_net_index':net_idx,
                'placer_net_id':term_key,
                'top_pins':[{'instance':i,'pin':p} for i,p in by_die['top']],
                'bottom_pins':[{'instance':i,'pin':p} for i,p in by_die['bottom']],
                'contains_external_io':has_io,
                'had_preassigned_terminal':known,
                **({'prior_terminal_reference_dbu':list(prior_hbts[term_key])} if known else {})
            })
    # Every original NET and every original connection must be preserved.
    expected=set(original_nets)
    top_netnames={entry_name(v) for v in out_nets['top']}
    bottom_netnames={entry_name(v) for v in out_nets['bottom']}
    assert top_netnames|bottom_netnames==expected
    assert top_netnames&bottom_netnames=={x['original_net_name'] for x in cross}
    assert sum(die_connections.values())==sum(len(v) for v in original_nets.values())
    assert term_count==len(prior_hbts),f'{term_count} matched / {len(prior_hbts)} terminal mismatch'
    assert not any(not x['contains_external_io'] for x in cross if not x['had_preassigned_terminal'])

    emit_def(out/'top.def',header,out_components['top'],[],out_nets['top'],'top')
    emit_def(out/'bottom.def',header,out_components['bottom'],pins_bottom,out_nets['bottom'],'bottom')
    shutil.copyfile(tech_lef,out/'tech.lef')
    shutil.copyfile(cell_lef,out/'cells.lef')
    # Deterministic identity mapping, useful in custom 3D CUGR input reader.
    (out/'instance_mapping.json').write_text(json.dumps({
        cid:{'name':name,'die':real_inst_die[name], 'macro':inst_types[name]}
        for cid,name in map_c_to_orig.items()},indent=2,ensure_ascii=False)+'\n')
    (out/'3d_nets.json').write_text(json.dumps({
        'description':'Cross-Die connectivity before HBT assignment, retaining original DEF net names',
        'io_die_assumption':'Original external DEF PINS are associated with Bottom Die',
        'hbt_is_preassigned':False,
        'nets':cross
    },indent=2,ensure_ascii=False)+'\n')
    pitch=spec['pitch_dbu'];diearea=spec['diearea_dbu']
    grid_config={
        'status':'proposed research grid; NOT verified legal HBT placement',
        'def_dbu_per_micron':spec['dbu_per_micron'],
        'diearea_dbu':diearea,
        'assumed_hbt_pitch_um':pitch/spec['dbu_per_micron'],
        'assumed_hbt_pitch_dbu':pitch,
        'grid_origin_dbu':spec['origin_dbu'],
        'grid_parameter_provenance':spec['provenance'],
        'assumed_gcell_pitch_um':2.1,
        'assumed_gcell_pitch_dbu':4200,
        'routing_layers_per_die':['metal1','metal2','metal3','metal4','metal5','metal6'],
        'hbt_interface_layer':'metal6',
        'caveats':['HBT candidates must still be checked against real obstacles, spacing, pins and full DRC',
                   'The LEF tech includes metal7-metal10; output DEF intentionally lists TRACKS only for metal1-metal6',
                   'Top-die placement coordinates are in a common projected XY frame, with no additional face-to-face mirroring']
    }
    model=spec['provenance']['placer_terminal_model']
    if model is not None:
        grid_config['pad_size_dbu']=model['terminal_size_dbu']
        grid_config['spacing_dbu']=model['terminal_spacing_dbu']
        grid_config['pad_geometry_source']='Actual placer TerminalSize model; not independent process DRC certification'
    (out/'hbt_config.json').write_text(json.dumps(grid_config,indent=2)+'\n')
    # A lightweight geometric legality audit (not detailed DRC).
    site_pitch_x_dbu=380  # as indicated by ROW STEP 380 in this source DEF
    site_pitch_y_dbu=2800 # as indicated by ROW STEP Y 2800
    placement_audit={}
    for tier in ('top','bottom'):
        pitems=[placements['C'+str(i+1)] for i in range(len(original_names))
                if real_inst_die[original_names[i]]==tier]
        placement_audit[tier]={
            'instances':len(pitems),
            'x_site_aligned':sum(x%site_pitch_x_dbu==0 for x,y,o in pitems),
            'y_row_aligned':sum(y%site_pitch_y_dbu==0 for x,y,o in pitems),
            'origin_within_diearea':sum(diearea[0]<=x<=diearea[2] and diearea[1]<=y<=diearea[3] for x,y,o in pitems)
        }
    summary={
      'site_alignment_audit':placement_audit,
      'input_def_declared_components':int(rows[sections['COMPONENTS'][0]].split()[1]),
      'source_actual_instances':len(old_comp),
      'source_original_nets':len(src['NETS']),
      'source_original_pins':len(src['PINS']),
      'top_instances':len(out_components['top']),
      'bottom_instances':len(out_components['bottom']),
      'top_net_records':len(out_nets['top']),
      'bottom_net_records':len(out_nets['bottom']),
      'external_pins_assigned_bottom':len(pins_bottom),
      'cross_die_nets_assuming_external_pins_bottom':len(cross),
      'cross_die_nets_with_preassigned_terminal':term_count,
      'cross_die_nets_without_preassigned_terminal':len(cross)-term_count,
      'cross_die_nets_involving_external_io':io_cross,
      'sum_local_endpoint_counts':sum(die_connections.values()),
      'complete_original_net_connectivity_preserved':True,
      'reference_hbt_instances_removed':len(prior_hbts),
      'lef_macro_pin_check':'pass',
      'reference_placement_coordinates_check':'pass',
      'physical_DRC_or_full_router_read_success':'not evaluated'
    }
    (out/'validation_report.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))
    # Fresh parse: regenerate and check actual section headers against each file.
    for die in ('top','bottom'):
        _,_,test=load_def(out/f'{die}.def')
        assert len(test['COMPONENTS'])==len(out_components[die])
        assert len(test['NETS'])==len(out_nets[die])
    return summary


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path('.'),help='Place-LoL root directory')
    p.add_argument('--out',type=Path,default=Path('./h3d_aes_input'),help='Output directory; defaults inside current folder')
    p.add_argument('--placer-input',type=Path,help='Actual placer input carrying TerminalSize, TerminalSpacing and DieSize')
    p.add_argument('--hbt-pitch-um',help='Explicit research-grid override; no implicit 3 um default')
    a=p.parse_args()
    run(a.root.resolve(),a.out.resolve(),a.placer_input.resolve() if a.placer_input else None,a.hbt_pitch_um)
