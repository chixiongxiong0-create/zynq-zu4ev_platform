# Only RAM changes. Production QSPI contents are untouched.
# Usage: xsdb ram_boot.tcl READY_SYMBOL TEST_BOOT_BIN FIT_IMAGE
if {$argc != 3} {error "usage: ram_boot.tcl READY_SYMBOL TEST_BOOT_BIN FIT_IMAGE"}
set root [file normalize [file join [file dirname [info script]] ..]]
set ready [lindex $argv 0]
set boot [file normalize [lindex $argv 1]]
set fit [file normalize [lindex $argv 2]]
foreach path [list $boot $fit] {if {![file isfile $path]} {error "missing $path"}}
if {[file size $boot] > 0xc00000 || [file size $fit] > 0x1400000} {error IMAGE_TOO_LARGE}
proc upload {path base} {
    targets -set -filter {name == "PSU"}
    set input [open $path rb]
    set part "$path.stage"
    set offset 0
    while {![eof $input]} {
        set data [read $input 65536]
        if {![string length $data]} {break}
        set output [open $part wb]
        puts -nonewline $output $data
        close $output
        dow -data $part [expr {$base+$offset}]
        incr offset [string length $data]
        if {$offset % 0x100000 == 0} {puts "RAM_STAGE $path $offset"}
    }
    close $input
    file delete $part
}
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
mwr 0xff5e0200 0x100
targets -set -filter {name == "Cortex-A53 #0"}
rst -system
after 1000
targets -set -filter {name == "PSU"}
mwr 0xff5e0200 0x2100
# Power USB before entering FSBL: its pre-PSU delay requires a timer still reset.
# SET/TRIG are write-only; write the requested mask directly, never read-modify-write.
mwr 0xffd80118 0x00100000
mwr 0xffd80120 0x00100000
set powered 0
for {set i 0} {$i < 100} {incr i} {
    if {([mrd -value 0xffd80100] & 0x00100000) != 0} {set powered 1;break}
    after 10
}
if {!$powered} {error USB_PREPOWER_TIMEOUT}
puts USB_PREPOWER_READY
mwr 0xffca0038 0x1ff
after 500
targets -set -filter {name =~ "MicroBlaze PMU"}
dow $root/firmware/sc130_uvc_platform/zynqmp_pmufw/pmufw.elf
con
after 500
targets -set -filter {name == "Cortex-A53 #0"}
rst -processor -clear-registers
dow $root/boot/fsbl/build/ramtest/fsbl_sc130.elf
bpremove -all
set bp [bpadd -addr $ready]
con
after 8000
if {[catch {stop} result] && ![string match {*Already stopped*} $result]} {error $result}
set pc [rrd pc]
puts "RAM_READY_PC $pc"
if {![string match -nocase "*[string range $ready 2 end]*" $pc]} {error RAM_READY_NOT_REACHED}
upload $boot 0x30000000
upload $fit 0x10000000
targets -set -filter {name == "Cortex-A53 #0"}
bpremove $bp
con
puts RAM_BOOT_DISPATCHED
disconnect
exit
