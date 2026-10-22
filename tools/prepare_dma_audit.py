"""Generate a RAM-only capture diagnostic module; production source untouched.

Poison each target before DMA, then check every U/V byte after completion.
SC130 grayscale YUY2 must overwrite each chroma byte with 0x80. A surviving
0x5a byte proves absent overwrite or stale CPU visibility, not which of those.
"""
from pathlib import Path
import shutil
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--freeze', action='store_true', help='stop rearming after N audited frames; retain userspace mappings')
parser.add_argument('--park', action='store_true', help='single-variable trial: use VDMA parking mode')
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
out = root/'build/dma-audit'
out.mkdir(parents=True, exist_ok=True)
src = (root/'linux/kernel/sc130_capture.c').read_text(encoding='utf-8')

def replace(old, new):
    global src
    if src.count(old) != 1:
        raise RuntimeError(f'Expected unique source site: {old[:80]}')
    src = src.replace(old, new)

replace('static bool trace_frames;', '''static unsigned audit_frames=32;
module_param(audit_frames,uint,0444);
MODULE_PARM_DESC(audit_frames,"RAM diagnostic: poison/verify first N frames per channel");
static bool trace_frames;''')
replace('    bool timedout;', '    unsigned audit_left;\n    bool timedout;')
replace('    dma_addr_t address[2];', '    dma_addr_t address[2];\n    bool poisoned;')
replace('    dma_sync_sgtable_for_device(ch->dma->device->dev,b->sgt[ci],DMA_FROM_DEVICE);', '''    b->poisoned=false;
    if(ch->audit_left) {
        void *pixels=vb2_plane_vaddr(&b->vb.vb2_buf,0);
        if(!pixels)goto failed;
        memset(pixels,0x5a,SIZE);
        b->poisoned=true;
        ch->audit_left--;
        dev_info(s->v4l2.dev,"AUDIT_ARM ch=%u id=%d epoch=%u base=%pad size=%u\\n",ci,id,epoch,&b->address[ci],SIZE);
    }
    dma_sync_sgtable_for_device(ch->dma->device->dev,b->sgt[ci],DMA_FROM_DEVICE);''')
replace('        b->vb.sequence=seq;b->vb.field=V4L2_FIELD_NONE;', '''        if(good && b->poisoned && !s->fatal) {
            const u32 *pixels=vb2_plane_vaddr(&b->vb.vb2_buf,0);
            unsigned bad=0,first=SIZE;
            if(!pixels)good=false;
            else for(unsigned p=0;p<SIZE/4;p++) {
                if((pixels[p]&0xff00ff00U)!=0x80008000U) {
                    if(!bad)first=p*4;
                    bad++;
                }
            }
            dev_info(s->v4l2.dev,"AUDIT_DONE ch=%u id=%d seq=%u base=%pad bytes=%u bad_words=%u first=%u valid=%u head=%08x tail=%08x\\n",ci,id,seq,&b->address[ci],bytes,bad,first,good,pixels?pixels[0]:0,pixels?pixels[SIZE/4-1]:0);
            if(bad)good=false;
        }
        b->vb.sequence=seq;b->vb.field=V4L2_FIELD_NONE;''')
replace('        s->ch[i].timedout=false;s->ch[i].miss_start=readl(s->ch[i].gate+MISS);',
        '        s->ch[i].audit_left=audit_frames;\n        s->ch[i].timedout=false;s->ch[i].miss_start=readl(s->ch[i].gate+MISS);')
if args.freeze:
    replace('    spin_lock_irqsave(&s->lock,f);id=dual_begin(&s->core,ci);',
            '    if(audit_frames && !ch->audit_left)return; /* bounded diagnostic only */\n    spin_lock_irqsave(&s->lock,f);id=dual_begin(&s->core,ci);')
if args.park:
    replace('struct xilinx_vdma_config config={.frm_cnt_en=1,.coalesc=1,.park=0};',
            'struct xilinx_vdma_config config={.frm_cnt_en=1,.coalesc=1,.park=1};')
(out/'sc130_capture.c').write_text(src, encoding='utf-8')
for name in ['Makefile','dual_core.h','sc130_contract.h']:
    shutil.copyfile(root/'linux/kernel'/name, out/name)
print('RAM-only diagnostic generated:', out)
