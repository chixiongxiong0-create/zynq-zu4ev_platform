connect -url tcp:127.0.0.1:3121
puts [targets]
targets -set -filter {name == "PSU"}
puts "BOOT_MODE [mrd 0xff5e0200]"
puts "RESET [mrd 0xff5e0218]"
targets -set -filter {name == "Cortex-A53 #0"}
puts "PC [rrd pc]"
disconnect
exit
