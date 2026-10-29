#!/usr/bin/env python3
"""Copy finished detailed-route artifacts and refresh the project-level CSV."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

PROJECT = Path('/home/wanghh/h3d/cu-gr-2')
SOURCE = Path('/home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002')
TARGET = PROJECT / 'detail-routing'
CSV = PROJECT / 'detail-routing.csv'
CASES = ('aes', 'tinyRocket', 'ethmac', 'mor1kx', 'ariane', 'jpegencode', 'bp')
DONE = {'passed', 'completed_with_drc', 'failed_route', 'failed_verification'}
FINAL = {'passed', 'completed_with_failures', 'worker_failed'}


def now():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def atomic_json(path, value):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
    os.replace(temporary, path)


def atomic_csv(path, rows):
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('w', newline='', encoding='utf-8-sig') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary, path)


def copy_checked(source, destination, cache):
    if source.is_symlink():
        raise ValueError('Refusing a symlink as a routing artifact: ' + str(source))
    key = str(destination.relative_to(TARGET))
    stat = source.stat()
    stamp = [stat.st_size, stat.st_mtime_ns]
    previous = cache.get(key)
    if destination.exists():
        if previous is None:
            raise FileExistsError('Unmanaged destination file exists: ' + str(destination))
        target_stat = destination.stat()
        target_stamp = [target_stat.st_size, target_stat.st_mtime_ns]
        if target_stamp != previous['destination_stamp']:
            if sha(destination) != previous['sha256']:
                raise RuntimeError('Destination was modified; refusing to overwrite: ' + str(destination))
            previous['destination_stamp'] = target_stamp
        if stamp == previous['source_stamp']:
            return
    digest = sha(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + '.sync-tmp')
    shutil.copy2(source, temporary)
    if sha(temporary) != digest:
        raise RuntimeError('Routing artifact copy failed its SHA-256 check')
    os.replace(temporary, destination)
    stat = destination.stat()
    cache[key] = {'source': str(source), 'source_stamp': stamp,
                  'destination_stamp': [stat.st_size, stat.st_mtime_ns], 'sha256': digest}


def sum_when_known(pair, key):
    values = [r.get(key) for r in pair]
    if any(v is None for v in values):
        return None
    total = sum(values)
    return round(total, 6) if isinstance(total, float) else total


def synchronize():
    owner = json.loads((TARGET / 'source_run.json').read_text())
    if owner['source'] != str(SOURCE) or owner['summary_csv'] != str(CSV):
        raise RuntimeError('Result directory ownership metadata does not match')
    state = json.loads((SOURCE / 'status.json').read_text())
    manifest_path = TARGET / 'copy_manifest.json'
    cache = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    for name in ('plan.json', 'native_preflight.json', 'guide_preflight.json'):
        copy_checked(SOURCE / name, TARGET / name, cache)
    for name in ('identifier_recheck_before.json', 'SUMMARY_before_identifier_recheck.md'):
        if (SOURCE / name).is_file():
            copy_checked(SOURCE / name, TARGET / name, cache)
    copied_jobs = []
    for case in CASES:
        for die in ('top', 'bottom'):
            key = case + '_' + die
            job = state['jobs'][key]
            if job['state'] not in DONE:
                continue
            source = SOURCE / 'jobs' / key
            destination = TARGET / case / die
            for path in sorted(source.rglob('*')):
                if path.is_file() and not path.name.endswith('.tmp'):
                    copy_checked(path, destination / path.relative_to(source), cache)
            if job.get('routed_def'):
                if sha(destination / 'routed.def') != job['routed_def']['sha256']:
                    raise RuntimeError('Copied DEF differs from the completed routing status: ' + key)
            copied_jobs.append(key)
        if case in state.get('joint_checks', {}):
            for suffix in ('.json', '.log'):
                source = SOURCE / 'joint' / (case + suffix)
                if source.exists():
                    copy_checked(source, TARGET / 'joint' / source.name, cache)
    rows, die_rows = [], []
    for case in CASES:
        top, bottom = [state['jobs'][case + '_' + d] for d in ('top', 'bottom')]
        pair = [top, bottom]
        joint = state.get('joint_checks', {}).get(case, 'pending')
        if all(v['state'] == 'passed' for v in pair) and joint == 'passed':
            status = 'passed'
        elif any(v['state'] in {'failed_route', 'failed_verification', 'completed_with_drc'} for v in pair) or joint == 'failed':
            status = 'failed_or_requires_review'
        elif all(v['state'] == 'passed' for v in pair):
            status = 'awaiting_joint_check'
        elif any(v['state'] in ('routing', 'auditing') for v in pair):
            status = 'running'
        elif all(v['state'] == 'queued' for v in pair):
            status = 'queued'
        else:
            status = 'partially_completed'
        completed = all(v.get('routing_output_complete') for v in pair)
        metrics = json.loads((SOURCE / 'inputs' / case / 'metrics.json').read_text())
        hbt_count = metrics['hbt']
        if joint == 'passed':
            joint_report = json.loads((SOURCE / 'joint' / (case + '.json')).read_text())
            if joint_report['status'] != 'passed' or joint_report['hbt_pairs_checked'] != hbt_count:
                raise RuntimeError('Assigned HBT count differs from verified physical pairs: ' + case)
            hbt_count = joint_report['hbt_pairs_checked']
        row = {'benchmark': case,
               'total_wire_length_um': sum_when_known(pair, 'final_wirelength_um') if completed else None,
               'total_via_count': sum_when_known(pair, 'final_via_count') if completed else None,
               'hbt_count': hbt_count,
               'total_drc': sum_when_known(pair, 'final_drc_count') if completed else None,
               'sum_wall_seconds': sum_when_known(pair, 'wall_seconds') if completed else None,
               'status': status}
        rows.append(row)
        for die, value in zip(('top', 'bottom'), pair):
            die_rows.append({'benchmark': case, 'die': die, 'status': value['state'],
                'final_drc': value.get('final_drc_count'), 'wire_length_um': value.get('final_wirelength_um'),
                'via_count': value.get('final_via_count'),
                'wall_seconds': value.get('wall_seconds') if value.get('return_code') is not None else None,
                'geometry': value.get('geometry_state', 'pending'), 'return_code': value.get('return_code'),
                'geometry_report': str(TARGET / case / die / value.get('geometry_audit_report', 'geometry_audit.json')),
                'output_directory': str(TARGET / case / die)})
    atomic_json(TARGET / 'source_status.json', state)
    atomic_csv(CSV, rows)
    atomic_csv(TARGET / 'die_summary.csv', die_rows)
    atomic_json(manifest_path, cache)
    report = {'updated_at_utc': now(), 'source_updated_at': state['updated_at'], 'source_batch_state': state['state'],
              'final': state['state'] in FINAL, 'copied_jobs': copied_jobs, 'copied_job_count': len(copied_jobs),
              'passed_benchmarks': [r['benchmark'] for r in rows if r['status'] == 'passed'],
              'summary_csv': str(CSV), 'copied_files': len(cache), 'all_copies_hash_verified': True}
    atomic_json(TARGET / 'sync_status.json', report)
    print(json.dumps(report, ensure_ascii=False), flush=True)
    return report['final']


def setup():
    if TARGET.exists() or CSV.exists():
        raise FileExistsError('Refusing to overwrite an existing detail-routing directory or CSV')
    if not (SOURCE / 'status.json').is_file():
        raise FileNotFoundError('Detailed-routing source status not found')
    TARGET.mkdir()
    (TARGET / 'joint').mkdir()
    shutil.copy2(Path(__file__).resolve(), TARGET / 'sync.py')
    atomic_json(TARGET / 'source_run.json', {'source': str(SOURCE), 'summary_csv': str(CSV),
                                           'created_at_utc': now(), 'sync_interval_seconds': 30})
    (TARGET / 'README.md').write_text('''# 当前代码的详细布线结果副本

原任务由独立 OpenROAD 后台 worker 执行。本目录是实际文件副本，不是符号链接。
每个任务完成后复制 DEF、最终/中间 DRC、日志、命令、耗时、压缩几何和独立验证报告，并核对 SHA-256。

- 项目根目录 `detail-routing.csv`：每个 benchmark 一行，仅列两侧合计线长、普通 via 数、HBT 数、DRC、耗时及最终状态。
- `die_summary.csv`：14 个 die 的逐项指标。
- `<case>/top/`、`<case>/bottom/`：已完成任务的详细布线成果，其中 `routed.def`、`drc.rpt`、`run.log` 最常用。
- `joint/<case>.json`：双层 HBT 联合连通检查。
- `source_status.json`、`sync_status.json`：原任务状态及复制进度。
- `copy_manifest.json`：每份副本的来源与 SHA-256。

`sync.py` 是独立后台同步程序，每 30 秒更新；整批结束后再完成一次同步并退出。
未完成任务的最终指标留空，不填成 0。案例总线长、via、DRC 和耗时仅在两侧布线输出都完成后填写。
`sum_wall_seconds` 为两侧耗时之和，不是并发批次的总墙钟时间。
`hbt_count` 是上下两侧配对的 HBT 数，一个 HBT 的两片焊盘只计一次，不将 Top/Bottom 重复相加；已通过案例的数量与联合检查 `hbt_pairs_checked` 核对一致。普通 via 则统计两侧之和，与 HBT 分开统计。

原始 command/status/provenance 文件中的 OpenROAD 路径保留用于追溯；本地成果位置以根目录 CSV 的 output_directory 为准。
自动管理的 CSV 会持续刷新；请勿在同步期间修改本目录中的结果副本，程序检测到人工改动会拒绝覆盖并记录错误。
'''.replace('detail-routing.csv', CSV.name))
    finished = synchronize()
    if finished:
        return
    with (TARGET / 'sync.log').open('x') as stream:
        child = subprocess.Popen([sys.executable, '-B', str(TARGET / 'sync.py'), 'watch',
                                  '--source', str(SOURCE), '--destination', str(TARGET), '--csv', str(CSV)],
                                 # Explicit paths keep a copied watcher on the
                                 # selected experiment, rather than 3um defaults.
                                 cwd=PROJECT, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
    atomic_json(TARGET / 'launcher.json', {'pid': child.pid, 'launched_at_utc': now()})
    print(json.dumps({'sync_worker_pid': child.pid, 'destination': str(TARGET), 'csv': str(CSV)}, ensure_ascii=False))


def watch():
    with (TARGET / 'sync.lock').open('x') as stream:
        stream.write(str(os.getpid()))
    try:
        while not synchronize():
            time.sleep(30)
    except BaseException as error:
        atomic_json(TARGET / 'sync_error.json', {'at_utc': now(), 'error': repr(error)})
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('setup', 'watch', 'once'))
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--destination', type=Path, default=TARGET)
    parser.add_argument('--csv', type=Path, default=CSV)
    a = parser.parse_args()
    SOURCE, TARGET, CSV = a.source.resolve(), a.destination.resolve(), a.csv.resolve()
    setup() if a.phase == 'setup' else watch() if a.phase == 'watch' else synchronize()
