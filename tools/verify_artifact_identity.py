#!/usr/bin/env python3
"""Reject mixing final hardware, preliminary PS configuration, and FSBL sources."""
import hashlib,json,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
final=root/'dist/sc130_platform.xsa'
pre=root/'dist/sc130_platform_preliminary.xsa'
result={'result':'PASS','ps_sources':{},'artifacts':{}}
with zipfile.ZipFile(final) as f,zipfile.ZipFile(pre) as p:
    for name in ('psu_init.c','psu_init.h','psu_init.tcl'):
        a,b=f.read(name),p.read(name)
        if a!=b:raise SystemExit('PS configuration changed since firmware generation: '+name)
        generated=root/'firmware/sc130_uvc_platform/hw'/name
        if generated.read_bytes()!=a:raise SystemExit('generated hardware source mismatch: '+name)
        result['ps_sources'][name]=sha(a)
    bits=[name for name in f.namelist() if name.endswith('.bit')]
    if len(bits)!=1:raise SystemExit('expected one bitstream in final XSA')
    if f.read(bits[0])!=(root/'dist/sc130_platform.bit').read_bytes():
        raise SystemExit('final XSA and standalone bitstream differ')
fsbl=root/'boot/fsbl/build/source/psu_init.c'
if fsbl.read_bytes()!=(root/'firmware/sc130_uvc_platform/hw/psu_init.c').read_bytes():
    raise SystemExit('FSBL compiled PSU initialization mismatch')
for name in ('dist/sc130_platform.bit','dist/sc130_platform.xsa',
             'boot/fsbl/build/fsbl_sc130.elf','firmware/sc130_uvc_platform/zynqmp_pmufw/pmufw.elf',
             'boot/artifacts/bl31.elf','boot/artifacts/u-boot.elf',
             'linux/dist/Image','linux/dist/sc130-platform.dtb','linux/dist/initramfs-sc130.cpio.gz'):
    path=root/name;data=path.read_bytes()
    result['artifacts'][name]={'bytes':len(data),'sha256':sha(data)}
(root/'reports/artifact_identity.json').write_text(json.dumps(result,indent=2)+'\n')
print('ARTIFACT_IDENTITY_PASS: final XSA bit and all PSU sources agree')
