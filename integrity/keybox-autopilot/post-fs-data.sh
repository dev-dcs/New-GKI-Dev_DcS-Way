#!/system/bin/sh
# Keybox Autopilot - early apply so the keybox/targets are in place before most
# apps start. Runs again (with network) from service.sh. @Dev_DcS
MODDIR="${0%/*}"
# post-fs-data has no network yet; just make sure a staged keybox lands and the
# target list is written. The real fetch happens in service.sh.
"$MODDIR/scripts/keybox_sync.sh" boot >/dev/null 2>&1 &
