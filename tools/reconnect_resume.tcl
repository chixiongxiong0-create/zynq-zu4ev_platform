connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "Cortex-A53 #0"}
puts [rrd pc]
bpremove -all
con
after 5000
puts [targets]
disconnect
exit
