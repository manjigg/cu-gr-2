"""Fingerprint the compiled V3 sources, official inputs, toolchain and final run outputs."""
import argparse
import hashlib
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import tarfile

from new3d_experiments import G, X, REF, ROOT, read, save, sha


def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT,text=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--outputs',action='store_true',help='Also hash completed official artifacts')
    parser.add_argument('--verify-outputs',action='store_true',help='Verify the existing output checksum records')
    args=parser.parse_args()
    if args.outputs and args.verify_outputs:
        parser.error('Choose output snapshot or output verification')
    prefixes=['src','rsyn','flute','tests/native','scripts','.gitignore']
    paths=git('ls-files','-m','-o','--exclude-standard','--',*prefixes).splitlines()
    source_files={p:sha(ROOT/p) for p in sorted(set(paths)) if (ROOT/p).is_file()}
    release=['run-new3d/route','build-new3d/native_replay','build-new3d/CMakeCache.txt',
             'build-new3d/CMakeFiles/route.dir/flags.make','flute/POST9.dat','flute/POWV9.dat',
             'tests/native/new3d-options.json','tests/native/benchmark-array.json']
    index=hashlib.sha256(json.dumps(source_files,sort_keys=True).encode()).hexdigest()
    archive=X/'module_regression'/('source-overlay-'+index[:16]+'.tar.gz')
    if not archive.exists():
        with tarfile.open(archive,'x:gz') as stream:
            for p in source_files:
                stream.add(ROOT/p,arcname=p,recursive=False)
    protocol=read(REF)
    original=protocol['input_sha256']
    inputs={p:sha(REF.parent/protocol['benchmarks']/p) for p in original}
    technology=read(X/'baseline/technology.json')
    tools={p:sha(Path(p)) for p in technology['tools']}
    geometry=Path('/home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002')
    tools.update({str(p):sha(p) for p in (geometry/'bin').iterdir() if p.is_file()})
    data=dict(time_utc=datetime.now(timezone.utc).isoformat(),branch=git('branch','--show-current').strip(),
              commit=git('rev-parse','HEAD').strip(),base_commit=git('rev-parse','timeopt_native-3d-f2f').strip(),
              git_status=git('status','--short').splitlines(),source_sha256=source_files,
              source_archive=str(archive.relative_to(ROOT)),source_archive_sha256=sha(archive),
              binary_and_build_sha256={p:sha(ROOT/p) for p in release},inputs=inputs,
              original_inputs_match=inputs==original,tools=tools,
              base_tools_match=all(tools[p]==v for p,v in technology['tools'].items()),
              openroad_version=technology['openroad_version'],machine_record='experiment_config.json',
              global_command='python3 scripts/native_bench.py run-new3d/route tests/native/benchmark-array.json result-3/new3D/final --options tests/native/new3d-options.json --seed-seconds 1200 --stage-seconds 15 --master-seconds 15 --threads 4 --jobs 1 --cases aes tinyRocket ethmac mor1kx jpegencode ariane bp',
              detail_command='python3 scripts/native_detail_batch.py /home/wanghh/h3d/OpenROAD/bazel-bin/openroad result-3/new3D/final result-3/new3Ddetail/final /home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002 --threads 8 --jobs 1 --cases aes tinyRocket ethmac mor1kx jpegencode ariane bp')
    formal_overlay=X/'module_regression/source-overlay.tar.gz'
    if formal_overlay.is_file():
        data['formal_source_overlay']=str(formal_overlay.relative_to(ROOT))
        data['formal_source_overlay_sha256']=sha(formal_overlay)
        data['source_snapshot_scope']='formal_source_overlay preserves the checkout used by the formal binary. source_archive also includes later diagnostics, tests, and opt-in topology changes.'
    save(G/'run_provenance.json',data)
    if args.outputs or args.verify_outputs:
        for root in (G,ROOT/'result-3/new3Ddetail'):
            entries={}
            for p in sorted((root/'final').glob('**/*')):
                if p.is_file() and not p.is_symlink():
                    entries[str(p.relative_to(root))]=sha(p)
            record=root/'output_sha256.json'
            if args.verify_outputs:
                expected=read(record)
                differences=sorted(p for p in entries.keys()|expected.keys() if entries.get(p)!=expected.get(p))
                save(root/'output_fingerprint_verification.json',
                     dict(matched=not differences,entries=len(entries),differences=differences))
                if differences:
                    raise RuntimeError('Output fingerprint mismatch in '+str(root))
            else:
                save(record,entries)
    if not (data['original_inputs_match'] and data['base_tools_match']):
        raise RuntimeError('Input or tool fingerprints changed')


if __name__=='__main__':
    main()
