connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "Cortex-A53 #0"}
puts [targets]
puts [rrd pc]
puts [rrd x0]
puts [rrd x1]
puts [rrd x2]
puts [rrd x3]
puts [rrd lr]
targets -set -filter {name == "PSU"}
puts [mrd 0xfffd8f70 2]
puts [mrd 0xff5e0200]
disconnect
exit
