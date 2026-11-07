#!/system/bin/sh
# Keybox Autopilot - network is up by late_start service; do the real fetch and
# then honour the refresh timer for as long as the device is up. @Dev_DcS
MODDIR="${0%/*}"
. "$MODDIR/scripts/common.sh"

# wait for connectivity (max ~2 min) so the first fetch doesn't fail on boot
i=0
while [ $i -lt 24 ]; do
  if ping -c1 -W2 android.googleapis.com >/dev/null 2>&1; then break; fi
  sleep 5; i=$((i+1))
done

"$MODDIR/scripts/keybox_sync.sh" boot >/dev/null 2>&1

# periodic refresh loop (only if REFRESH_HOURS > 0)
RH="$(cfg REFRESH_HOURS)"; [ -n "$RH" ] || RH=0
case "$RH" in ''|*[!0-9]*) RH=0 ;; esac
if [ "$RH" -gt 0 ]; then
  while true; do
    sleep $((RH*3600))
    "$MODDIR/scripts/keybox_sync.sh" boot >/dev/null 2>&1
  done &
fi
