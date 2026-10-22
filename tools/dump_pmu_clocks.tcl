connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
mrd -bin -file E:/workspace/AI_Projects/ZU4EV/sc130_platform/reports/pmu_clocks_4000.bin 0xffdd4000 3072
mrd -bin -file E:/workspace/AI_Projects/ZU4EV/sc130_platform/reports/pmu_clock_array.bin 0xffdd2610 83
disconnect
exit
