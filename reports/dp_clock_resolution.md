# DP clock and vblank resolution

2026-10-21. Source audit plus runtime observations supplied by the root hardware agent; this subagent did not access hardware.

The VPLL rate-change warning was genuine PMU firmware ownership protection. PMU counts selected generic-clock parents even if the clock has no active Linux consumer. FSBL originally selected VPLL for DP video, DP audio, DP STC and TOPSW_MAIN. Linux's clock summary omitted TOPSW_MAIN, so counting only its visible VPLL children missed a real consumer. Runtime PMU object inspection confirmed TOPSW_MAIN as the remaining second child after audio/STC were moved. VPLL_TO_LPD is a cross-domain clock and does not increment this firmware counter.

The FSBL now selects RPLL_TO_FPD for DP audio and STC, VPLL only for DP video, and DPLL for TOPSW_MAIN, before PMU configuration captures parent ownership. TOPSW_MAIN uses DPLL/3 = **400MHz**, deliberately reduced from VPLL/3 = approximately500MHz. This is a20% reduction in the main FPD switch clock, chosen to avoid changing the DDR PLL rate or coupling the switch to the CPU's APLL/DVFS. It remains below the533MHz maximum cited by the root boot agent. This change must not be described as preserving500MHz; sustained combined DP+USB120 throughput is the practical acceptance check.

The root hardware agent reports the third RAM boot has no VPLL warnings, DP video reference187314960Hz for requested187315000Hz, approximately119.966fps capture with zero invalid frames, and normal DP sequence progression.

The separate double-incremented DP event sequence was software accounting: atomic_begin called drm_crtc_vblank_on on each framebuffer update, which resets the core timestamp and increments the software counter, before the physical IRQ increments it again. Kernel patch0003 limits restoration to active modesets while retaining its placement before atomic_flush, preserving initial flip-event ordering. The regression harness at `linux/tests/test_vblank_restore.py` covers ordinary flips, activation/resume, active mode/connector changes and disable; red and green transcripts are alongside this report.

The optional USB sequence marker is disabled by default and affects only the USB output copy. It supports raw host-frame ordering checks without changing capture/DP data. Full-frame host MD5 processing is not a reliable120fps transport benchmark when CPU-bound; use a null sink or extract only the small marker before analysis.
