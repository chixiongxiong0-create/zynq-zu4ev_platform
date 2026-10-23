# Portable reconstruction from exported Vivado 2025.1 configuration.
set root [file normalize [file join [file dirname [info script]] ..]]
file mkdir $root/build $root/reports $root/dist
create_project sc130_platform $root/build/project -part xczu4ev-sfvc784-2-i
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property verilog_define {SC130_UVC} [get_filesets sources_1]
add_files [glob $root/rtl/*.v]
add_files [list $root/isp/isp.v $root/isp/raw10_to_yuy2.v $root/isp/sc130_tonemap_yuy2.v]
add_files -fileset constrs_1 $root/constraints/board.xdc
foreach name {camera_clocks camera_ila csi2_rx_uvc uvc_ila} {source $root/hardware/$name.tcl}
source $root/hardware/ps_system.tcl
set bd [get_files ps_system.bd]
validate_bd_design
save_bd_design
make_wrapper -files $bd -top -import
set_property top sc130_camera_top_v2 [get_filesets sources_1]
set_property AUTO_INCREMENTAL_CHECKPOINT 0 [get_runs synth_1]
update_compile_order -fileset sources_1
generate_target all $bd
puts PLATFORM_RECREATE_PASS
close_project
exit
