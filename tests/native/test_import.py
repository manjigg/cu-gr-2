"""Compare the two simultaneous CUGR snapshots with independent DEF inventories."""
import json
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
binary = pathlib.Path(sys.argv[1]).resolve()
config_file = root/'tests/native/aes-input.json'
config = json.loads(config_file.read_text())

def inspect(manifest, success=True):
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp)/'manifest.json'
        path.write_text(json.dumps(manifest))
        result = subprocess.run([binary,'-inspect-native',path],text=True,capture_output=True)
        if not success:
            assert result.returncode != 0, result.stdout
            return
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout.split('NATIVE_INPUT ')[1])

expected = []
for die in config['dies']:
    for name in ('lef','def'):
        die[name] = str((config_file.parent/die[name]).resolve())
    text = pathlib.Path(die['def']).read_text()
    nets = re.search(r'^NETS (\d+) ;(.*?)^END NETS',text,re.M|re.S)
    expected.append((int(nets[1]),len(re.findall(r'\(\s*\S+\s+\S+\s*\)',nets[2]))))
for _ in range(2):
    result = inspect(config)
    assert [(d['local_nets'],d['pins']) for d in result['dies']] == expected
    assert result['global_nets'] == sum(n for n,_ in expected)-1
    assert result['cross_die_nets'] == 1 and result['mapping_ports'] == 2
    assert result['real_pins']+2 == sum(p for _,p in expected)
    assert all(d['via_layer_pairs']==6 for d in result['dies'])
    config['dies'].reverse()
    expected.reverse()
inspect(dict(config,mapping=config['mapping']*2),False)
inspect(dict(config,mapping=[{'top':{'net':'_00002_','cut_port':'T1'},'bottom':{'net':'_00002_'}}]),False)
print('PASS isolated imports in both orders, explicit mapping, pin counts, duplicate mapping, false cut port')
