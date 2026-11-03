#!/system/bin/sh
MODDIR=${0%/*}
LOG="$MODDIR/wonder.log"

[ -d /sys/module/wonder_mosey_wild ] && exit 0

(
  i=0
  while [ ! -d /sys/module/cfg80211 ] && [ "$i" -lt 120 ]; do
    sleep 1
    i=$((i + 1))
  done
  if [ ! -d /sys/module/cfg80211 ]; then
    echo "cfg80211 never showed up after ${i}s, not loading" > "$LOG"
    exit 0
  fi
  KO="$MODDIR/wonder_mosey_wild.ko"
  if [ -s "$MODDIR/mac_addr" ]; then
    set -- "$KO" "mac_addr=$(head -n1 "$MODDIR/mac_addr")"
  else
    set -- "$KO"
  fi
  if insmod "$@" > "$LOG" 2>&1; then
    echo "loaded after ${i}s on $(uname -r)" >> "$LOG"
  else
    echo "insmod failed on $(uname -r), see dmesg | grep wonder" >> "$LOG"
  fi
) &
