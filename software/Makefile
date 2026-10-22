CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror
LDFLAGS ?=
OUT ?= build
.PHONY: all test clean
all: $(OUT)/sc130_platform_bridge $(OUT)/dp_smoke
$(OUT):
	mkdir -p $@
$(OUT)/sc130_platform_bridge: platform_bridge.c uvc_core.c uvc_core.h dual_core.h dp_display.c dp_display.h capture_broker.h | $(OUT)
	$(CC) $(CFLAGS) platform_bridge.c uvc_core.c dp_display.c $(LDFLAGS) -o $@
$(OUT)/dp_smoke: dp_smoke.c dp_display.c dp_display.h | $(OUT)
	$(CC) $(CFLAGS) dp_smoke.c dp_display.c $(LDFLAGS) -o $@
$(OUT)/test_broker: tests/test_broker.c capture_broker.h | $(OUT)
	$(CC) $(CFLAGS) $< -o $@
$(OUT)/dp_ownership_test: tests/dp_ownership_test.c dp_display.c dp_display.h | $(OUT)
	$(CC) $(CFLAGS) $< -o $@
$(OUT)/test_platform_events: tests/test_platform_events.c platform_bridge.c uvc_core.c dp_display.c | $(OUT)
	$(CC) $(CFLAGS) tests/test_platform_events.c uvc_core.c dp_display.c -o $@
$(OUT)/test_snapshot: tests/test_snapshot.c platform_bridge.c uvc_core.c dp_display.c | $(OUT)
	$(CC) $(CFLAGS) tests/test_snapshot.c uvc_core.c dp_display.c -o $@
$(OUT)/test_usb_marker: tests/test_usb_marker.c platform_bridge.c uvc_core.c dp_display.c | $(OUT)
	$(CC) $(CFLAGS) tests/test_usb_marker.c uvc_core.c dp_display.c -o $@
test: $(OUT)/test_broker $(OUT)/dp_ownership_test $(OUT)/test_platform_events $(OUT)/test_snapshot $(OUT)/test_usb_marker
	$(OUT)/test_broker
	$(OUT)/dp_ownership_test
	$(OUT)/test_platform_events
	$(OUT)/test_snapshot
	$(OUT)/test_usb_marker
clean:
	rm -f $(OUT)/sc130_platform_bridge $(OUT)/dp_smoke $(OUT)/test_broker $(OUT)/dp_ownership_test



