#!/system/bin/sh
# Loads the little virtual "wonder" phy on phones that don't have a real one.
#
# The build drops wonder_mosey_wild.ko next to this file. We only want it on
# devices with no hardware wondertap, so bail out early if:
#   - there's no .ko (nothing to do),
#   - a "wonder" phy already exists (real hardware, leave it be), or
#   - it's already loaded.
#
# @Dev_DcS

MODDIR="${0%/*}"
KO="$MODDIR/wonder_mosey_wild.ko"
STATE_DIR="/data/adb/mosey-enabler"
LOG="$STATE_DIR/wonder.log"
mkdir -p "$STATE_DIR" 2>/dev/null

ts() { date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo now; }

[ -f "$KO" ] || exit 0

# real wondertap already up? then we're done
for n in /sys/class/ieee80211/*/name; do
    [ -f "$n" ] || continue
    if [ "$(cat "$n" 2>/dev/null)" = "wonder" ]; then
        echo "$(ts) real wonder phy is here already, skipping" >> "$LOG"
        exit 0
    fi
done

grep -q '^wonder_mosey_wild ' /proc/modules 2>/dev/null && exit 0

# cfg80211 rides in with Wi-Fi, so give it a minute to show up
i=0
while [ ! -d /sys/module/cfg80211 ] && [ "$i" -lt 60 ]; do
    sleep 1
    i=$((i + 1))
done
if [ ! -d /sys/module/cfg80211 ]; then
    echo "$(ts) cfg80211 never came up (${i}s), giving up" >> "$LOG"
    exit 0
fi

if [ -s "$MODDIR/mac_addr" ]; then
    set -- "$KO" "mac_addr=$(head -n1 "$MODDIR/mac_addr")"
else
    set -- "$KO"
fi

if insmod "$@" >> "$LOG" 2>&1; then
    echo "$(ts) wonder up on $(uname -r)" >> "$LOG"
else
    echo "$(ts) insmod failed on $(uname -r) — check dmesg | grep wonder" >> "$LOG"
fi
