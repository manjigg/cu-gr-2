"""Exercise the actual Stage 3 job pool on same-cell HBT contention."""
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts'))
from native_check import check

binary = Path(sys.argv[1]).resolve()
base = root / 'tests/native'
cfg = json.loads((base / 'dual.json').read_text())
for die in cfg['dies']:
    for key in ('lef', 'def'):
        die[key] = str((base / die[key]).resolve())
cfg.update(routing_mode='fast', stages=[3], stage_seconds=8, net_seconds=1,
           enable_parallel_maze=True, maze_batch_factor=2, maze_max_retries=2,
           conflict_workers=1)
with TemporaryDirectory() as temp:
    folder = Path(temp)
    for threads, guided in ((1,False),(2,False),(4,False),(2,True),(4,True)):
        cfg.update(enable_conflict_aware_batching=guided,enable_adaptive_parallelism=guided)
        for repeat in range(2):
            dest = folder / f'run-{threads}-{guided}-{repeat}'
            manifest = folder / f'manifest-{threads}-{guided}-{repeat}.json'
            manifest.write_text(json.dumps(cfg))
            subprocess.run([binary,'-native',manifest,'-output',dest,'-threads',str(threads)],
                           env=dict(os.environ,CUGR_NATIVE_PROFILE='1'),check=True,stdout=subprocess.DEVNULL)
            verified = check(manifest,dest)
            assert verified['hbt_count'] == 2
            stats = json.loads((dest/'routing_statistics.json').read_text())
            stage = stats['stages'][0]['parallel_maze']
            if guided:
                assert stage['resource_conflict_rejections'] == 0 and stage['conflict_deferrals'] > 0, stage
                assert stage['retry_count'] == 0
            else:
                assert stage['resource_conflict_rejections'] >= 1 and stage['retry_count'] >= 1, stage
            assert stage['accepted'] == 2 and stage['maze_worker_cpu_seconds'] >= 0
            assert 1 <= stage['actual_workers'] <= threads
            profile = json.loads((dest/'profile.json').read_text())
            assert profile['worker_modules']['stage_3_maze']['calls'] == stage['maze_tasks']
            assert profile['counters']['astar_expanded_nodes'] == stage['astar_expanded_nodes']
            ledger = dest/'ledger.json'
            subprocess.run([binary.parent/'native_replay',manifest,dest/'route_edges.tsv',ledger],
                           check=True,stdout=subprocess.DEVNULL)
            assert json.loads(ledger.read_text())['resource_keys_after_release'] == 0
    cfg.update(enable_maze_memory_budget=True,maze_worker_reserve_kb=1073741824)
    dest=folder/'memory-serial'
    manifest=folder/'memory.json'
    manifest.write_text(json.dumps(cfg))
    subprocess.run([binary,'-native',manifest,'-output',dest,'-threads','4'],check=True,stdout=subprocess.DEVNULL)
    assert check(manifest,dest)['hbt_count']==2
    stats=json.loads((dest/'routing_statistics.json').read_text())
    assert stats['maze_memory_policy']['serial_fallback'] and 'serial_maze' in stats['stages'][0]
