#!/usr/bin/env python3
"""Offline checks, only a temporary fake configfs is written."""
from pathlib import Path
import os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 e=dict(os.environ,CONFIGFS_ROOT=d,SC130_TEST='1',USB_VID='0x1234',USB_PID='0x5678')
 subprocess.run(['sh',str(root/'setup_uvc.sh'),'prepare'],env=e,check=True)
 g=Path(d)/'sc130_platform';f=g/'functions/uvc.usb1';frame=f/'streaming/uncompressed/yuy2/1280x1024'
 assert (frame/'dwDefaultFrameInterval').read_text()=='83333\n'
 assert (frame/'dwFrameInterval').read_text()=='83333\n166667\n'
 assert (frame/'dwMaxVideoFrameBufferSize').read_text()=='2621440\n'
 assert (frame.parent/'guidFormat').read_bytes()==bytes.fromhex('5955593200001000800000aa00389b71')
 assert (f/'streaming/class/ss/h').is_symlink()
 assert not (f/'streaming/class/hs/h').exists() and not (f/'streaming/class/fs/h').exists()
 assert (g/'UDC').read_text()=='\n'
 print('TEST_PASS gadget SS-only YUY2 default120 unbound')
if len(sys.argv)>1:
 dt=sys.argv[1]
 def get(node,prop,t='x'):return subprocess.check_output(['fdtget','-t',t,dt,node,prop],text=True).strip()
 assert get('/amba-pl/dma-controller@a0000000/dma-channel-s2mm','interrupts')=='0 59 4'
 assert get('/amba-pl/dma-controller@a0020000/dma-channel-s2mm','interrupts')=='0 5b 4'
 assert get('/amba-pl/capture@a0010000','interrupts')=='0 5a 4 0 5c 4'
 for node in ['/amba-pl/capture@a0010000','/axi/usb@ff9d0000','/axi/display@fd4a0000','/axi/dma-controller@fd4c0000','/axi/spi@ff0f0000']:
  assert get(node,'status','s')=='okay',node
 assert get('/axi/phy@fd400000','clock-names','s')=='ref1 ref2'
 assert get('/usb-gtr-ref1','clock-frequency')=='18cba80'
 assert get('/dp-gtr-ref2','clock-frequency')=='19bfcc0'
 assert get('/reserved-memory/qspi_staging@30000000','reg')=='0 30000000 0 4000000'
 phys=get('/axi/display@fd4a0000','phys').split()
 assert phys[1:5]==['3','6','0','2'] and phys[6:10]==['2','6','1','2']
 assert get('/axi/display@fd4a0000','phy-names','s')=='dp-phy0 dp-phy1'
 assert get('/axi/usb@ff9d0000','phys').split()[1:]==['1','4','0','1']
 assert get('/axi/usb@ff9d0000/usb@fe200000','maximum-speed','s')=='super-speed'
 out=get('/axi/display@fd4a0000/ports/port@5/endpoint','remote-endpoint')
 dest=get('/dp_connector/port/endpoint','phandle')
 assert out==dest
 print('TEST_PASS DT capture IRQ89..92 DP USB clocks GTR lanes connector graph QSPI staging')
