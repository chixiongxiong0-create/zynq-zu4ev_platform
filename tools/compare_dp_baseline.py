"""Read-only comparison of preserved DP source and integrated source.

Writes an evidence receipt; does not assert that source equals flashed binaries.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'sc130_dp_test/camera'

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def pair(a, b):
    return dict(baseline=str(a), integrated=str(b), baseline_sha256=digest(a),
                integrated_sha256=digest(b), identical=a.read_bytes() == b.read_bytes())

files = [pair(p, ROOT/'rtl'/p.name) for p in sorted((BASE/'rtl').glob('*.v'))
         if (ROOT/'rtl'/p.name).exists()]
for a, b in [('src/sc130_table.h','boot/fsbl/camera/sc130_table.h'),
             ('src/sensor_i2c.c','boot/fsbl/camera/sensor_i2c.c'),
             ('src/camera_pipeline.c','boot/fsbl/camera/camera_pipeline.c'),
             ('rtl/raw10_to_yuy2.v','reference/raw10_to_yuy2_linear.v')]:
    files.append(pair(BASE/a, ROOT/b))

old_bd = BASE/'build/uvc-v2/project/sc130_uvc_v2.srcs/sources_1/bd/ps_system/ps_system.bd'
new_bd = ROOT/'build/project/sc130_platform.srcs/sources_1/bd/ps_system/ps_system.bd'
old = json.loads(old_bd.read_text(encoding='utf-8'))['design']
new = json.loads(new_bd.read_text(encoding='utf-8'))['design']
cells = {}
for name in ['vdma','vdma1','memory_xbar','control_xbar','ps']:
    a = old['components'][name].get('parameters', {})
    b = new['components'][name].get('parameters', {})
    changes = {k: {'old': a.get(k), 'new': b.get(k)}
               for k in sorted(a.keys() | b.keys()) if a.get(k) != b.get(k)}
    cells[name] = dict(parameter_count=len(a), changes=changes)
receipt = dict(scope='source and saved BD comparison; no hardware access', files=files,
               baseline_bd=str(old_bd), integrated_bd=str(new_bd), cells=cells,
               interface_nets_identical=old['interface_nets']==new['interface_nets'],
               nets_identical=old['nets']==new['nets'],
               addressing_identical=old['addressing']==new['addressing'])
out = ROOT/'reports/dp_baseline_comparison.json'
out.write_text(json.dumps(receipt, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
print(f'{len(files)} file pairs: {sum(x["identical"] for x in files)} identical')
for name, item in cells.items():
    print(name, 'parameter changes:', len(item['changes']))
    # Full values (including long generated protection maps) stay in JSON.
    if item['changes']:
        print(' changed keys:', ', '.join(item['changes']))
print('receipt:', out)
