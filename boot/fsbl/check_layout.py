import re,sys
from pathlib import Path

def check(text):
 sections=[]
 for m in re.finditer(r'^([.][\w.]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)',text,re.M):
  name,start,size=m.group(1),int(m.group(2),16),int(m.group(3),16)
  if not size: continue
  if 0xfffc0000<=start<0xfffe9e00 and start+size>0xfffe9e00: raise ValueError('FSBL overlaps handoff: '+name)
  if name=='.handoff_params' and (start!=0xfffe9e00 or size>512): raise ValueError('Invalid handoff section')
  if 0xfffe9e00<=start<0xffff0000 and name!='.handoff_params': raise ValueError('FSBL code overlaps ATF: '+name)
  sections.append((name,start,size))
 if not any(n=='.handoff_params' for n,a,s in sections): raise ValueError('Missing handoff params')
 return sections
if __name__=='__main__':
 s=check(Path(sys.argv[1]).read_text()); print('FSBL_OCM_LAYOUT_PASS sections='+str(len(s)))
