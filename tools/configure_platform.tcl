set root E:/workspace/AI_Projects/ZU4EV/sc130_platform
open_project $root/build/project/sc130_platform.xpr
foreach file [get_files] {
 if {[string match -nocase {*sc130_dp_test/camera/rtl/*} $file] || [string match -nocase {*sc130_dp_test/camera/constraints/*} $file]} {remove_files $file}
}
foreach file [glob $root/rtl/*.v] {if {[file tail $file] ne "raw10_to_yuy2.v"} {add_files $file}}
add_files $root/isp/raw10_to_yuy2.v
add_files $root/isp/sc130_tonemap_yuy2.v
add_files -fileset constrs_1 $root/constraints/board.xdc
set_property top sc130_camera_top_v2 [get_filesets sources_1]
open_bd_design [get_files ps_system.bd]
source -notrace C:/Users/pries/.codex/plugins/cache/amd-ross-agentic-ai-assistant/amd-ross-agentic-ai-assistant/2026.9.1/skills/vivado-ip-configurator/lib/ipcfg.tcl
set ::ipcfg::native_log_path $root/reports/configure.log
puts [ipcfg::python_status]
puts [ipcfg::python_exec -c {import argparse,base64,hashlib,json,pathlib,re,sys,typing; print("PYTHON_IMPORTS:OK")}]
set cfg [dict create CONFIG.PSU__USB0__PERIPHERAL__ENABLE 1 CONFIG.PSU__USB0__PERIPHERAL__IO {MIO 52 .. 63} CONFIG.PSU__USB3_0__PERIPHERAL__ENABLE 1 CONFIG.PSU__USB3_0__PERIPHERAL__IO {GT Lane1} CONFIG.PSU__USB0__REF_CLK_SEL {Ref Clk1} CONFIG.PSU__USB0__REF_CLK_FREQ 26 CONFIG.PSU__USB__RESET__MODE {Separate MIO pin} CONFIG.PSU__USB__RESET__POLARITY {Active Low} CONFIG.PSU__USB0__RESET__ENABLE 1 CONFIG.PSU__USB0__RESET__IO {MIO 36}]
set receipt [ipcfg::capture $root/reports/platform_usb_config.tcldict [list ipcfg::require ps $cfg {SC130 unified DP GTR3/2 and USB0 GTR1} ] {REQ:pinned+applied}]
puts $receipt
if {[dict get $receipt status] ne "OK"} {error PLATFORM_PS_CONFIG_FAILED}
if {[llength [ipcfg::verify_stuck ps $cfg]]} {error PLATFORM_PS_CONFIG_REVERTED}
set f [open $root/reports/ps_readback.txt w]
foreach key [concat [dict keys $cfg] {CONFIG.PSU__DISPLAYPORT__PERIPHERAL__ENABLE CONFIG.PSU__DP__LANE_SEL CONFIG.PSU__DP__REF_CLK_SEL CONFIG.PSU__DP__REF_CLK_FREQ CONFIG.PSU__DISPLAYPORT__LANE0__IO CONFIG.PSU__DISPLAYPORT__LANE1__IO CONFIG.PSU__DPAUX__PERIPHERAL__IO CONFIG.PSU__CRF_APB__DP_VIDEO_REF_CTRL__SRCSEL}] {puts $f "$key=[get_property $key [get_bd_cells ps]]"}
close $f
validate_bd_design -force
save_bd_design
generate_target -force all [get_files ps_system.bd]
update_compile_order -fileset sources_1
write_project_tcl -force $root/tools/recreate_project.tcl
write_hw_platform -fixed -force -file $root/dist/sc130_platform_preliminary.xsa
close_project
puts PLATFORM_CONFIG_PASS
exit
