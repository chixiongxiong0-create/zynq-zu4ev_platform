import unittest
from prepare_fsbl import replace_body, patch_clock_parents
from check_layout import check
class Hooks(unittest.TestCase):
 def test_clock_parent_hook_after_psu_init(self):
  text='u32 Status; Status=psu_init();\nXFsbl_Out32(AMS_PS_SYSMON_ANALOG_BUS, PS_SYSMON_ANALOG_BUS_VAL);\nreturn Status;'
  patched=patch_clock_parents(text)
  self.assertLess(patched.index('psu_init()'),patched.index('0xFD1A0074U'))
  self.assertIn('if (Status == XFSBL_SUCCESS)',patched)
  self.assertIn('0xFD1A007CU',patched)
  self.assertIn('0xFD1A00C0U',patched)
  self.assertIn('0x303U',patched)
  self.assertNotIn('0xFD1A0070',patched)
  self.assertIn('& ~7U) | 3U',patched)
 def test_nested_body_replaced(self):
  self.assertEqual(replace_body('u32 Hook(void) { if(1) { x(); } return 0; } tail','Hook','return 1;'),'u32 Hook(void) {\nreturn 1;\n} tail')
 def test_handoff_overlap_rejected(self):
  with self.assertRaises(ValueError): check('.text 0xfffc0000 0x30000\n.handoff_params 0xfffe9e00 0x200')
 def test_valid_layout(self):
  self.assertEqual(len(check('.text 0xfffc0000 0x20000\n.handoff_params 0xfffe9e00 0x200\n.bitstream_buffer 0xffff0040 0xf000')),3)
if __name__=='__main__': unittest.main()
