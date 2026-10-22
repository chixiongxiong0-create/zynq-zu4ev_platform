"""Decode a read-only PMU RAM dump supplied by the main task."""
import argparse,json,struct
from pathlib import Path
ap=argparse.ArgumentParser(); ap.add_argument('dump',type=Path); ap.add_argument('--base',type=lambda x:int(x,0),required=True)
a=ap.parse_args(); b=a.dump.read_bytes(); rows=json.loads((Path(__file__).parent/'pmu-parent-addresses.json').read_text())
read=lambda addr:struct.unpack_from('<I',b,addr-a.base)[0]
result=[]
for r in rows:
 addr=int(r['parent_pointer_address'],16)
 if a.base<=addr<=a.base+len(b)-4:
  v=read(addr)
  if v==0xffdd5450: result.append(r['symbol'])
print(json.dumps({'dump_bytes':len(b),'base':hex(a.base),'vpll_child_count':read(0xffdd6cf0),'generic_vpll_parent_matches':result},indent=2))
