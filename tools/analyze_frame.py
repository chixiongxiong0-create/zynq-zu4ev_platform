#!/usr/bin/env python3
"""Inspect a real completed YUY2 frame without altering the saved sensor data."""
import argparse, hashlib, json, re
from pathlib import Path
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('frame',type=Path)
    p.add_argument('--output-dir',required=True,type=Path)
    a=p.parse_args()
    data=a.frame.read_bytes()
    if len(data)!=1280*1024*2:
        raise SystemExit('wrong frame length')
    pix=np.frombuffer(data,dtype=np.uint8).reshape(1024,2560)
    y=pix[:,0::2];uv=pix[:,1::2]
    output=a.output_dir;output.mkdir(parents=True,exist_ok=True)
    Image.fromarray(y).save(output/'camera_y_code.png')
    # Correct limited-range expansion for a normal PNG viewer.
    preview=np.clip((y.astype(np.float32)-16)*255/219,0,255).astype(np.uint8)
    Image.fromarray(preview).save(output/'camera_preview.png')
    result={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
            'luma_min':int(y.min()),'luma_max':int(y.max()),'luma_mean':float(y.mean()),
            'luma_percentiles':dict(zip(['p1','p10','p50','p90','p99'],np.percentile(y,[1,10,50,90,99]).tolist())),
            'chroma_non_128':int(np.count_nonzero(uv!=128)),
            'luma_outside_16_235':int(np.count_nonzero((y<16)|(y>235))),
            'preview_conversion':'PNG=(Y-16)*255/219, clipped; source untouched'}
    (output/'frame_analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    fig,ax=plt.subplots(figsize=(8,4));ax.hist(y.ravel(),bins=np.arange(256),log=True)
    ax.set(xlabel='Stored Y code',ylabel='Pixel count (log)',title='Captured tone-mapped frame')
    fig.tight_layout();fig.savefig(output/'luma_histogram.png',dpi=160);plt.close(fig)
    rtl=(Path(__file__).resolve().parents[1]/'isp/raw10_to_yuy2.v').read_text()
    tone=np.array([int(v,16) for v in re.findall(r"tone_rom\[\d+\] = 8'h([0-9A-Fa-f]{2})",rtl)])
    if len(tone)!=1024:
        raise SystemExit('expected 1024-entry current LUT')
    raw=np.arange(1024);linear=16+(raw*219+511)//1023
    fig,ax=plt.subplots(figsize=(8,4));ax.plot(raw,linear,label='Linear bypass');ax.plot(raw,tone,label='Current static tone LUT')
    ax.set(xlabel='RAW10 code',ylabel='Stored Y code',title='ISP transfer curves',xlim=(0,1023),ylim=(0,255));ax.legend();ax.grid(alpha=.2)
    fig.tight_layout();fig.savefig(output/'tone_curve.png',dpi=160);plt.close(fig)
    print(json.dumps(result,indent=2))
    if result['chroma_non_128'] or result['luma_outside_16_235']:
        raise SystemExit('unexpected ISP output codes')

if __name__=='__main__':main()
