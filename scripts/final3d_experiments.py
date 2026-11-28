"""Run frozen and current CUGR experiments with source, input and output provenance."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time

from native_bench import CASES

ROOT = Path(__file__).resolve().parents[1]
GLOBAL = ROOT / "result-3/final3D"
DETAIL = ROOT / "result-3/final3Ddetail"
BASE = GLOBAL / "baseline"
PROTOCOL = ROOT / "tests/native/benchmark-array.json"
REFERENCE = ROOT / "tests/native/new3d-options.json"
FINAL = ROOT / "tests/native/final3d-options.json"
BUDGET = ROOT / "tests/native/final3d-budget-options.json"
OLD_ROUTER = BASE / "build/route"
REVISION = os.environ.get("CUGR_FINAL3D_REVISION","")
if REVISION and not re.fullmatch(r"r[0-9]+",REVISION):
    raise ValueError("Expected a final3D experiment revision such as r2")
SUFFIX = "-"+REVISION if REVISION else ""
CONFIG = GLOBAL / "config" / REVISION if REVISION else GLOBAL / "config"
ROUTER = GLOBAL / ("config/bin"+SUFFIX) / "route"
REPLAY = ROUTER.with_name("native_replay")
ABLATION = GLOBAL / "ablation" / REVISION
CHECKPOINT_STUDY = GLOBAL / ("exchange/stage2"+SUFFIX)
TRADEOFF = GLOBAL / "hbt_tradeoff" / REVISION
OPENROAD = ROOT.parent / "OpenROAD/bazel-bin/openroad"
GEOMETRY = ROOT.parent / "OpenROAD/h3d_drt_validation/run_002"
REPRESENTATIVE = ["aes", "ethmac", "ariane", "bp"]


def read(path):
    return json.loads(path.read_text())


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    temp.replace(path)


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""):
            sha.update(block)
    return sha.hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def verify_inputs():
    spec = read(PROTOCOL)
    root = (PROTOCOL.parent / spec["benchmarks"]).resolve()
    actual = {name: digest(root / name) for name in spec["input_sha256"]}
    if actual != spec["input_sha256"]:
        raise ValueError("Original LEF or DEF fingerprint changed")
    return actual


def freeze(build):
    if git("branch", "--show-current") != "end-3d-new-f2f":
        raise ValueError("Expected the final3d development branch")
    target = ROUTER.parent
    target.mkdir(parents=True, exist_ok=True)
    for name in ("route", "native_replay"):
        src, dest = build / name, target / name
        if dest.exists() and digest(src) != digest(dest):
            raise ValueError("Frozen binary differs from requested build")
        if not dest.exists():
            shutil.copy2(src, dest)
    paths = git("ls-files", "-m", "-o", "--exclude-standard", "--", "src", "scripts", "tests/native", "docs").splitlines()
    changed = {p: digest(ROOT/p) for p in paths if (ROOT/p).is_file()}
    CONFIG.mkdir(parents=True,exist_ok=True)
    overlay = CONFIG/"source-overlay.tar.gz"
    temp = overlay.with_suffix(".tmp")
    with tarfile.open(temp,"w:gz") as archive:
        for name in changed:
            archive.add(ROOT/name,arcname=name,recursive=False)
    temp.replace(overlay)
    shutil.copy2(build/"CMakeCache.txt",CONFIG/"CMakeCache.txt")
    source_manifest = dict(commit=git("rev-parse","HEAD"), baseline_commit=git("rev-parse","3d-new-f2f"),
                           branch=git("branch","--show-current"), tracked_workspace=git("status","--short"),
                           source_changes_sha256=changed, system=platform.platform(),
                           compiler=subprocess.check_output(["c++","--version"], text=True).splitlines()[0],
                           cmake=subprocess.check_output(["cmake","--version"], text=True).splitlines()[0],
                           cpu=subprocess.check_output(["lscpu"], text=True), memory=Path("/proc/meminfo").read_text(),
                           inputs_sha256=verify_inputs(), benchmark_protocol_sha256=digest(PROTOCOL),
                           reference_sha256=digest(REFERENCE), route_sha256=digest(ROUTER),
                           replay_sha256=digest(REPLAY), v3_route_sha256=digest(OLD_ROUTER),
                           cmake_cache_sha256=digest(build/"CMakeCache.txt"),source_overlay_sha256=digest(overlay),
                           flute_sha256={n:digest(ROOT/"flute"/n) for n in ("POST9.dat","POWV9.dat")},
                           openroad_sha256=digest(OPENROAD), geometry_audit_sha256=digest(GEOMETRY/"audit.py"),
                           openroad_version=subprocess.check_output([OPENROAD,"-version"],text=True).strip())
    source_manifest["cpu_affinity_at_freeze"]=sorted(os.sched_getaffinity(0))
    source_manifest["experiment_revision"]=REVISION or "r1"
    write(CONFIG/"binary_provenance.json",source_manifest)
    write(GLOBAL/"run_provenance.json",source_manifest)
    write(DETAIL/"run_provenance.json",{k:source_manifest[k] for k in
        ("commit","baseline_commit","inputs_sha256","openroad_sha256","openroad_version","geometry_audit_sha256")})


def receipt_path(directory):
    return directory.with_name(directory.name + ".run.json")


def run(directory, command, input_files, cases, completion_file):
    """Only reuse matching completed results. Interrupted or failed attempts retain their own paths."""
    directory.parent.mkdir(parents=True, exist_ok=True)
    signature = {str(p):digest(p) for p in input_files}
    attempt = 1
    while True:
        out = directory if attempt==1 else directory.with_name(directory.name+f".attempt-{attempt}")
        receipt = receipt_path(out)
        args = list(map(str, command(out)))
        if receipt.exists():
            previous = read(receipt)
            if previous["input_sha256"] != signature or previous["args"] != args:
                raise ValueError(f"Incompatible existing artifact under {out}")
            if previous["state"]=="COMPLETE":
                files = [out/"summary.tsv",*(out/c/completion_file for c in cases)]
                checksums = previous.get("result_sha256")
                if all(f.is_file() for f in files) and (checksums is None or
                        all(digest(f)==checksums.get(str(f)) for f in files)):
                    return out
        elif not out.exists():
            break
        attempt += 1
    state = dict(args=args,input_sha256=signature,state="RUNNING",wall_start_unix=time.time(),
                 load_before=Path("/proc/loadavg").read_text().strip(),cpu_affinity=sorted(os.sched_getaffinity(0)))
    write(receipt,state)
    start = time.monotonic()
    with out.with_name(out.name+".driver.log").open("x") as log:
        proc = subprocess.run(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    files = [out/"summary.tsv",*(out/c/completion_file for c in cases)]
    state.update(state="COMPLETE" if all(f.is_file() for f in files) else "FAILED",
                 returncode=proc.returncode,wall_seconds=time.monotonic()-start,
                 result_sha256={str(f):digest(f) for f in files if f.is_file()})
    write(receipt,state)
    print(out,proc.returncode,flush=True)
    return out


def option_file(label, obj):
    file = CONFIG / (label+".json")
    if file.exists() and read(file)!=obj:
        raise ValueError(f"Frozen options differ in {file}")
    if not file.exists():
        write(file,obj)
    return file


def bench(binary, options, directory, cases, profile=False):
    return run(directory,lambda out:[sys.executable,ROOT/"scripts/native_bench.py",binary,PROTOCOL,out,
        "--options",options,"--threads","4","--jobs","1","--seed-seconds","1200",
        "--stage-seconds","15","--master-seconds","15","--cases",*cases,*(["--profile"] if profile else [])],
        [binary,options,PROTOCOL],cases,"process.json")


def detail(source, directory, cases):
    paths = [OPENROAD,GEOMETRY/"audit.py",GEOMETRY/"export_geometry.tcl",GEOMETRY/"support/check_joint_routed.py"]
    for case in cases:
        paths.append(source/case/"manifest.json")
        paths.extend(source/case/"output"/p for p in ["top.guide","bottom.guide","components.jsonl","hbt_assignment.tsv"])
    return run(directory,lambda out:[sys.executable,ROOT/"scripts/native_detail_batch.py",OPENROAD,source,out,GEOMETRY,
        "--threads","8","--jobs","1","--cases",*cases],paths,cases,"report.json")


def variants():
    original = read(REFERENCE)
    joint = dict(original,optimization_mode="joint",prefer_hbt_nonincrease=False)
    topology = dict(joint,enable_topology_refinement=True,enable_hbt_augmentation=True,
                    enable_steiner_migration=True,enable_edge_reconnection=True)
    coordinated = dict(topology,enable_topology_coordination=True)
    archive = dict(coordinated,enable_topology_archive=True,enable_topology_safe_pruning=True)
    exchange = dict(archive,enable_topology_exchange=True)
    complete = dict(exchange,enable_topology_group_regeneration=True)
    return {
        "A0":original, "A1":joint, "A2":topology, "A3":coordinated, "A4":archive,
        "A5":exchange, "A6":complete, "A6-rotation":dict(complete,enable_topology_rotation=True),
        "A6-no-dominance":dict(complete,enable_topology_safe_pruning=False),
    }


def screen():
    configs=variants()
    labels=(["A6","A3","A4","A5","A6-rotation","A6-no-dominance"] if REVISION
            else [label for label in configs if label!="A0"])
    for label in labels:
        cfg=configs[label]
        binary=OLD_ROUTER if label in ("A1","A2") else ROUTER
        bench(binary,option_file(label,cfg),ABLATION/label,REPRESENTATIVE)


def stage2(cases):
    settings=variants()
    snapshot_options=option_file("snapshot",dict(settings["A0"],stages=[1,2],save_stage2_checkpoint=True))
    snapshots={}
    for case in cases:
        original=GLOBAL/"exchange/stage2-source"/case
        snapshot=original/case/"output/stage2.checkpoint"
        complete=(original/case/"process.json").is_file() and read(original/case/"process.json")["exit_code"]==0
        if REVISION and snapshot.is_file() and complete:
            snapshots[case]=snapshot
        else:
            initial=bench(ROUTER,snapshot_options,GLOBAL/("exchange/stage2-source"+SUFFIX)/case,[case])
            snapshots[case]=initial/case/"output/stage2.checkpoint"
    labels=(["A6","A3","A4","A5","A6-rotation","A6-no-dominance"] if REVISION
            else ["A0","A1","A2","A3","A4","A5","A6","A6-rotation","A6-no-dominance"])
    for label in labels:
        for case in cases:
            checkpoint = snapshots[case]
            opts=option_file(f"snapshot-{label}-{case}",dict(settings[label],stages=[2],load_stage2_checkpoint=str(checkpoint)))
            out=bench(ROUTER,opts,CHECKPOINT_STUDY/label/case,[case])
            write(out/"common_checkpoint.json",dict(benchmark=case,path=str(checkpoint),sha256=digest(checkpoint)))


def budget(cases):
    original=read(REFERENCE)
    enhanced=read(BUDGET)
    for case in cases:
        source=BASE/"global"/case
        ledger=read(source/"ledger-replay.json")
        pins=bytearray(ledger["nets"])
        with (source/"output/components.jsonl").open() as file:
            for line in file:
                item=json.loads(line)
                if item["pins"]:
                    pins[item["net_id"]]|=1<<item["die"]
        if not all(pins):
            raise ValueError(f"Incomplete baseline real-terminal inventory for {case}")
        lower=pins.count(3)
        reference=ledger["hbt_count"]
        values=list(dict.fromkeys([None,reference,lower+(reference-lower)//2,lower]))
        for number in values:
            suffix="uncapped" if number is None else str(number)
            for label,router,settings in [("v3",OLD_ROUTER,original),("final",ROUTER,enhanced)]:
                options=dict(settings,prefer_hbt_nonincrease=False)
                options.pop("hbt_budget",None)
                options.update(optimization_mode="joint" if number is None else "budgeted")
                if number is not None:
                    options["hbt_budget"]=number
                folder=TRADEOFF/label/case/suffix
                control=GLOBAL/"hbt_tradeoff/v3"/case/suffix
                if REVISION and label=="v3" and (control/"budget.json").is_file():
                    prior=read(control/(case+"/manifest.json"))
                    if all(prior.get(k)==v for k,v in options.items()):
                        continue
                folder=bench(router,option_file(f"budget-{label}-{case}-{suffix}",options),folder,[case])
                write(folder/"budget.json",dict(hbt_budget=number,hbt_lower_bound=lower,v3_reference=reference))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite",choices=["freeze","screen","stage2","formal","formal-first","repeats","budget",
                                         "detail","detail-v3","detail-final","profile"])
    parser.add_argument("--build",type=Path,default=ROOT/"build/final3d")
    parser.add_argument("--cases",nargs="+",choices=CASES)
    parser.add_argument("--variant",choices=list(variants()),help="Select a diagnostic profile configuration")
    args=parser.parse_args()
    if args.suite!="freeze":
        verify_inputs()
    if args.suite=="freeze":
        freeze(args.build.resolve())
    elif args.suite=="screen":
        screen()
    elif args.suite=="stage2":
        lock=GLOBAL/("exchange/stage2"+SUFFIX+".lock")
        lock.parent.mkdir(parents=True,exist_ok=True)
        with lock.open("a") as file:
            fcntl.flock(file,fcntl.LOCK_EX)
            stage2(args.cases or REPRESENTATIVE)
    elif args.suite in ("formal","formal-first","repeats"):
        cases=args.cases or CASES
        if cases!=CASES:
            parser.error("Formal comparison uses the full benchmark suite")
        if args.suite!="repeats":
            bench(ROUTER,FINAL,GLOBAL/"final",cases)
        if args.suite!="formal-first":
            for repeat in (2,3):
                order=[(OLD_ROUTER,REFERENCE,"v3"),(ROUTER,FINAL,"final")]
                if repeat==3:
                    order.reverse()
                for router,config,version in order:
                    bench(router,config,GLOBAL/"repeats"/(version+f"-r{repeat}"),cases)
    elif args.suite=="budget":
        budget(args.cases or CASES)
    elif args.suite in ("detail","detail-v3","detail-final"):
        cases=args.cases or CASES
        if args.suite!="detail-final":
            detail(BASE/"global",DETAIL/"baseline",cases)
        if args.suite!="detail-v3":
            detail(GLOBAL/"final",DETAIL/"final",cases)
    elif args.suite=="profile":
        options=option_file(args.variant,variants()[args.variant]) if args.variant else FINAL
        router=OLD_ROUTER if args.variant in ("A0","A1","A2") else ROUTER
        directory=GLOBAL/"profile"/("final" if args.variant is None else args.variant+SUFFIX)
        bench(router,options,directory,args.cases or CASES,True)


if __name__=="__main__":
    main()
