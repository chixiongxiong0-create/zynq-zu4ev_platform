set root [file normalize [file join [file dirname [info script]] ..]]
open_project $root/build/project/sc130_platform.xpr
open_run impl_1
report_utilization -hierarchical -file $root/reports/utilization.rpt
report_clocks -file $root/reports/clocks.rpt
report_drc -file $root/reports/inspection_drc.rpt
check_timing -verbose -file $root/reports/check_timing.rpt
write_xdc -force -exclude_physical $root/reports/implemented_constraints.xdc
set output [open $root/reports/implementation_checks.txt w]
puts $output "setup_slack=[get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]"
puts $output "hold_slack=[get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]"
foreach clock [get_clocks] {puts $output "$clock period=[get_property PERIOD $clock]"}
puts $output "drc_errors=[llength [get_drc_violations -filter {SEVERITY == Error}]]"
close $output
close_project
puts PLATFORM_INSPECTION_PASS
exit
