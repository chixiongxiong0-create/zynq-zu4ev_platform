#!/bin/sh
# RAM-only root. Console remains available while platform starts in background.
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /run /tmp /sys/kernel/config
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp
mount -t configfs configfs /sys/kernel/config
/opt/sc130/start-platform.sh >/tmp/sc130-platform.log 2>&1 &
echo 'SC130 DP+USB120 tone platform: startup log /tmp/sc130-platform.log'
while true; do setsid cttyhack /bin/sh; done
