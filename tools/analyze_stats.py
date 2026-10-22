#!/usr/bin/env python3
"""Measure independent capture/DP/USB counter deltas over a stable log window."""
import argparse, json, re
from pathlib import Path

def rows(text):
    found=[]
    for line in text.splitlines():
        if 'stats ns=' not in line:
            continue
        row={k:int(v) for k,v in re.findall(r'([a-z_]+)=(\d+)',line)}
        if all(k in row for k in ('ns','capture','sent','returned','dp_flips','invalid','drop','usb_error')):
            if not found or row['ns']>found[-1]['ns']:
                found.append(row)
    return found

def measure(data, start, seconds):
    chosen=[r for r in data if r['ns']>=start*1e9]
    if not chosen:
        raise ValueError('no samples at requested start')
    first=chosen[0]
    end=first['ns']+seconds*1e9
    chosen=[r for r in chosen if r['ns']<=end]
    if len(chosen)<2:
        raise ValueError('need at least two samples')
    last=chosen[-1]
    elapsed=(last['ns']-first['ns'])/1e9
    delta={k:last[k]-first[k] for k in ('capture','sent','returned','dp_flips','invalid','drop','usb_error')}
    if any(v<0 for v in delta.values()):
        raise ValueError('counters reset in window')
    return {'elapsed_seconds':elapsed,'first':first,'last':last,'delta':delta,
            'rates_fps':{k:delta[k]/elapsed for k in ('capture','sent','returned','dp_flips')}}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',type=Path)
    p.add_argument('--start',type=float,default=0,help='minimum board monotonic seconds')
    p.add_argument('--seconds',type=float,default=120)
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    result=measure(rows(a.log.read_text(errors='replace')),a.start,a.seconds)
    output=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(output)
    print(output)
