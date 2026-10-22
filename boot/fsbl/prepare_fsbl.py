#!/usr/bin/env python3
"""Copy and patch generated FSBL sources without changing generated firmware."""
import argparse, re, shutil
from pathlib import Path

def patch_clock_parents(text):
 marker='XFsbl_Out32(AMS_PS_SYSMON_ANALOG_BUS, PS_SYSMON_ANALOG_BUS_VAL);'
 if text.count(marker)!=1: raise ValueError('Missing unique post-PSU hook location')
 # Must precede XFsbl_PmInit/PMU PmClockInit: firmware records all selected
 # PLL children, including disabled audio/STC clocks. Keep VPLL video-only.
 body='''
    if (Status == XFSBL_SUCCESS) {
        /* Keep the main interconnect independent of variable video VPLL.
         * DPLL / 3 is 400 MHz; preserve the generated divide-by-three. */
        XFsbl_Out32(0xFD1A00C0U, (XFsbl_In32(0xFD1A00C0U) & ~7U) | 3U);
        XFsbl_Out32(0xFD1A0074U, (XFsbl_In32(0xFD1A0074U) & ~7U) | 3U);
        XFsbl_Out32(0xFD1A007CU, (XFsbl_In32(0xFD1A007CU) & ~7U) | 3U);
        if ((XFsbl_In32(0xFD1A00C0U) & 0x3F07U) != 0x303U ||
            (XFsbl_In32(0xFD1A0074U) & 7U) != 3U ||
            (XFsbl_In32(0xFD1A007CU) & 7U) != 3U) return XFSBL_FAILURE;
        XFsbl_Printf(DEBUG_GENERAL, "DP_CLOCK_PARENTS main=DPLL/3 audio/stc=RPLL_TO_FPD video=VPLL\\r\\n");
    }
'''
 return text.replace(marker,marker+'\n'+body)

def replace_body(text,name,body):
 m=re.search(r'\bu32\s+'+name+r'\s*\([^)]*\)\s*\{',text)
 if not m: raise ValueError('Missing generated hook '+name)
 start=m.end(); depth=1; end=start
 while depth:
  if text[end]=='{': depth+=1
  elif text[end]=='}': depth-=1
  end+=1
 return text[:start]+'\n'+body+'\n'+text[end-1:]

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--source',type=Path,required=True); ap.add_argument('--output',type=Path,required=True)
 ap.add_argument("--ram-test",action="store_true")
 args=ap.parse_args(); here=Path(__file__).resolve().parent
 if not (args.source/'xfsbl_hooks.c').is_file(): ap.error('Source must contain generated xfsbl_hooks.c')
 args.output.mkdir(parents=True,exist_ok=True)
 for p in args.source.iterdir():
  if p.is_file() and p.suffix in ('.c','.h','.S','.ld'): shutil.copyfile(p,args.output/p.name)
 hooks=args.output/'xfsbl_hooks.c'; text=hooks.read_text()
 text+='\n'
 includes='extern int sc130_power_up(unsigned mask);\nextern u32 Sc130AfterBitstream(void);\nextern u32 Sc130BeforeHandoff(void);\n'
 text=text.replace('#include "psu_init.h"','#include "psu_init.h"\n'+includes)
 text=replace_body(text,'XFsbl_HookAfterBSDownload','    return Sc130AfterBitstream();')
 text=replace_body(text,'XFsbl_HookBeforeHandoff','    (void)EarlyHandoff;\n    return Sc130BeforeHandoff();')
 m=re.search(r'\bu32\s+XFsbl_HookPsuInit\s*\(\s*void\s*\)\s*\{',text)
 if not m: raise ValueError('Missing PSU init hook')
 # Before any generated psu_init, including USB domain MMIO.
 text=text[:m.end()]+'\n    if (!sc130_power_up(0x00100000U)) return XFSBL_FAILURE;\n'+text[m.end():]
 text=patch_clock_parents(text)
 hooks.write_text(text)
 if args.ram_test:
  qspi=args.output/'xfsbl_qspi.c'; text=qspi.read_text()
  m=re.search(r'\bu32\s+XFsbl_Qspi32Copy\s*\([^)]*\)\s*\{',text)
  if not m: raise ValueError('Missing QSPI32 copy function')
  body='''
#ifdef SC130_RAM_BOOT_TEST
    extern void Sc130RamBootReady(void);
    static unsigned ready;
    if (!ready) {
        ready = 1;
        Xil_DCacheDisable(); Xil_ICacheDisable();
        Sc130RamBootReady();
    }
    if ((u64)SrcAddress + Length > 0x02000000U) return XFSBL_FAILURE;
    const volatile u8 *source=(const volatile u8 *)(UINTPTR)(0x30000000U+SrcAddress);
    u8 *destination=(u8 *)DestAddress;
    for (u32 i=0;i<Length;++i) destination[i]=source[i];
    Xil_DCacheFlush();
    return XFSBL_SUCCESS;
#endif
'''
  qspi.write_text(text[:m.end()]+body+text[m.end():])
 for p in [here/'sc130_power.c',here/'sc130_sensor_start.c',here/'ram_boot_support.c',*(here/'camera').iterdir()]:
  if p.is_file(): shutil.copyfile(p,args.output/p.name)
if __name__=='__main__': main()

