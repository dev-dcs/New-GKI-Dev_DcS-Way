#!/usr/bin/env bash
set -u

manifest=${1:?usage: fetch-blobs.sh <manifest> <dest-root>}
root=${2:?usage: fetch-blobs.sh <manifest> <dest-root>}
[ -f "$manifest" ] || { echo "[blobs] manifest not found: $manifest" >&2; exit 2; }

base=${MOSEY_SHIM_BASE:-}
rc=0
placed=0

fetch() {
  if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
  else wget -q "$1" -O "$2"; fi
}

while read -r sha dest url rest; do
  case "${sha:-}" in ''|'#'*) continue ;; esac
  [ -n "${dest:-}" ] && [ -n "${url:-}" ] || { echo "[blobs] bad manifest line: $sha $dest $url" >&2; rc=1; continue; }

  raw=$url
  url=${url//'${MOSEY_SHIM_BASE}'/$base}
  if [ "$raw" != "$url" ] && [ -z "$base" ]; then
    echo "[blobs] $dest: MOSEY_SHIM_BASE is not set - point it at where you host the shim APKs (e.g. a Release on your repo)" >&2; rc=1; continue
  fi
  case "$url" in
    *'${'*) echo "[blobs] $dest: URL still has an unset variable ($url) - set it or edit the manifest" >&2; rc=1; continue ;;
  esac

  out="$root/$dest"
  mkdir -p "$(dirname "$out")"
  tmp="$out.part"
  if ! fetch "$url" "$tmp"; then
    echo "[blobs] $dest: download failed from $url" >&2; rm -f "$tmp"; rc=1; continue
  fi

  have=$(sha256sum "$tmp" | cut -d' ' -f1)
  if [ "$have" != "$sha" ]; then
    echo "[blobs] $dest: checksum mismatch (want $sha, got $have)" >&2; rm -f "$tmp"; rc=1; continue
  fi

  mv -f "$tmp" "$out"
  placed=$((placed + 1))
  echo "[blobs] $dest <- $url  (sha256 ok)"
done < "$manifest"

echo "[blobs] placed $placed file(s), rc=$rc"
exit $rc
