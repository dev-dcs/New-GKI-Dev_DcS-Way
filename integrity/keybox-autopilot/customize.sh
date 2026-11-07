SKIPUNZIP=0

# Keybox Autopilot - install-time setup. @Dev_DcS
ui_print ""
ui_print "  Keybox Autopilot  @Dev_DcS"
ui_print "  ---------------------------"

DATA=/data/adb/keybox_autopilot
mkdir -p "$DATA"

# Seed the editable config once; never clobber the user's settings on update.
if [ ! -f "$DATA/config" ]; then
  cp -f "$MODPATH/config.default" "$DATA/config"
  ui_print "- wrote first-run config to $DATA/config"
else
  ui_print "- kept your existing config at $DATA/config"
fi

# Seed the recommended target list if there isn't one yet.
if [ ! -f "$DATA/targets.custom" ]; then
  : > "$DATA/targets.custom"
fi

if [ ! -d /data/adb/tricky_store ]; then
  ui_print "! TrickyStore not found (/data/adb/tricky_store)."
  ui_print "  Install TrickyStore (or AlwaysStrong / TEESimulator) first,"
  ui_print "  then reboot - Autopilot will populate it on boot."
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh"            0 0 0755
set_perm "$MODPATH/post-fs-data.sh"       0 0 0755
set_perm "$MODPATH/scripts/keybox_sync.sh" 0 0 0755
set_perm "$MODPATH/scripts/common.sh"     0 0 0755

ui_print "- done. Open the WebUI from your root manager to set a source."
ui_print ""
