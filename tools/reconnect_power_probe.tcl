connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
foreach addr {0xffd80100 0xffd80118 0xffd80120 0xff260000 0xff260008 0xff26000c 0xff260020} {puts [mrd $addr]}
after 100
puts [mrd 0xff260008 2]
disconnect
exit
