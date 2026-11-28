"""Summarize V3 ablations, repeated runs, budget experiments and verified conductor evidence."""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from statistics import median
from subprocess import CalledProcessError

from native_bench import CASES
from new3d_regression import evidence, write_tables
from new3d_experiments import G, X, ROOT, read, save, sha


def rows(path):
    if not path.is_file():
        return []
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def export(root, name, records):
    if records:
        write_tables(root, name, list(dict.fromkeys(k for r in records for k in r)), records)


def audited_run(folder, case, audit):
    job=folder/case
    if not (job/'process.json').exists():
        return dict(benchmark=case,status='NOT_RUN',path=str(job))
    proc=read(job/'process.json')
    stats_path=job/'output/routing_statistics.json'
    stats=read(stats_path) if stats_path.is_file() else {}
    phases=stats.get('stages',[])
    status='ROUTER_FEASIBLE' if proc.get('exit_code')==0 and stats.get('metrics') else 'FAIL'
    if stats.get('hbt_budget_excess',0)>0:
        status='BUDGET_UNMET'
    if status=='ROUTER_FEASIBLE':
        if (job/'tree-replay.json').is_file() and (job/'ledger-replay.json').is_file():
            ledger=read(job/'ledger-replay.json')
            tree=read(job/'tree-replay.json')
            if (ledger['resource_keys_after_release']==0 and ledger['hbt_violations']==0
                    and ledger['via_capacity_violations']==0 and ledger['unconnected_nets']==0
                    and max(ledger['max_wire_overflow_by_die'])<=1e-12
                    and tree['original_pins']==ledger['physical_pins']==proc['real_pins']
                    and ledger['nets']==proc['global_nets']
                    and tree['via_count']==sum(ledger['via_by_die'])
                    and tree['hbt_count']==ledger['hbt_count']
                    and math.isclose(tree['wirelength_um'],sum(ledger['wire_um_by_die']),rel_tol=1e-9)):
                status='PASS'
            else:
                status='FAIL'
        elif audit:
            verified=evidence(case,folder,replay=None if (job/'ledger-replay.json').exists()
                              else ROOT/'build-new3d/native_replay',save_tree=True)
            status=verified['status']
    cfg=read(job/'manifest.json')
    knobs={k:v for k,v in cfg.items() if k not in {'dies','mapping','bonded_dbu','bonding','provenance','weights'}}
    result=dict(benchmark=case,status=status,router_status=stats.get('status'),exit_code=proc['exit_code'],
                wirelength_um=proc['wirelength_um'],vias=proc['via_count'],hbt=proc['used_hbt'],
                objective=phases[-1]['best_feasible'] if phases else None,wall_seconds=proc['wall_seconds'],
                router_seconds=stats.get('routing_seconds'),rss_kb=proc['max_rss_kb'],
                stage3_accepted=next((p['accepted_nets'] for p in phases if p['stage']==3), None),
                stage2_accepted=next((p['accepted_nets'] for p in phases if p['stage']==2), None),
                threads=cfg['provenance'].get('requested_threads'),budget=cfg.get('hbt_budget'),
                budget_excess=stats.get('hbt_budget_excess'),path=str(job),
                options=json.dumps(knobs,sort_keys=True,separators=(',',':')))
    return result


def ablations(audit):
    ab=X/'ablation'
    dirs={'A0':X/'baseline/global','A1':X/'parallel_scaling/p1-t4-r1','A2':ab/'A2-p2-t4',
          'A3':ab/'A3-joint','A4':ab/'A4-topology','A5':ab/'A5-dp','A6':ab/'A6-hbt',
          'A7':ab/'A7-repair','A8':G/'final','A9':ab/'A9-no-maze/runs',
          'prim_reference':ab/'seed-ref','prim_blocked':ab/'seed-prim'}
    for label,name in [('A3-v2','A3-joint-v2'),('A4-v2','A4-topology-v2')]:
        if (ab/name).is_dir():
            dirs[label]=ab/name/'runs'
    allrows=[]
    ops=[]
    for label,folder in dirs.items():
        for case in CASES:
            # A8 is independently checked by the official verification driver.
            r=audited_run(folder,case,audit and label!='A8')
            allrows.append(dict(variant=label,**r))
            if label.startswith('A4'):
                path=folder/case/'output/routing_statistics.json'
                if path.exists():
                    for stage in read(path)['stages']:
                        for op,stat in stage.get('topology_refinement',{}).items():
                            ops.append(dict(variant=label,benchmark=case,operation=op,**stat))
    for i,module in enumerate(['topology_refinement','incremental_dp','progressive_hbt_search','failure_driven_repair'],10):
        for case in CASES:
            allrows.append(dict(variant=f'A{i}',benchmark=case,status='NOT_APPLICABLE',
                                reason=f'enable_{module} is already false in A8'))
    export(G,'ablation_summary',allrows)
    export(ab,'topology_operations',ops)
    from native_summary import detail_record
    detail=ROOT/'result-3/new3Ddetail'
    trials=[('A3-joint',ab/'A3-joint','aes'),('A4-topology',ab/'A4-topology','aes'),
            ('budget-90-mor1kx',X/'pareto/90pct/mor1kx/runs','mor1kx')]
    observations=[]
    for label,global_dir,case in trials:
        trial=detail/'_experiments/ablation'/(label+'-trial')/'runs'
        row=detail_record(trial,case,global_dir,{})
        observations.append(dict(variant=label,**row))
    export(detail,'ablation_summary',observations)


def nondominated(row,points):
    metrics=('wirelength_um','vias','hbt')
    return not any(all(p[k]<=row[k] for k in metrics) and any(p[k]<row[k] for k in metrics) for p in points)


def budgets(audit):
    folder=X/'pareto'
    if not folder.exists():
        return
    bounds=read(X/'baseline/budget_lower_bounds.json') if (X/'baseline/budget_lower_bounds.json').is_file() else {}
    results=[]
    for case in CASES:
        group=[]
        for percent in [90,100,110,120]:
            r=audited_run(folder/f'{percent}pct'/case/'runs',case,audit)
            lower=bounds.get(case,{}).get('two_sided_real_nets')
            bound_status=None
            if lower is not None and r.get('budget') is not None:
                bound_status='PROVEN_INFEASIBLE' if r['budget']<lower else 'BOUND_DOES_NOT_DECIDE'
            group.append(dict(percent=percent,physical_hbt_lower_bound=lower,bound_status=bound_status,**r))
        feasible=[r for r in group if r['status']=='PASS']
        for r in group:
            r['nondominated_among_verified']=nondominated(r,feasible) if r['status']=='PASS' else None
        results.extend(group)
    export(folder,'budget_summary',results)


def plot_budgets(folder, records):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_pdf import PdfPages
    output=folder/'hbt-budget-tradeoffs.pdf'
    temporary=output.with_suffix('.pdf.tmp')
    with PdfPages(temporary) as pdf:
        for case in CASES:
            all_points=[r for r in records if r['benchmark']==case]
            legal=[r for r in all_points if r['status']=='PASS']
            if not legal:
                continue
            frontier=[r for r in legal if r['nondominated_among_verified']]
            fig,axes=plt.subplots(1,2,figsize=(9,3.6),constrained_layout=True)
            for ax,field,ylabel in zip(axes,('wirelength_um','vias'),('Global wire length (µm)','Within-die vias')):
                ax.scatter([p['hbt'] for p in legal],[p[field] for p in legal],
                           color='#8994a7',label='Physically verified, within budget')
                ax.scatter([p['hbt'] for p in frontier],[p[field] for p in frontier],
                           marker='D',color='#00776b',label='Nondominated in WL, Via and HBT')
                curves=defaultdict(list)
                for p in legal:
                    curves[(p['hbt'],p[field])].append(str(p['percent'])+'%')
                for point,labels in curves.items():
                    ax.annotate(', '.join(labels),point,xytext=(4,5),textcoords='offset points',fontsize=8)
                ax.set(xlabel='Physical HBT sites used',ylabel=ylabel)
                ax.ticklabel_format(style='plain',useOffset=False)
                ax.margins(x=.15,y=.2)
                ax.grid(alpha=.2)
            axes[-1].legend(loc='best',fontsize=8,frameon=False)
            failed=[str(p['percent'])+'%' for p in all_points if p['status']!='PASS']
            fig.suptitle(case+('  |  Settings without a verified solution: '+', '.join(failed) if failed else ''))
            pdf.savefig(fig)
            plt.close(fig)
    temporary.replace(output)


def plot_parallel():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_pdf import PdfPages
    points=rows(G/'parallel_transaction_summary.tsv')
    readonly=rows(G/'parallel_summary.tsv')
    output=G/'maze_scaling.pdf'
    temporary=output.with_suffix('.pdf.tmp')
    with PdfPages(temporary) as pdf:
        for case in ('aes','ethmac','ariane','bp'):
            fig,axes=plt.subplots(1,3,figsize=(11,3.8),constrained_layout=True)
            samples=sorted((r for r in readonly if r['benchmark']==case),key=lambda r:int(r['threads']))
            axes[0].plot([int(r['threads']) for r in samples],[float(r['median_seconds']) for r in samples],
                         color='#00776b',marker='o')
            for ax,protocol in zip(axes[1:],('work','time')):
                for mode,color in [('P0','#667085'),('P1','#3265a8'),('P2','#00776b')]:
                    group=sorted((r for r in points if r['protocol']==protocol and r['benchmark']==case
                                  and r['mode']==mode and r['successes']=='3'),key=lambda r:int(r['threads']))
                    if group:
                        ax.plot([int(r['threads']) for r in group],[float(r['median_stage_seconds']) for r in group],
                                marker='o',color=color,label=mode)
                if ax.lines:
                    ax.legend(loc='best',frameon=False)
            for ax,title in zip(axes,('Read-only candidates','Stage 3, fixed workload','Stage 3, fixed time')):
                ax.set(xlabel='Requested threads',ylabel='Median elapsed time (s)',title=title)
                ax.set_xscale('log',base=2)
                ticks=[1,2,4,8,16,32]
                ax.set_xticks(ticks,labels=list(map(str,ticks)))
                ax.set_xlim(.9,36)
                ax.grid(axis='y',alpha=.25)
            fig.suptitle(f'{case}  |  Three repeats per configuration')
            pdf.savefig(fig)
            plt.close(fig)
    temporary.replace(output)


def budget_lower_bounds():
    """Each distinct net with real pins on both dies needs its own exclusive physical site."""
    result={}
    for case in CASES:
        job=X/'baseline/global'/case
        baseline=read(job/'ledger-replay.json')
        manifest=read(job/'manifest.json')
        if any(s['capacity']!=1 for s in manifest['bonding']['sites']):
            raise ValueError('Exclusive-site proof requires capacity one')
        masks=bytearray(baseline['nets'])
        total=0
        source=job/'output/components.jsonl'
        with source.open() as stream:
            for line in stream:
                c=json.loads(line)
                if c['pins']:
                    masks[c['net_id']]|=1<<c['die']
                    total+=len(c['pins'])
        if total!=baseline['physical_pins'] or 0 in masks:
            raise ValueError('Pin inventory mismatch in '+case)
        result[case]=dict(two_sided_real_nets=masks.count(3),physical_pins=total,
                          hbt_baseline=baseline['hbt_count'],components_sha256=sha(source))
    save(X/'baseline/budget_lower_bounds.json',result)


def repeats():
    results=[]
    pairs=defaultdict(dict)
    for mode,original in [('old',X/'baseline/global'),('new',G/'final')]:
        runs=[original]+[X/'repeats'/f'{mode}-r{n}'/'runs' for n in (2,3)]
        for case in CASES:
            times=[]
            for folder in runs:
                f=folder/case/'process.json'
                if f.is_file() and read(f)['exit_code']==0:
                    times.append(read(f)['wall_seconds'])
            row=dict(version=mode,benchmark=case,observations=len(times),expected=3,
                status='MEASURED' if len(times)==3 else 'INCOMPLETE',
                median_seconds=median(times) if len(times)==3 else None,
                min_seconds=min(times) if times else None,max_seconds=max(times) if times else None,
                range_seconds=max(times)-min(times) if len(times)>1 else None)
            results.append(row)
            pairs[case][mode]=row['median_seconds']
    export(G,'timing_repeats',results)
    paired=[dict(benchmark=c,baseline_median=p['old'],v3_median=p['new'],
                 speedup=p['old']/p['new'] if p['old'] and p['new'] else None) for c,p in pairs.items()]
    if all(r['speedup'] for r in paired):
        old=sum(r['baseline_median'] for r in paired)
        new=sum(r['v3_median'] for r in paired)
        paired.append(dict(benchmark='TOTAL',baseline_median=old,v3_median=new,speedup=old/new,
                           geometric_mean_speedup=math.exp(sum(math.log(r['speedup']) for r in paired)/len(paired))))
    export(G,'timing_repeats_comparison',paired)


def profiles():
    data=[]
    counters=[]
    high_fanout=[]
    for variant in ('baseline','v3','dp-reference','incremental-dp','progressive-hbt'):
        group=X/'profile'/variant
        for case in CASES:
            path=(group if variant=='baseline' else group/'runs')/case/'profile.json'
            if not path.is_file():
                continue
            report=read(path)
            total=report['modules']['total']['inclusive_seconds']
            for module,stat in report['modules'].items():
                workers=report.get('worker_modules',{}).get(module,{})
                data.append(dict(benchmark=case,variant=variant,module=module,calls=stat['calls'],
                    inclusive_seconds=stat['inclusive_seconds'],exclusive_seconds=stat['exclusive_seconds'],
                    exclusive_pct_of_main_thread_total=stat['exclusive_seconds']*100/total,
                    worker_calls=workers.get('calls'),worker_inclusive_elapsed=workers.get('inclusive_elapsed_seconds'),
                    worker_exclusive_elapsed=workers.get('exclusive_elapsed_seconds'),path=str(path)))
            counters.append(dict(benchmark=case,variant=variant,path=str(path),**report['counters']))
            slow=rows(path.with_name('slow_nets.tsv'))
            if slow:
                high=[r for r in slow if int(r['real_pins'])>=128]
                stage_seed=report['modules']['seed']['inclusive_seconds']
                slow_seed=sum(float(r['seed_seconds']) for r in high)
                high_fanout.append(dict(benchmark=case,variant=variant,slow_nets_recorded=len(slow),
                    high_fanout_in_slow_nets=len(high),high_fanout_seed_seconds=slow_seed,
                    total_seed_seconds=stage_seed,lower_bound_pct_of_seed=slow_seed*100/stage_seed if stage_seed else None))
    export(G,'profile_comparison',data)
    export(G,'profile_counters',counters)
    export(G,'high_fanout_diagnostics',high_fanout)


def comparisons_with_h3d():
    from timeopt_report import PAPER
    detail=ROOT/'result-3/new3Ddetail'
    verified={r['benchmark']:r for r in rows(detail/'summary.tsv')}
    local={r['benchmark']:r for r in rows(X/'h3d_reference/runs/summary.tsv')}
    baselines={r['benchmark']:r for r in rows(detail/'_experiments/baseline/summary.tsv')}
    baseline_global={r['benchmark']:r for r in rows(X/'baseline/global/summary.tsv')}
    formal={r['benchmark']:r for r in rows(G/'summary.tsv')}
    gr=[]
    dr=[]
    for case in CASES:
        paper=PAPER[case]
        dr.append(dict(benchmark=case,variant='H3D paper 3 um',status='PUBLISHED_REFERENCE',
            detailed_wl_um=paper[0],detailed_vias=paper[1],hbt=paper[2],global_seconds=paper[3],
            time_scope='paper platform and timing boundary',source='scripts/timeopt_report.py:PAPER'))
        for name,tab,gtab in [('TimeOpt',baselines,baseline_global),('V3',verified,formal)]:
            d=tab.get(case,{})
            g=gtab.get(case,{})
            old=name=='TimeOpt'
            old_report=detail/'_experiments/baseline'/case/'report.json'
            detail_wall=read(old_report)['detail_wall_seconds'] if old and old_report.is_file() else d.get('detail_wall_seconds')
            dr.append(dict(benchmark=case,variant=name,status=d.get('status','NOT_RUN'),
                detailed_wl_um=d.get('total_wirelength_um' if old else 'total_wire_length_um'),
                detailed_vias=d.get('total_via_count'),hbt=d.get('hbt_count'),global_seconds=g.get('wall_seconds'),
                time_scope='local full global process',detail_wall_seconds=detail_wall,
                total_drc=d.get('total_drc'),joint_connectivity=d.get('joint_connectivity'),
                global_peak_rss_kb=g.get('max_rss_kb'),source=d.get('output_directory')))
            gr.append(dict(benchmark=case,variant=name,status=g.get('status','NOT_RUN'),wirelength_um=g.get('wirelength_um'),
                via_count=g.get('via_count'),hbt=g.get('used_hbt'),legal_sites=g.get('legal_hbt_sites'),
                global_wall_seconds=g.get('wall_seconds'),max_rss_kb=g.get('max_rss_kb'),source=g.get('output_directory')))
        h=local.get(case,{})
        gr.append(dict(benchmark=case,variant='H3D local',status=('MEASURED' if h.get('exit_code')=='0'
            else 'FAIL' if h else 'NOT_RUN'),h3d_wire_metric=h.get('wirelength_h3d_metric'),
            via_count=h.get('via_count'),hbt=h.get('used_hbt'),legal_sites=h.get('available_hbt'),
            global_wall_seconds=h.get('wall_seconds'),max_rss_kb=h.get('max_rss_kb'),source=str(X/'h3d_reference/runs'/case)))
    export(detail,'comparison_h3d',dr)
    export(G,'comparison_h3d',gr)


def changed_hbt_counts():
    """Trace any design-level HBT increase to actual physical sites and route edges."""
    result=[]
    for case in CASES:
        pair=[folder/case for folder in (X/'baseline/global',G/'final')]
        if not all((job/'process.json').is_file() for job in pair):
            continue
        p0,p1=[read(job/'process.json') for job in pair]
        if p0['used_hbt'] is None or p1['used_hbt'] is None or p1['used_hbt']<=p0['used_hbt']:
            continue
        old,new=[defaultdict(set),defaultdict(set)]
        for job,sites in zip(pair,(old,new)):
            for row in rows(job/'output/hbt_assignment.tsv'):
                sites[int(row['net_id'])].add(int(row['site_id']))
        changed=[net for net in sorted(set(old)|set(new)) if len(old[net])!=len(new[net])]
        audit={net:dict(benchmark=case,net_id=net,baseline_sites=','.join(map(str,sorted(old[net]))),
                        v3_sites=','.join(map(str,sorted(new[net]))),delta_hbt=len(new[net])-len(old[net]))
               for net in changed}
        for job,label in zip(pair,('baseline','v3')):
            with (job/'output/components.jsonl').open() as stream:
                for line in stream:
                    entry=json.loads(line)
                    if entry['net_id'] in audit:
                        audit[entry['net_id']][f'die{entry["die"]}_net']=entry['local_net']
            total=defaultdict(lambda:[0.,0])
            with (job/'output/route_edges.tsv').open() as stream:
                for edge in csv.DictReader(stream,delimiter='\t'):
                    net=int(edge['net_id'])
                    if net in audit:
                        total[net][0]+=float(edge['wire_um'])
                        total[net][1]+=edge['type']=='VIA'
            for net in changed:
                audit[net][label+'_wire_um']=total[net][0]
                audit[net][label+'_vias']=total[net][1]
        result.extend(audit.values())
    export(G,'hbt_count_changes',result)


def trials():
    result=[]
    for protocol,path in [('work',X/'fixed_workload'),('time',X/'fixed_time')]:
        data=rows(path/'transaction-r1/trials.tsv')
        groups=defaultdict(list)
        for r in data:
            groups[(r['benchmark'],r['mode'],int(r['threads']))].append(r)
        for (case,mode,n),sample in groups.items():
            ok=[r for r in sample if int(r['exit_code'])==0]
            ref=groups.get((case,'P0',1),[])
            row=dict(protocol=protocol,benchmark=case,mode=mode,threads=n,successes=len(ok),expected=3,
                     failed=len(sample)-len(ok))
            for field in ('stage_seconds','maze_search_wall_seconds','maze_worker_elapsed_seconds',
                          'maze_worker_cpu_seconds','actual_workers','maze_batches','scheduler_seconds',
                          'queue_wait_sum_seconds','commit_seconds','astar_expanded_nodes','tasks','accepted',
                          'actual_commit_checks','resource_conflict_rejections','resource_conflict_rate',
                          'retry_count','serial_fallback_count','maze_search_timeouts','maze_refinement_timeouts',
                          'candidate_acceptance_per_task','pending_at_deadline','stage_overrun_seconds',
                          'wire_um','vias','hbt','objective','wall_seconds','peak_rss_kb'):
                measured=[float(r[field]) for r in ok if r.get(field) not in ('','N/A',None)]
                row['median_'+field]=median(measured) if len(measured)==3 else None
            if len(ok)==3:
                times=[float(r['stage_seconds']) for r in ok]
                row.update(min_stage_seconds=min(times),max_stage_seconds=max(times))
                if len(ref)==3 and all(int(r['exit_code'])==0 for r in ref):
                    row['stage_speedup_vs_P0']=median(float(r['stage_seconds']) for r in ref)/median(times)
                    row['stage_efficiency']=row['stage_speedup_vs_P0']/n
                    row['process_speedup_vs_P0']=median(float(r['wall_seconds']) for r in ref)/row['median_wall_seconds']
                p1=groups.get((case,'P1',n),[])
                if mode=='P2' and len(p1)==3 and all(int(r['exit_code'])==0 for r in p1):
                    row['stage_speedup_vs_P1']=median(float(r['stage_seconds']) for r in p1)/median(times)
                    row['objective_delta_vs_P1']=row['median_objective']-median(float(r['objective']) for r in p1)
            result.append(row)
    export(G,'parallel_transaction_summary',result)


def audit_trials():
    from native_check import check
    from native_summary import replay_edges
    data=[]
    for protocol in ('fixed_workload','fixed_time'):
        table=rows(X/protocol/'transaction-r1/trials.tsv')
        # Predetermined sample for independent validation of all three algorithms.
        for r in table:
            if r['repeat']!='0' or (r['mode'],r['threads']) not in [('P0','1'),('P1','4'),('P2','4')]:
                continue
            path=Path(r['path'])
            row=dict(protocol=protocol,benchmark=r['benchmark'],mode=r['mode'],threads=int(r['threads']),
                     repeat=0,status='FAIL',path=str(path))
            try:
                if r['exit_code']!='0':
                    raise ValueError('Router returned '+r['exit_code'])
                tree=check(path/'manifest.json',path/'output')
                replay_edges(path,ROOT/'build-new3d/native_replay')
                ledger=read(path/'ledger-replay.json')
                if (ledger['resource_keys_after_release']!=0 or ledger['unconnected_nets']!=0
                        or ledger['hbt_violations']!=0 or ledger['via_capacity_violations']!=0
                        or max(ledger['max_wire_overflow_by_die'])>1e-12
                        or tree['original_pins']!=ledger['physical_pins']
                        or tree['hbt_count']!=ledger['hbt_count'] or tree['via_count']!=sum(ledger['via_by_die'])
                        or not math.isclose(tree['wirelength_um'],sum(ledger['wire_um_by_die']),rel_tol=1e-9)):
                    raise ValueError('Invalid or inconsistent independent routing checks')
                save(path/'tree-replay.json',tree)
                row.update(status='PASS',resources_after_release=0,physical_pins=ledger['physical_pins'])
            except (ValueError, KeyError, OSError, RuntimeError, CalledProcessError) as error:
                row['reason']=str(error)
            data.append(row)
    export(G,'checkpoint_verification',data)
    return len(data)==24 and all(r['status']=='PASS' for r in data)


def inventory():
    detail=ROOT/'result-3/new3Ddetail'
    scenarios={'fixed_workload':156,'fixed_time':156}
    state={
        'global':read(G/'experiment_status.json') if (G/'experiment_status.json').is_file() else {},
        'detail':read(detail/'verification_status.json') if (detail/'verification_status.json').is_file() else {},
        'checkpoint_trials':{},
    }
    for name,expected in scenarios.items():
        rows_path=X/name/'transaction-r1/trials.tsv'
        table=rows(rows_path)
        state['checkpoint_trials'][name]=dict(completed=len(table),expected=expected,
            router_exit_zero=sum(r['exit_code']=='0' for r in table),source=str(rows_path))
    state['budget']={k:sum(r['status']==k for r in rows(X/'pareto/budget_summary.tsv'))
                      for k in ('PASS','ROUTER_FEASIBLE','BUDGET_UNMET','FAIL','NOT_RUN')}
    state['sensitivity']={'completed_runs':len(list((X/'parameter_sweep').glob('*/runs/*/process.json'))),
                          'expected':42}
    state['profile']={name:len(list((X/'profile'/name/'runs').glob('*/profile.json')))
                      for name in ('v3','dp-reference','incremental-dp','progressive-hbt')}
    state['detail_ablation']={k:sum(r['status']==k for r in rows(detail/'ablation_summary.tsv'))
                              for k in ('PASSED','FAILED','NOT_COMPLETED')}
    save(G/'evidence_inventory.json',state)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit',action='store_true',help='Run physical tree validation and missing C++ ledger replays')
    parser.add_argument('--budget-bounds',action='store_true',help='Prove per-design necessary HBT lower bounds from real pins')
    parser.add_argument('--audit-budgets',action='store_true',help='Only replay and validate the HBT budget runs')
    parser.add_argument('--audit-trials',action='store_true',help='Independently check the first P0 and four-thread P1/P2 runs')
    parser.add_argument('--changed-hbt-counts',action='store_true',help='Trace design-level HBT increases by physical net')
    parser.add_argument('--plot-budgets',action='store_true',help='Only render the verified budget table with Matplotlib')
    parser.add_argument('--plot-parallel',action='store_true',help='Only render the maze scaling tables with Matplotlib')
    args=parser.parse_args()
    if args.plot_parallel:
        plot_parallel()
        return
    if args.plot_budgets:
        folder=X/'pareto'
        table=rows(folder/'budget_summary.tsv')
        verified=[dict(r,hbt=int(r['hbt']),wirelength_um=float(r['wirelength_um']),vias=int(r['vias']),
                       nondominated_among_verified=r['nondominated_among_verified']=='True')
                  for r in table if r['status']=='PASS']
        if not verified:
            parser.error('No independently verified budget runs')
        plot_budgets(folder,verified+[r for r in table if r['status']!='PASS'])
        return
    if args.audit_budgets:
        budgets(True)
        inventory()
        return
    if args.audit_trials:
        raise SystemExit(0 if audit_trials() else 1)
    if args.budget_bounds:
        budget_lower_bounds()
    if args.changed_hbt_counts:
        changed_hbt_counts()
    ablations(args.audit)
    budgets(args.audit)
    repeats()
    trials()
    profiles()
    comparisons_with_h3d()
    dest=X/'parameter_sweep'
    if dest.is_dir():
        data=[]
        for options in sorted(dest.glob('*/options.json')):
            for case in ['aes','ethmac']:
                data.append(dict(setting=options.parent.name,**audited_run(options.parent/'runs',case,args.audit)))
        export(dest,'sensitivity',data)
    inventory()


if __name__=='__main__':
    main()
