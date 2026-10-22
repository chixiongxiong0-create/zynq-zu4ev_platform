#!/bin/sh
# USB setup errors must never gate camera/DP startup.
export USB_VID=0x1d6b USB_PID=0x0102 USB_SERIAL=SC130-PLATFORM-001
[ ! -r /etc/sc130-platform.conf ] || . /etc/sc130-platform.conf
insmod /opt/sc130/sc130_capture.ko || echo 'capture module already loaded or probe failed; inspect dmesg'
soft=
if sh /opt/sc130/setup_uvc.sh prepare; then
 for path in /sys/class/udc/*; do
  [ -d "$path" ] || continue
  udc=${path##*/}
  if [ -w "$path/soft_connect" ]; then
   printf 'disconnect\n' > "$path/soft_connect"
   if printf '%s\n' "$udc" > /sys/kernel/config/usb_gadget/sc130_platform/UDC; then
    printf 'disconnect\n' > "$path/soft_connect"
    soft=$path/soft_connect
   fi
  fi
  break
 done
fi
/opt/sc130/tune-irq.sh &
# No USB presence, STREAMON, or DP connector state is a prerequisite here.
if [ -n "$soft" ]; then
 exec /usr/bin/sc130_platform_bridge --soft-connect "$soft"
else
 exec /usr/bin/sc130_platform_bridge
fi
