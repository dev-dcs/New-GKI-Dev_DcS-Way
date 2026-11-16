#!/system/bin/sh
# @Dev_DcS - apply the runtime hardening sysctls. Call from post-fs-data.sh:
#     sh /path/to/apply-hardening.sh
# Each key is written straight to /proc/sys; an unsupported key is skipped, so
# this never bootloops or aborts a module.
DIR="$(dirname "$0")"
CONF="$DIR/99-devdcs-hardening.conf"
[ -f "$CONF" ] || exit 0

while IFS= read -r line; do
  case "$line" in ''|\#*) continue ;; esac
  key="${line%%=*}"
  val="${line#*=}"
  key="$(echo "$key" | tr -d '[:space:]')"
  val="$(echo "$val" | tr -d '[:space:]')"
  [ -n "$key" ] || continue
  path="/proc/sys/$(echo "$key" | tr '.' '/')"
  if [ -w "$path" ]; then
    echo "$val" > "$path" 2>/dev/null && echo "[+] $key = $val" || echo "[i] skip $key (write failed)"
  else
    echo "[i] skip $key (not present)"
  fi
done < "$CONF"
