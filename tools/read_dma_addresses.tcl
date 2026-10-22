connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
foreach base {0xa0000000 0xa0020000} {
 puts "VDMA_BASE $base"
 foreach off {0x28 0x30 0x34 0xa0 0xa4 0xa8 0xac 0xb0 0xb4 0xb8} {
  puts [mrd [expr {$base+$off}]]
 }
}
disconnect
exit
