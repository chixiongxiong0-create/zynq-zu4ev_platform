import unittest
import tempfile
from pathlib import Path
import package_qspi as p
class Layout(unittest.TestCase):
 def test_limits(self):
  self.assertEqual(p.validate_sizes(12*1024*1024,20*1024*1024),32*1024*1024)
  for a,b in [(12*1024*1024+1,1),(1,20*1024*1024+1),(0,1),(1,0)]:
   with self.assertRaises(ValueError): p.validate_sizes(a,b)
 def test_merge_preserves_offsets(self):
  with tempfile.TemporaryDirectory() as d:
   d=Path(d); (d/'boot').write_bytes(b'boot'); (d/'fit').write_bytes(b'fit')
   p.merge(d/'boot',d/'fit',d/'out')
   v=(d/'out').read_bytes(); self.assertEqual(v[:4],b'boot'); self.assertEqual(v[4:p.FIT_OFFSET],b'\xff'*(p.FIT_OFFSET-4)); self.assertEqual(v[p.FIT_OFFSET:],b'fit')
if __name__=='__main__': unittest.main()
