#!/usr/bin/env bash
set -euo pipefail

codename="${1:?usage: fetch_init_boot.sh <codename> <A16|A17> [out_dir]}"
gen="${2:?usage: fetch_init_boot.sh <codename> <A16|A17> [out_dir]}"
here="$(cd "$(dirname "$0")" && pwd)"
out="${3:-$here/$gen}"
mkdir -p "$out"
want_ver="${gen#A}"

ck='Cookie: devsite_wall_acked=1'
ua='Mozilla/5.0 (fetch_init_boot)'

rows() {
  curl -fsSL -H "$ck" -A "$ua" "$1" 2>/dev/null | awk -v cn="$codename" -v kind="$2" '
    BEGIN { RS = "<tr" }
    index($0, cn) {
      a = ""; u = ""
      if (match($0, /Android[ ]+[0-9]+/)) { a = substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", a) }
      if (match($0, /https:\/\/dl\.google\.com\/dl\/android\/aosp\/[^"]+\.zip/)) { u = substr($0, RSTART, RLENGTH) }
      if (u != "" && index(u, cn) && index(u, "-" kind)) print a, u
    }'
}

pick() {
  local kind="$1" line
  local all; all="$(rows "$2" "$kind" || true)"
  [ -n "$all" ] || return 1
  line="$(printf '%s\n' "$all" | awk -v v="$want_ver" '$1==v{print $2; exit}')"
  if [ -z "$line" ]; then
    line="$(printf '%s\n' "$all" | awk 'NR==1{print $2}')"
    [ -n "$line" ] && echo "[fetch] no $kind build tagged Android $want_ver for $codename; taking the current one" >&2
  fi
  [ -n "$line" ] && { echo "$line"; return 0; }
  return 1
}

url=""; kind=""
if url="$(pick ota https://developers.google.com/android/ota)"; then kind=ota
elif url="$(pick factory https://developers.google.com/android/images)"; then kind=factory
else
  echo "[fetch] no public image found for '$codename' on Google's pages" >&2
  exit 2
fi
echo "[fetch] $codename ($gen): $kind -> $url"

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
curl -fL -H "$ck" -A "$ua" "$url" -o "$tmp/img.zip"
cd "$tmp"

if [ "$kind" = ota ]; then
  unzip -oq img.zip payload.bin
  if command -v payload-dumper-go >/dev/null 2>&1; then PD=payload-dumper-go
  else
    pdurl="https://github.com/ssut/payload-dumper-go/releases/latest/download/payload-dumper-go_$(uname -s)_$(uname -m).tar.gz"
    curl -fsSL "$pdurl" -o pd.tgz && tar xzf pd.tgz
    PD=./payload-dumper-go
  fi
  "$PD" -p init_boot -o . payload.bin
else
  inner="$(unzip -Z1 img.zip | grep -E 'image-.*\.zip' | head -n1)"
  [ -n "$inner" ] && unzip -oq img.zip "$inner" && unzip -oq "$inner" init_boot.img
fi

[ -f init_boot.img ] || { echo "[fetch] could not extract init_boot.img from $url" >&2; exit 3; }
cp -f init_boot.img "$out/init_boot.img"
echo "[fetch] wrote $out/init_boot.img ($(stat -c%s "$out/init_boot.img") bytes)"
