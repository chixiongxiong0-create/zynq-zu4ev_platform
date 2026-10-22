connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
mrd -bin -file E:/workspace/AI_Projects/ZU4EV/sc130_platform/reports/pmu_state.bin 0xffdd0000 16384
disconnect
exit
