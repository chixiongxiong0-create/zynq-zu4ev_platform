set root [file normalize [file join [file dirname [info script]] ..]]
open_project $root/build/project/sc130_platform.xpr
if {![llength [get_files -quiet */isp/sc130_tonemap_yuy2.v]]} {add_files $root/isp/sc130_tonemap_yuy2.v}
set_property AUTO_INCREMENTAL_CHECKPOINT 0 [get_runs synth_1]
set_property INCREMENTAL_CHECKPOINT {} [get_runs synth_1]
update_compile_order -fileset sources_1
set_param general.maxThreads 2
reset_runs synth_1
launch_runs synth_1 -jobs 1
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "SYNTH_FAILED [get_property STATUS [get_runs synth_1]]"}
open_run synth_1
set f [open $root/reports/vdma_driver_check.txt w]
foreach channel {vdma vdma1} {
    foreach pin_name {m_axi_s2mm_awready m_axi_s2mm_wready m_axi_s2mm_bvalid m_axi_s2mm_bresp[0] m_axi_s2mm_bresp[1]} {
        set target [get_pins -hier -filter "NAME == ps_i/$channel/$pin_name"]
        set net [get_nets -segments -of_objects $target]
        set drivers [get_pins -leaf -of_objects $net -filter {DIRECTION == OUT}]
        puts $f "$pin_name target=$target drivers=$drivers"
        if {[llength $target] != 1 || [llength $drivers] == 0} {error "UNDRIVEN_VDMA_PIN $channel $pin_name"}
    }
}
close $f
report_timing_summary -file $root/reports/synth_timing.rpt
close_design
reset_runs impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 1
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "IMPL_FAILED [get_property STATUS [get_runs impl_1]]"}
open_run impl_1
report_timing_summary -report_unconstrained -file $root/reports/timing.rpt
report_drc -file $root/reports/drc.rpt
report_cdc -file $root/reports/cdc.rpt
report_methodology -file $root/reports/methodology.rpt
set setup_slack [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold_slack [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$setup_slack < 0 || $hold_slack < 0} {error "TIMING_FAILED setup=$setup_slack hold=$hold_slack"}
if {[llength [get_drc_violations -filter {SEVERITY == Error}]]} {error DRC_ERRORS}
write_debug_probes -force $root/dist/sc130_platform.ltx
file copy -force [get_property DIRECTORY [get_runs impl_1]]/sc130_camera_top_v2.bit $root/dist/sc130_platform.bit
write_hw_platform -fixed -include_bit -force -file $root/dist/sc130_platform.xsa
puts PLATFORM_BUILD_PASS
close_project
exit

