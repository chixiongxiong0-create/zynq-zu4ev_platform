set root E:/workspace/AI_Projects/ZU4EV/sc130_platform
open_project E:/workspace/AI_Projects/ZU4EV/sc130_dp_test/camera/build/uvc-v2/project/sc130_uvc_v2.xpr
save_project_as -exclude_run_results -include_local_ip_cache sc130_platform $root/build/project
set f [open $root/reports/cloned_files.txt w]
foreach p [get_files -all] {puts $f $p}
close $f
close_project
puts PLATFORM_CLONE_PASS
exit
