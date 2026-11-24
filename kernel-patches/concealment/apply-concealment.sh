#!/system/bin/sh
# @Dev_DcS - apply the runtime concealment switches. Call from post-fs-data.sh:
#     sh /path/to/apply-concealment.sh
# Two line kinds in 99-devdcs-concealment.conf, commented out by default so
# installing this changes nothing until you opt in:
#   key.path = value        -> written to /proc/sys (same as apply-hardening)
#   prop  name  value       -> resetprop -n name value (bootconfig lock props)
# An unsupported key or a missing resetprop is skipped, never fatal, so this
# can't bootloop or abort a module.
DIR="$(dirname "$0")"
CONF="$DIR/99-devdcs-concealment.conf"
[ -f "$CONF" ] || exit 0

have_resetprop=0
command -v resetprop >/dev/null 2>&1 && have_resetprop=1

while IFS= read -r line; do
  case "$line" in ''|\#*) continue ;; esac

  # shellcheck disable=SC2086 # intentional split into: prop name value
  set -- $line
  if [ "$1" = "prop" ]; then
    name="$2"; val="$3"
    [ -n "$name" ] || continue
    if [ "$have_resetprop" = 1 ]; then
      resetprop -n "$name" "$val" 2>/dev/null && echo "[+] prop $name = $val" \
        || echo "[i] skip prop $name (resetprop failed)"
    else
      echo "[i] skip prop $name (no resetprop)"
    fi
    continue
  fi

  key="${line%%=*}"; val="${line#*=}"
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
