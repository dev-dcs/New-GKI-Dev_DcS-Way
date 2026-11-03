BUILT_FOR="unknown-build-it-yourself"
RUNNING="$(uname -r)"

ui_print "- wonder_mosey_wild for $BUILT_FOR"
if [ "$RUNNING" != "$BUILT_FOR" ]; then
  ui_print "! Running kernel is $RUNNING"
  ui_print "! The module will very likely be refused until you"
  ui_print "! flash the matching Wild kernel from the same run."
fi
if [ ! -d /sys/module/cfg80211 ]; then
  ui_print "! cfg80211 isn't loaded right now. It normally comes"
  ui_print "! with the Wi-Fi driver, the phy loads once it is."
fi
ui_print "- MAC: $(head -n1 "$MODPATH/mac_addr" 2>/dev/null)"
ui_print "  change it in /data/adb/modules/mosey_wonder/mac_addr"
