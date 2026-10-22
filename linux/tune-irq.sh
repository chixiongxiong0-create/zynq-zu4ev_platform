#!/bin/sh
# Discover Linux IRQ number dynamically; controller's SPI is not the IRQ number.
[ -d /sys/devices/system/cpu/cpu3 ] || exit 0
[ ! -r /sys/devices/system/cpu/cpu3/online ] || [ "$(cat /sys/devices/system/cpu/cpu3/online)" = 1 ] || exit 0
# The UVC function requests the interrupt only after its userspace endpoint
# is opened/connected. Startup must not finish this discovery too early.
attempt=0
while [ "$attempt" -lt 30 ]; do
 irqs=$(awk '/dwc3|fe200000.usb/ {gsub(":","",$1); if ($1 ~ /^[0-9]+$/) print $1}' /proc/interrupts)
 if [ -n "$irqs" ]; then
  for irq in $irqs; do
   [ ! -w "/proc/irq/$irq/smp_affinity_list" ] || printf '3\n' > "/proc/irq/$irq/smp_affinity_list"
  done
  exit 0
 fi
 attempt=$((attempt+1))
 sleep 1
done
echo 'USB IRQ not registered within startup tuning window' >&2
