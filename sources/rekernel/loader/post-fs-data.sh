#!/system/bin/sh
# Load the Re-Kernel .ko built for this kernel. @Dev_DcS
MODDIR=${0%/*}
[ -d /sys/module/rekernel ] && exit 0
for ko in "$MODDIR"/rekernel-*.ko; do
  [ -f "$ko" ] || continue
  insmod "$ko" 2>/dev/null && break
done
