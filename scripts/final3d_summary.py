"""Build paper-stage tables from independently verified final3D run artifacts."""
import argparse
from collections import defaultdict
import math
from statistics import median
import subprocess

from final3d_exchange_check import check_exchange
from final3d_experiments import ABLATION, BASE, CASES, CHECKPOINT_STUDY, DETAIL, GLOBAL, REPLAY, REVISION, TRADEOFF
from final3d_experiments import digest, read, verify_inputs, write
from native_check import check
from native_summary import global_record
from new3d_regression import DATA_ERRORS, GLOBAL_FIELDS, evidence, write_tables

EXCHANGE = """archive_candidates retained_candidates duplicate_candidates safe_pruned_candidates archive_evicted_candidates
archive_invalid_alternatives static_rejected_candidates blocked_resources blocker_records_dropped
exchange_groups exchange_attempted exchange_accepted exchange_multinet_accepted exchange_strict_pair_accepted
exchange_completed exchange_timeout exchange_from_blocking exchange_from_budget exchange_group_regeneration
exchange_regenerated exchange_combinations exchange_search_nodes exchange_safe_pruned_nodes archive_payload_peak_bytes
archive_payload_limit_bytes exchange_identical_completed exchange_tasks_dropped exchange_tasks_pending
exchange_delta_wirelength exchange_delta_via exchange_delta_hbt exchange_delta_objective exchange_runtime_seconds
stage_overrun_seconds stop_reason""".split()
GLOBAL_COLUMNS = [*GLOBAL_FIELDS[:-3], "stage1_seconds", "stage2_seconds", "stage3_seconds",
                  "topology_attempted", "topology_accepted", "base_objective_connected_routes", *EXCHANGE,
                  *GLOBAL_FIELDS[-3:]]
DETAIL_COLUMNS = """benchmark status top_wirelength_um bottom_wirelength_um total_wirelength_um top_vias bottom_vias
total_vias used_hbt top_drc bottom_drc unconnected_nets top_runtime_seconds bottom_runtime_seconds
sum_die_runtime_seconds actual_detail_wall_seconds global_router_seconds top_geometry bottom_geometry
geometry_check joint_connectivity_check output_directory reason""".split()
DETAIL_RENAME = dict(top_wirelength_um="top_wire_length_um",bottom_wirelength_um="bottom_wire_length_um",
                     total_wirelength_um="total_wire_length_um",top_vias="top_via_count",bottom_vias="bottom_via_count",
                     total_vias="total_via_count",used_hbt="hbt_count",top_runtime_seconds="top_wall_seconds",
                     bottom_runtime_seconds="bottom_wall_seconds",sum_die_runtime_seconds="sum_wall_seconds",
                     actual_detail_wall_seconds="detail_wall_seconds",joint_connectivity_check="joint_connectivity")
COMPARE = ["Benchmark","原WL","新WL","WL变化","原#Vias","新#Vias","Via变化","原#HBTs","新#HBTs","HBT变化",
           "原RT","新RT","RT变化"]


def latest_attempt(directory):
    if not directory.with_name(directory.name+".run.json").exists():
        return directory
    # The designated first formal attempt remains the quality and DRT source.
    if directory == GLOBAL/"final":
        return directory
    candidate = directory
    for sibling in sorted(directory.parent.glob(directory.name+".attempt-?*"),
                          key=lambda item: int(item.name.split(".attempt-")[-1])):
        marker = sibling.with_name(sibling.name+".run.json")
        if marker.is_file() and read(marker).get("state")=="COMPLETE":
            candidate = sibling
    return candidate


def replay(job, binary, budget):
    job = job.resolve()
    config, output = job/"manifest.json", job/"output"
    stamp = job/"verification_sha256.json"
    files = [binary,config,*[output/name for name in ("route_edges.tsv","hbt_assignment.tsv",
             "components.jsonl","top.guide","bottom.guide","routing_statistics.json")]]
    hashes = {str(file):digest(file) for file in files}
    ledger = job/"ledger-replay.json"
    tree = job/"tree-replay.json"
    valid = ledger.is_file() and tree.is_file()
    if valid:
        hashes.update({str(file):digest(file) for file in (ledger,tree)})
    if not valid or not stamp.is_file() or read(stamp)!=hashes:
        temporary = ledger.with_suffix(".partial.json")
        if temporary.exists():
            temporary.rename(temporary.with_name(temporary.stem+f".{temporary.stat().st_mtime_ns}.json"))
        with (job/"ledger-replay.log").open("w") as log:
            subprocess.run([binary,config,output/"route_edges.tsv",temporary],cwd=job,
                           stdout=log,stderr=subprocess.STDOUT,check=True)
        if ledger.is_file() and read(ledger)!=read(temporary):
            raise ValueError("Routed artifact and existing ledger evidence disagree")
        temporary.replace(ledger)
        write(tree,check(config,output,require_budget=not budget))
        hashes.update({str(file):digest(file) for file in (ledger,tree)})
        write(stamp,hashes)
    return read(ledger)


def collect_global(directory, case, verify=False, budget=False):
    directory = latest_attempt(directory)
    job = directory/case
    result = dict(benchmark=case,status="BLOCKED",output_directory=str(job/"output"),reason="Missing process result")
    if not (job/"process.json").is_file():
        return result
    try:
        stats = read(job/"output/routing_statistics.json")
        verified = None
        if stats.get("metrics") and (verify or (job/"ledger-replay.json").is_file()):
            verified = replay(job,REPLAY,budget) if verify else read(job/"ledger-replay.json")
        if verified:
            result.update(global_record(directory,case))
        else:
            process = read(job/"process.json")
            metrics = stats.get("metrics") or {}
            result.update(stats["inventory"])
            del result["dies"]
            result.update({k:metrics.get(k) for k in ("wirelength_um","via_count","used_hbt","wire_overflow")})
            result.update(wall_seconds=process["wall_seconds"],router_reported_seconds=stats["routing_seconds"],
                          max_rss_kb=int(process["max_rss_kb"]),exit_code=process["exit_code"],
                          legal_hbt_sites=process["legal_sites"])
        result["status"] = stats["status"]
        for stage in stats["stages"]:
            no = stage["stage"]
            result[f"stage{no}_seconds"] = stage.get("seconds")
            if no==2:
                result.update({k:v for k,v in (stage.get("topology_exchange") or {}).items() if k in EXCHANGE})
                operations = stage.get("topology_refinement",{}).values()
                result["topology_attempted"] = sum(op["attempted"] for op in operations)
                result["topology_accepted"] = sum(op["accepted"]+op.get("joint_accepted",0) for op in operations)
                result["stage2_optimizer_workers"] = (stage.get("local_optimizer") or {}).get("workers")
            if no==3:
                result["stage3_maze_workers"] = (stage.get("parallel_maze") or {}).get("actual_workers",1)
        last = stats["stages"][-1] if stats["stages"] else {}
        result["best_feasible_objective"] = last.get("best_feasible")
        result["base_objective_connected_routes"] = last.get("base_objective_connected_routes")
        code = result.get("exit_code")
        budget_unmet = stats.get("hbt_budget_excess",0)>0
        if verified:
            physical = (verified["resource_keys_after_release"]==verified["unconnected_nets"]==
                        verified["via_capacity_violations"]==verified["hbt_violations"]==0 and
                        max(verified["max_wire_overflow_by_die"])<=1e-12)
            tree_pass = (job/"tree-replay.json").is_file()
            result["independent_check"] = "passed" if physical and tree_pass else "failed" if not physical else None
            if code!=0 and not (budget and code==2 and budget_unmet):
                result.update(status="FAIL",reason=f"Routing process exited with code {code}")
            elif physical and tree_pass:
                result["status"] = "BUDGET_UNMET" if budget_unmet else "PASS"
                result["reason"] = ("Heuristic search left positive budget excess" if result["status"]=="BUDGET_UNMET"
                                    else None)
            else:
                result.update(status="FAIL" if not physical else "INCONCLUSIVE",reason="Missing or failed independent check")
        elif stats.get("metrics"):
            result.update(status="UNVERIFIED",reason="Independent verification pending")
        else:
            result.update(status="FAIL",reason=stats["status"])
        if budget:
            result.update({k:stats.get(k) for k in ("hbt_budget","hbt_budget_used","hbt_budget_excess")})
            result["budget_feasible"] = result["status"]=="PASS" if stats.get("hbt_budget") is not None else None
    except DATA_ERRORS as error:
        result.update(status="FAIL" if not isinstance(error,FileNotFoundError) or verify else "INCONCLUSIVE",
                      reason=str(error))
    return result


def collect_detail(global_dir, detail_dir, case, verify=False):
    row = evidence(case,global_dir,detail_dir)
    row.update({k:row.get(v) for k,v in DETAIL_RENAME.items()})
    f = detail_dir/case/"joint_geometry.json"
    try:
        if f.is_file():
            row["unconnected_nets"] = read(f).get("disconnected_nets")
        if verify and row["status"]=="PASS":
            run = detail_dir/case
            source = global_dir/case/"output"
            guide = read(run/"guide_provenance.json")
            actual = {name:digest(source/name) for name in
                      ("top.guide","bottom.guide","components.jsonl","hbt_assignment.tsv")}
            if actual!=guide["sha256"]:
                raise ValueError("Detailed routing guide lineage changed")
            hashes = {}
            for side in ("top","bottom"):
                for state,def_file in (("input","placed.def"),("routed","routed.def")):
                    tag = f"geometry_{state}"
                    data = run/side/(tag+".tsv.gz")
                    layout = run/side/def_file
                    provenance = read(run/side/(tag+"_provenance.json"))
                    hashes[str(data)] = digest(data)
                    hashes[str(layout)] = digest(layout)
                    if (hashes[str(data)]!=provenance["gzip_sha256"] or hashes[str(layout)]!=provenance["def_sha256"]
                            or provenance["openroad_sha256"]!=guide["openroad_binary_sha256"]):
                        raise ValueError("Detailed routing conductor provenance changed")
                for name in ("detail.json","drc.rpt","geometry_audit.json","terminal_components.jsonl.gz"):
                    path = run/side/name
                    hashes[str(path)] = digest(path)
            for name in ("report.json","verified_report.json","joint_geometry.json","guide_provenance.json","plan.json"):
                hashes[str(run/name)] = digest(run/name)
            write(run/"verified_output_sha256.json",hashes)
    except DATA_ERRORS as error:
        row.update(status="FAIL",reason=str(error))
    sides = [row.get(side+"_geometry") for side in ("top","bottom")]
    row["geometry_check"] = "passed" if sides==["passed","passed"] else "failed" if "failed" in sides else None
    return row


def detail_report(old, new):
    columns=["benchmark","version","global_resource_replay","status","top_completed","bottom_completed",
             "top_drc","bottom_drc","top_geometry","bottom_geometry","joint_connectivity_check",
             "unconnected_nets","mixed_nets","evidence"]
    result=[]
    for label,grt,drt,items in (("V3",BASE/"global",DETAIL/"baseline",old),
                                ("Final",GLOBAL/"final",DETAIL/"final",new)):
        for row in items:
            case=row["benchmark"]
            run=drt/case
            joint=run/"joint_geometry.json"
            entry=dict(version=label,**row)
            entry["global_resource_replay"]="passed" if (grt/case/"verification_sha256.json").is_file() else None
            for side in ("top","bottom"):
                entry[side+"_completed"]=(run/side/"routed.def").is_file() and (run/side/"detail.json").is_file()
            try:
                entry["mixed_nets"]=read(joint).get("mixed_nets") if joint.is_file() else None
            except DATA_ERRORS:
                entry["mixed_nets"]=None
            entry["evidence"]=f"[{case}]({run.relative_to(DETAIL)}/)" if run.is_dir() else None
            result.append(entry)
    write_tables(DETAIL/"verification","evidence",columns,result)
    table=(DETAIL/"verification/evidence.md").read_text()
    (DETAIL/"verification_report.md").write_text(
        "# 双裸片详细布线验证\n\n"
        "全局资源验证依赖完整树与资源回放。两侧几何检查使用 OpenROAD/ODB 导体导出，"
        "联合检查核对实际通孔、键合位置、端点归属及跨裸片连通性。详细规则覆盖范围为"
        "已有 LEF/DEF、TritonRoute 检查和原 F2F 导体审计。制造工艺完整签核超出此实验协议。"
        "N/A 表示相应证据尚不可用。\n\n"+table)


def write_report(old_global,new_global,old_detail,new_detail):
    selection_file=GLOBAL/"config/selection.json"
    selection=read(selection_file) if selection_file.is_file() else {}
    verified=lambda rows: sum(r["status"]=="PASS" for r in rows)
    lines=[
        "# Native CUGR-3D 最终版本开发与实验记录",
        "",
        "冻结对照为 3d-new-f2f 的 0172372。实验仅使用 benchmark-array.json 的原始输入及 HBT 规则。",
        "初解沿用 FLUTE、三维模式布线和跨裸片 Prim 估价。新增协同位于 Stage 2。",
        "",
        "## C++ 修改",
        "",
        "- CandidateArchive 保存合法完整树及其固定摘要，并按边等价、需求与物理 HBT 集合筛选。",
        "- TopologyExchange 和 TopologyRefinement 交替组织单网候选与阻塞交换，使用组资源视图进行再生成。",
        "- RoutingGraph 追溯请求资源及其实际拥有者，以同一组累计需求规则检查和发布。",
        "- ConflictOptimizer 复用原有限候选深度搜索，按固定代价与真实全局 HBT 预算下界剪枝。",
        "- IncrementalRoute 发布组内事务，MazeExperiment 保存及恢复相同 Stage 1 状态。",
        "- PatternSearch 接通只读资源视图，Profile 记录档案、阻塞、联合组、再生成和提交耗时。",
        "",
        "## 实验状态",
        "",
        f"- 第一次正式新全局结果通过两种独立回放 {verified(new_global)}/{len(CASES)} 项。[全局总结](summary.tsv)",
        f"- 第一次正式新详细结果通过两侧及联合几何检查 {verified(new_detail)}/{len(CASES)} 项。"
        "[详细总结](../final3Ddetail/summary.tsv)",
        f"- 正式配置选择 {selection.get('selected','待代表性证据复核')}。{selection.get('reason','')}",
        "- 程序内部计时与进程墙钟分别核对。[全局对照](comparison_v3.tsv) · [进程墙钟](comparison_v3_wall.tsv)",
        "- 三轮独立运行的中位数、最小值和最大值单列。[重复计时](repeat_totals.tsv)",
        "- 详细布线记录两侧各自耗时、两侧之和及流程实际墙钟。[详细对照](../final3Ddetail/comparison_v3.tsv)",
        "- 档案、静态筛选、动态阻塞与组视图生成的消融记录保留原始候选计数。"
        "[消融](ablation_summary.tsv) · [相同初解](exchange/same_stage2_summary.tsv)",
        "- 预算与真实阻塞案例保留原始树、需求摘要及独立核对。"
        "[预算对照](hbt_tradeoff/summary.tsv) · [组交换证据](exchange/real_cases.tsv)",
        "- 联合优化配置的七例结果分别入表。[A6 联合配置](exchange/joint_summary.tsv)",
        "",
        "有限候选搜索、时限和档案容量约束了实际可见优化机会。研究结论需结合真实双网证据以及同预算、"
        "端到端与详细布线结果。未有独立证据的指标保留 N/A，预算超额结果保留实际状态。",
        "",
        "[算法、模型与复杂度](../../docs/native-3d/final3d/algorithm_design.md) · "
        "[源代码审计](../../docs/native-3d/final3d/code_audit.md) · "
        "[运行协议](../../docs/native-3d/final3d/experiment_protocol.md) · "
        "[正确性](../../docs/native-3d/final3d/correctness.md)",
    ]
    for title,old,new,metrics in (
            ("全局布线",old_global,new_global,("wirelength_um","via_count","used_hbt","wall_seconds")),
            ("详细布线",old_detail,new_detail,("total_wirelength_um","total_vias","used_hbt","actual_detail_wall_seconds"))):
        paired=[(a,b) for a,b in zip(old,new) if a["status"]==b["status"]=="PASS" and
                all(r.get(m) is not None for r in (a,b) for m in metrics)]
        if not paired:
            continue
        lines += ["",f"## {title}的可比合计", "", f"共 {len(paired)}/{len(CASES)} 个设计，"
                  +"、".join(a["benchmark"] for a,_ in paired)+"。", "",
                  "| 指标 | V3 | R2 | 变化率 |", "| --- | ---: | ---: | ---: |"]
        for i,(metric,label) in enumerate(zip(metrics,("线长 µm","普通 Via","HBT","实际墙钟 s"))):
            x,y=(sum(pair[j][metric] for pair in paired) for j in (0,1))
            fmt=",d" if i in (1,2) else ",.3f"
            change=f"{(y-x)/x:.4%}" if x else "N/A"
            lines.append(f"| {label} | {x:{fmt}} | {y:{fmt}} | {change} |")
    (GLOBAL/"development_report.md").write_text("\n".join(lines)+"\n")


def compare(directory,name,old,new,metrics):
    rows,successful,speeds = [],[],[]
    for reference,current in zip(old,new):
        row = {"Benchmark":current["benchmark"]}
        good = reference["status"]==current["status"]=="PASS" and all(
            t.get(m) is not None for m in metrics for t in (reference,current))
        for index,metric in enumerate(metrics):
            original,value = reference.get(metric),current.get(metric)
            a,b,c = COMPARE[1+3*index:4+3*index]
            row.update({a:original,b:value,c:round((value-original)/original*100,6) if good and original else None})
        if good:
            successful.append((reference,current))
            if reference[metrics[-1]]>0 and current[metrics[-1]]>0:
                speeds.append(reference[metrics[-1]]/current[metrics[-1]])
        rows.append(row)
    totals = {"Benchmark":f"合计 ({len(successful)}/{len(CASES)})"}
    for index,metric in enumerate(metrics):
        a,b,c = COMPARE[1+3*index:4+3*index]
        if successful:
            x,y = (sum(pair[i][metric] for pair in successful) for i in (0,1))
            totals.update({a:x,b:y,c:round((y-x)/x*100,6) if x else None})
    write_tables(directory,name,COMPARE,[*rows,totals])
    return dict(comparable_cases=[v["benchmark"] for _,v in successful],
                passed=sum(n["status"]=="PASS" for n in new),failed=sum(n["status"]=="FAIL" for n in new),
                missing=sum(n["status"]=="BLOCKED" for n in new),
                unverified=sum(n["status"] in ("INCONCLUSIVE","UNVERIFIED") for n in new),
                geomean_speedup=math.exp(sum(map(math.log,speeds))/len(speeds)) if speeds else None)


def supplements(verify):
    ablation = []
    for label in ["A0","A1","A2","A3","A4","A5","A6","A7","A6-rotation","A6-no-dominance"]:
        for case in ("aes","ethmac","ariane","bp"):
            directory = BASE/"global" if label=="A0" else (
                        GLOBAL/"ablation"/label if label in ("A1","A2") else ABLATION/label)
            if label=="A7":
                directory=GLOBAL/"final"
            ablation.append(dict(configuration=label,**collect_global(directory,case,verify)))
    write_tables(GLOBAL,"ablation_summary",["configuration",*GLOBAL_COLUMNS],ablation)
    if REVISION:
        joint=[collect_global(ABLATION/"A6" if case in ("aes","ethmac","ariane","bp")
                              else GLOBAL/"exchange/joint-r2-additional",case,verify) for case in CASES]
        write_tables(GLOBAL/"exchange","joint_summary",GLOBAL_COLUMNS,joint)
        detailed=[]
        for case in ("aes","ethmac"):
            detailed.append(dict(configuration="A0",**collect_detail(BASE/"global",DETAIL/"baseline",case,verify)))
            detailed.append(dict(configuration="A6",**collect_detail(ABLATION/"A6",DETAIL/"ablation/A6-r2",case,verify)))
        write_tables(DETAIL/"ablation","joint_detail_summary",["configuration",*DETAIL_COLUMNS],detailed)
    snapshots = []
    exchange = []
    for root in [ABLATION,CHECKPOINT_STUDY,GLOBAL/"final"]:
        for path in sorted(root.glob("**/topology_exchanges.json")):
            if not REVISION and "r2" in path.relative_to(root).parts:
                continue
            config = read(path.parents[1]/"manifest.json")
            checked = []
            for index,case in enumerate(read(path)):
                before,after = case["before"],case["after"]
                try:
                    audited = check_exchange(config,case)
                    audit_status = "PASS"
                except DATA_ERRORS as error:
                    audited = dict(passed=False,reason=str(error))
                    audit_status = "FAIL"
                checked.append(audited)
                exchange.append(dict(benchmark=path.parents[1].name,case=index+1,strict_pair=case["strict_pair"],
                    nets=",".join(map(str,case["nets"])),actual_blockers=len(case["blocks"]),
                    budget_pair=case["budget_pair"],search_complete=case["search_complete"],
                    evidence=str(path),objective_change=sum(r["cost"] for r in after)-sum(r["cost"] for r in before),
                    independent_check=audit_status))
            write(path.parent/"topology_exchange_audit.json",checked)
    write_tables(GLOBAL/"exchange","real_cases",["benchmark","case","strict_pair","nets","actual_blockers",
                 "budget_pair","search_complete","objective_change","independent_check","evidence"],exchange)
    snapshot_files=list(CHECKPOINT_STUDY.glob("*/*/common_checkpoint.json"))
    if REVISION:
        for label in ("A0","A1","A2"):
            snapshot_files.extend((GLOBAL/"exchange/stage2"/label).glob("*/common_checkpoint.json"))
    for p in sorted(snapshot_files):
        checkpoint=read(p)
        case=checkpoint.get("benchmark",p.parent.name.split(".attempt-")[0])
        snapshots.append(dict(configuration=p.parents[1].name,checkpoint_sha256=checkpoint["sha256"],
                              **collect_global(p.parent,case,verify)))
    write_tables(GLOBAL/"exchange","same_stage2_summary",["configuration","checkpoint_sha256",*GLOBAL_COLUMNS],snapshots)
    budget_rows = []
    budget_paths={}
    for folder in ([GLOBAL/"hbt_tradeoff/v3"] if REVISION else [])+list(TRADEOFF.glob("*")):
        if folder.name not in ("v3","final"):
            continue
        for path in sorted(folder.glob("*/*/budget.json")):
            budget_paths[path.parts[-4:-1]]=path
    for p in budget_paths.values():
        label,case,point = p.parts[-4:-1]
        row = collect_global(p.parent,case,verify,budget=True)
        row.update(read(p))
        budget_rows.append(dict(version=label,sample=point,**row))
    write_tables(GLOBAL/"hbt_tradeoff","summary",["version","sample",*GLOBAL_COLUMNS,"hbt_budget","hbt_lower_bound",
                 "v3_reference","hbt_budget_used","hbt_budget_excess","budget_feasible"],budget_rows)
    paired=defaultdict(dict)
    for row in budget_rows:
        paired[row["benchmark"],row["sample"]][row["version"]]=row
    budget_comparison=[]
    budgets=("status","used_hbt","hbt_budget_excess","wirelength_um","via_count",
             "base_objective_connected_routes","wall_seconds","independent_check")
    for (case,sample),versions in sorted(paired.items()):
        reference=versions.get("v3",{})
        current=versions.get("final",{})
        row=dict(benchmark=case,sample=sample,hbt_lower_bound=(reference or current)["hbt_lower_bound"])
        for name,data in (("v3",reference),("final",current)):
            row.update({f"{name}_{k}":data.get(k) for k in budgets})
        budget_comparison.append(row)
    write_tables(GLOBAL/"hbt_tradeoff","comparison",["benchmark","sample","hbt_lower_bound",
                 *(f"{name}_{k}" for name in ("v3","final") for k in budgets)],budget_comparison)
    eligible = [r for r in budget_rows if r["status"]=="PASS" and r["independent_check"]=="passed"]
    metrics = ("wirelength_um","via_count","used_hbt")
    nondominated = []
    for row in eligible:
        values = tuple(row[k] for k in metrics)
        related = (other for other in eligible if other["benchmark"]==row["benchmark"])
        if any(all(other[k]<=value for k,value in zip(metrics,values)) and
               any(other[k]<value for k,value in zip(metrics,values)) for other in related):
            continue
        nondominated.append(row)
    write_tables(GLOBAL/"hbt_tradeoff","observed_nondominated",["benchmark","version","sample",*metrics,
                 "hbt_budget","wall_seconds","output_directory"],nondominated)
    times = []
    for label, initial in [("v3",BASE/"global"),("final",GLOBAL/"final")]:
        for case in CASES:
            samples = [collect_global(p,case,verify) for p in [initial,*(GLOBAL/"repeats"/f"{label}-r{i}" for i in (2,3))]]
            row = dict(benchmark=case,version=label,verified_runs=sum(x["status"]=="PASS" for x in samples))
            row.update({f"status_r{i}":sample["status"] for i,sample in enumerate(samples,1)})
            for key in ("wall_seconds","router_reported_seconds","wirelength_um","via_count","used_hbt","max_rss_kb",
                        "best_feasible_objective"):
                for idx,sample in enumerate(samples,1):
                    row[f"{key}_r{idx}"] = sample.get(key)
                nums = [s[key] for s in samples if s["status"]=="PASS" and s.get(key) is not None]
                for suffix,func in [("median",median),("min",min),("max",max)]:
                    row[f"{key}_{suffix}"] = func(nums) if nums else None
            times.append(row)
    write_tables(GLOBAL,"repeat_summary",list(times[0]),times)
    totals=[]
    for version in ("v3","final"):
        designs = [r for r in times if r["version"]==version]
        for scope in ("router_reported_seconds","wall_seconds"):
            samples = {}
            for i in range(1,4):
                key=f"{scope}_r{i}"
                verified=[d[key] for d in designs if d[f"status_r{i}"]=="PASS" and d[key] is not None]
                samples[i]=sum(verified) if len(verified)==len(CASES) else None
            good=[v for v in samples.values() if v is not None]
            totals.append(dict(version=version,scope=scope,complete_rounds=len(good),
                 round_1=samples[1],round_2=samples[2],round_3=samples[3],
                 minimum=min(good) if good else None,median=median(good) if good else None,
                 maximum=max(good) if good else None))
    write_tables(GLOBAL,"repeat_totals",list(totals[0]),totals)
    profiles, counters = [], []
    sources=[("v3",BASE/"profile"),("final",GLOBAL/"profile/final")]
    if REVISION:
        sources.append(("A6-"+REVISION,GLOBAL/"profile"/("A6-"+REVISION)))
    for label,folder in sources:
        for case in CASES:
            job = folder/case
            source = job/"profile.json"
            if not source.is_file():
                continue
            profile = read(source)
            for module,values in profile["modules"].items():
                worker = profile.get("worker_modules",{}).get(module,{})
                profiles.append(dict(version=label,benchmark=case,module=module,**values,
                                     worker_calls=worker.get("calls"),
                                     worker_inclusive_elapsed_seconds=worker.get("inclusive_elapsed_seconds"),
                                     worker_exclusive_elapsed_seconds=worker.get("exclusive_elapsed_seconds")))
            counters.extend(dict(version=label,benchmark=case,counter=key,value=value)
                            for key,value in profile["counters"].items())
    write_tables(GLOBAL,"runtime_breakdown",["version","benchmark","module","calls","inclusive_seconds",
                 "exclusive_seconds","worker_calls","worker_inclusive_elapsed_seconds",
                 "worker_exclusive_elapsed_seconds"],profiles)
    write_tables(GLOBAL,"profile_counters",["version","benchmark","counter","value"],counters)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify",action="store_true",help="Execute independent resource and terminal replay")
    parser.add_argument("--supplements",action="store_true",help="Aggregate ablations, reruns and budget experiments")
    args = parser.parse_args()
    inputs = verify_inputs()
    write(GLOBAL/"verification/input_sha256_rechecked.json",inputs)
    current,reference = GLOBAL/"final",BASE/"global"
    new = [collect_global(current,c,args.verify) for c in CASES]
    old = [collect_global(reference,c,args.verify) for c in CASES]
    write_tables(GLOBAL,"summary",GLOBAL_COLUMNS,new)
    global_metrics = ["wirelength_um","via_count","used_hbt"]
    main_global = compare(GLOBAL,"comparison_v3",old,new,[*global_metrics,"router_reported_seconds"])
    wall_global = compare(GLOBAL,"comparison_v3_wall",old,new,[*global_metrics,"wall_seconds"])
    write(GLOBAL/"experiment_status.json",dict(router=main_global,wall=wall_global,
          cases={r["benchmark"]:r["status"] for r in new},baseline_cases={r["benchmark"]:r["status"] for r in old}))
    new_detail = [collect_detail(current,DETAIL/"final",c,args.verify) for c in CASES]
    old_detail = [collect_detail(reference,DETAIL/"baseline",c,args.verify) for c in CASES]
    write_tables(DETAIL,"summary",DETAIL_COLUMNS,new_detail)
    detail_metrics = ["total_wirelength_um","total_vias","used_hbt"]
    main_detail = compare(DETAIL,"comparison_v3",old_detail,new_detail,[*detail_metrics,"actual_detail_wall_seconds"])
    sum_detail = compare(DETAIL,"comparison_v3_die_sum",old_detail,new_detail,[*detail_metrics,"sum_die_runtime_seconds"])
    write(DETAIL/"experiment_status.json",dict(wall=main_detail,die_sum=sum_detail,
          cases={r["benchmark"]:r["status"] for r in new_detail},
          baseline_cases={r["benchmark"]:r["status"] for r in old_detail}))
    detail_report(old_detail,new_detail)
    if args.supplements:
        supplements(args.verify)
    write_report(old,new,old_detail,new_detail)


if __name__ == "__main__":
    main()
