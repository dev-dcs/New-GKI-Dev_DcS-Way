#!/system/bin/sh
# Keybox Autopilot - WebUI backend dispatcher. Called by app.js via ksu.exec.
# @Dev_DcS
DIR="$(dirname "$0")"
. "$DIR/common.sh"
ACTION="$1"; shift 2>/dev/null

j() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

case "$ACTION" in
  state)
    src="$(cfg SOURCE_URL)"; rec="$(cfg APPLY_RECOMMENDED)"; rh="$(cfg REFRESH_HOURS)"
    dest="$(cfg KEYBOX_DEST)"; [ -n "$dest" ] || dest="/data/adb/tricky_store/keybox.xml"
    last="$(cat "$DATA/.last_run" 2>/dev/null)"
    kb="no"; [ -f "$dest" ] && kb="yes"
    store="no"; [ -d "$(dirname "$dest")" ] && store="yes"
    targets="$(awk 'NF' "$DATA/targets.custom" 2>/dev/null | tr '\n' ',' | sed 's/,$//')"
    printf '{"source":"%s","recommended":"%s","refresh":"%s","last":"%s","keybox":"%s","backend":"%s","targets":"%s"}' \
      "$(j "$src")" "$(j "$rec")" "$(j "$rh")" "$(j "$last")" "$kb" "$store" "$(j "$targets")"
    ;;
  set)
    key="$1"; val="$2"
    case "$key" in SOURCE_URL|APPLY_RECOMMENDED|REFRESH_HOURS|CHECK_REVOCATION) set_cfg "$key" "$val"; echo OK ;; *) echo "ERR bad key" ;; esac
    ;;
  addtarget)
    pkg="$1"
    [ -n "$pkg" ] || { echo "ERR empty"; exit 0; }
    touch "$DATA/targets.custom"
    grep -qxF "$pkg" "$DATA/targets.custom" 2>/dev/null || echo "$pkg" >> "$DATA/targets.custom"
    echo OK
    ;;
  deltarget)
    pkg="$1"
    [ -f "$DATA/targets.custom" ] && { grep -vxF "$pkg" "$DATA/targets.custom" > "$DATA/.t" 2>/dev/null; mv "$DATA/.t" "$DATA/targets.custom"; }
    echo OK
    ;;
  sync)
    "$DIR/keybox_sync.sh" now >/dev/null 2>&1
    echo OK
    ;;
  log)
    tail -n 60 "$LOG" 2>/dev/null
    ;;
  *)
    echo "ERR unknown action"
    ;;
esac
