connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
foreach base {0x20900000 0x20c00000 0x20f00000 0x21200000} {
 puts "BUFFER $base"
 puts [mrd $base 4]
 puts [mrd [expr {$base+2621440-16}] 4]
}
disconnect
exit
