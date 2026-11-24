"""Run the independent OpenDB conductor audit with bijective DEF instance-name decoding."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys


def instance_names(raw_names,decode):
    aliases={}
    owners={}
    for raw in raw_names:
        logical=decode(raw)
        if owners.setdefault(logical,raw)!=raw:
            raise ValueError('Ambiguous DEF instance '+logical)
        for token in (raw,logical):
            if aliases.setdefault(token,logical)!=logical:
                raise ValueError('Ambiguous instance spelling '+token)
    return aliases


class ExportReader:
    def __init__(self,core,source):
        self.core=core
        self.names=instance_names(core.section_records(source.read_text(),'COMPONENTS'),core.canonical)

    def __getattr__(self,name):
        return getattr(self.core,name)

    def read_geometry(self,path):
        data=self.core.read_geometry(path)
        # OpenDB 可以保留 DEF 原始反斜线，独立审计使用逻辑名。只改端点索引键，
        # 保留矩形、Via cut、金属层、端点组和全部导体坐标。
        mapped={}
        for (net,inst,pin),group in data['terms'].items():
            key=(net,inst if inst=='PIN' else self.names[inst],pin)
            if key in mapped:
                raise ValueError('Duplicate exported physical terminal '+str(key))
            mapped[key]=group
        data['terms']=mapped
        return data


def audit(tool,run,side,recheck):
    sys.path.insert(0,str(tool/'support'))
    import check_joint_routed as core
    spec=importlib.util.spec_from_file_location('geometry_driver',tool/'audit.py')
    driver=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(driver)
    jobs=json.loads((run/'plan.json').read_text())['jobs']
    original=next(j for j in jobs if j['id']==side)
    reader=ExportReader(core,Path(original['input_def']))
    if recheck:
        # 独立重导出实物。首次失败的几何、报告和 DRT 日志继续保留。
        dest=run/side/'geometry-recheck'
        dest.mkdir()
        folder=dest/side
        folder.mkdir()
        (folder/'routed.def').symlink_to(run/side/'routed.def')
        for name in ('bin','support','export_geometry.tcl'):
            (dest/name).symlink_to(run/name)
        (dest/'plan.json').write_text(json.dumps(dict(jobs=[dict(original,directory=str(folder))])))
        run=dest
    return driver.audit_job(run,side,reader)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tool',type=Path)
    parser.add_argument('run',type=Path)
    parser.add_argument('side',choices=['top','bottom'])
    parser.add_argument('--recheck',action='store_true')
    args=parser.parse_args()
    raise SystemExit(audit(args.tool.resolve(),args.run.resolve(),args.side,args.recheck))
