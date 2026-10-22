connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
mwr 0xff5e0200 0
puts [mrd 0xff5e0200]
targets -set -filter {name == "Cortex-A53 #0"}
rst -system
after 12000
puts [targets]
disconnect
exit
