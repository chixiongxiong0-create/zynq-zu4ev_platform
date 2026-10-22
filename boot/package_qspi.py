#!/usr/bin/env python3
"""Offline image packaging; never accesses hardware."""
import argparse, gzip, hashlib, json, shutil, subprocess
from pathlib import Path
FLASH_SIZE=32*1024*1024
FIT_OFFSET=12*1024*1024
FIT_LIMIT=FLASH_SIZE-FIT_OFFSET

def validate_sizes(boot_size,fit_size):
 if not 0 < boot_size <= FIT_OFFSET: raise ValueError('BOOT.BIN must be nonempty and fit below 0x00c00000')
 if not 0 < fit_size <= FIT_LIMIT: raise ValueError('FIT must be nonempty and fit in final 20 MiB')
 return FIT_OFFSET+fit_size

def merge(boot,fit,output):
 a,b=Path(boot).read_bytes(),Path(fit).read_bytes(); validate_sizes(len(a),len(b))
 Path(output).write_bytes(a+b'\xff'*(FIT_OFFSET-len(a))+b)

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--boot-bin',required=True,type=Path); ap.add_argument('--kernel',required=True,type=Path)
 ap.add_argument('--dtb',required=True,type=Path); ap.add_argument('--initrd',required=True,type=Path)
 ap.add_argument('--output-dir',required=True,type=Path); ap.add_argument('--mkimage',default='mkimage')
 args=ap.parse_args()
 for v in [args.boot_bin,args.kernel,args.dtb,args.initrd]:
  if not v.is_file(): ap.error(f'Missing input: {v}')
 out=args.output_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
 if args.kernel.stat().st_size>0x03e00000: ap.error('Uncompressed kernel overlaps U-Boot at 0x04000000')
 if args.dtb.stat().st_size>0x100000: ap.error('DTB overlaps initrd')
 if args.initrd.stat().st_size>0x09f00000: ap.error('Initrd overlaps FIT staging at 0x10000000')
 (out/'Image.gz').write_bytes(gzip.compress(args.kernel.read_bytes(),mtime=0))
 shutil.copyfile(args.dtb,out/'system.dtb'); shutil.copyfile(args.initrd,out/'initramfs.cpio.gz')
 its='''/dts-v1/;
/ { description = "SC130 unified QSPI Linux"; #address-cells = <1>;
 images {
  kernel { data = /incbin/("Image.gz"); type = "kernel"; arch = "arm64"; os = "linux"; compression = "gzip"; load = <0x00200000>; entry = <0x00200000>; hash { algo = "sha256"; }; };
  fdt { data = /incbin/("system.dtb"); type = "flat_dt"; arch = "arm64"; compression = "none"; load = <0x06000000>; hash { algo = "sha256"; }; };
  ramdisk { data = /incbin/("initramfs.cpio.gz"); type = "ramdisk"; arch = "arm64"; os = "linux"; compression = "none"; load = <0x06100000>; hash { algo = "sha256"; }; };
 };
 configurations { default = "conf-sc130"; conf-sc130 { kernel = "kernel"; fdt = "fdt"; ramdisk = "ramdisk"; }; };
};
'''
 (out/'sc130.its').write_text(its)
 subprocess.run([args.mkimage,'-f','sc130.its','image.ub'],cwd=out,check=True)
 merge(args.boot_bin,out/'image.ub',out/'QSPI_LINUX.bin')
 manifest={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [args.boot_bin,out/'image.ub',out/'QSPI_LINUX.bin']}
 manifest['layout']={'flash_bytes':FLASH_SIZE,'fit_offset':FIT_OFFSET,'fit_limit':FIT_LIMIT}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__': main()
