from pathlib import Path
import subprocess,sys,tempfile
exe=sys.argv[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'frame.yuy2'
 for size in [0,2621439,2621441]:
  with p.open('wb') as f:f.truncate(size)
  r=subprocess.run([exe,str(p)],capture_output=True,text=True)
  assert r.returncode==3,(size,r)
  assert 'expected exactly 2621440-byte regular YUY2 file' in r.stderr
 r=subprocess.run([exe,d],capture_output=True,text=True)
 assert r.returncode==3
 r=subprocess.run([exe,'extra','argument'],capture_output=True,text=True)
 assert r.returncode==2
print('TEST_PASS stage-frame invalid size0/short/long directory args before devmem')
