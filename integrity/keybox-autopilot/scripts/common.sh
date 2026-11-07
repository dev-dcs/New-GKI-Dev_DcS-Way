#!/system/bin/sh
# Keybox Autopilot - shared helpers. @Dev_DcS
DATA=/data/adb/keybox_autopilot
CFG="$DATA/config"
LOG="$DATA/autopilot.log"
RECOMMENDED="com.android.vending
com.google.android.gms
com.google.android.apps.walletnfcrel
com.google.android.apps.nbu.paisa.user"

log() {
  # keep the log small - last ~200 lines
  echo "$(date '+%m-%d %H:%M:%S')  $*" >> "$LOG" 2>/dev/null
  if [ -f "$LOG" ]; then
    tail -n 200 "$LOG" > "$LOG.t" 2>/dev/null && mv "$LOG.t" "$LOG" 2>/dev/null
  fi
}

cfg() {
  # cfg KEY -> value (empty if unset). Reads KEY="..." or KEY=... lines.
  local k="$1" v=""
  [ -f "$CFG" ] || return 0
  v="$(grep -E "^${k}=" "$CFG" 2>/dev/null | tail -n1 | cut -d= -f2-)"
  # strip surrounding quotes/spaces
  v="${v%\"}"; v="${v#\"}"; v="${v# }"
  echo "$v"
}

set_cfg() {
  # set_cfg KEY VALUE  (persists to $CFG)
  local k="$1" val="$2"
  mkdir -p "$DATA"
  [ -f "$CFG" ] || : > "$CFG"
  if grep -qE "^${k}=" "$CFG" 2>/dev/null; then
    # replace line (use a temp file; sed -i exists on toybox but be safe)
    grep -vE "^${k}=" "$CFG" > "$CFG.t" 2>/dev/null
    echo "${k}=\"${val}\"" >> "$CFG.t"
    mv "$CFG.t" "$CFG"
  else
    echo "${k}=\"${val}\"" >> "$CFG"
  fi
}

# download URL -> FILE, trying curl then busybox wget. 0 on success.
fetch() {
  local url="$1" out="$2"
  [ -n "$url" ] || return 1
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --connect-timeout 15 -o "$out" "$url" && return 0
  fi
  if command -v wget >/dev/null 2>&1; then
    wget -q -T 15 -O "$out" "$url" && return 0
  fi
  if command -v busybox >/dev/null 2>&1; then
    busybox wget -q -T 15 -O "$out" "$url" && return 0
  fi
  return 1
}
