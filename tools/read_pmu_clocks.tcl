connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "PSU"}
puts "VPLL_COUNT [mrd 0xffdd6cf0]"
puts "DP_VIDEO_PARENT [mrd 0xffdd5324]"
puts "DP_AUDIO_PARENT [mrd 0xffdd5300]"
puts "DP_STC_PARENT [mrd 0xffdd52dc]"
disconnect
exit
