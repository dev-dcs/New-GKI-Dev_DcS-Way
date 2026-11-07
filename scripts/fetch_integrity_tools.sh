#!/usr/bin/env bash
# Fetch the latest build of each integrity tool and bundle them. @Dev_DcS
#
#   fetch_integrity_tools.sh <out_dir> [repo_root]
#
# Needs: gh (authenticated via GH_TOKEN), zip, sha256sum.
# Pulls the NEWEST release of each repo - including pre-releases / canaries -
# so you get the freshest CI build, not just the last "stable" tag.
set -uo pipefail

OUT="${1:-$PWD/Integrity_Tools}"
REPO_ROOT="${2:-${GITHUB_WORKSPACE:-$PWD}}"
rm -rf "$OUT"; mkdir -p "$OUT"
MAN="$OUT/MANIFEST.txt"; : > "$MAN"

# name | owner/repo | space-separated asset globs, tried in order
TOOLS="
Duck-ToolBox|eltavine/Duck-ToolBox|*.zip
OhMyKeymint-OMK|qwq233/OhMyKeymint|*.zip
PlayIntegrityFork|osm0sis/PlayIntegrityFork|*.zip
TEESimulator-RS|JingMatrix/TEESimulator|*Release*.zip *.zip
HMA-OSS|XiaoTong6666/HMA-OSS|*.zip *.apk
"

printf 'Integrity Tools bundle - newest build of each, pulled %s UTC\n'  "$(date -u '+%Y-%m-%d %H:%M')" >> "$MAN"
printf '%s\n' "----------------------------------------------------------------" >> "$MAN"

echo "$TOOLS" | while IFS='|' read -r name repo globs; do
  [ -n "${name// /}" ] || continue
  echo "::group::$name ($repo)"
  dir="$OUT/$name"; mkdir -p "$dir"

  tag="$(gh release list --repo "$repo" -L 1 --json tagName -q '.[0].tagName' 2>/dev/null || true)"
  if [ -z "$tag" ]; then
    echo "::warning::$name: no releases found on $repo"
    echo "$name  $repo  (no release found)" >> "$MAN"
    echo "::endgroup::"; continue
  fi

  got=""
  for g in $globs; do
    if gh release download "$tag" --repo "$repo" --pattern "$g" --dir "$dir" 2>/dev/null; then got=1; break; fi
  done
  [ -n "$got" ] || gh release download "$tag" --repo "$repo" --dir "$dir" 2>/dev/null || true

  # if both Debug and Release landed, keep Release
  if ls "$dir"/*Release*.zip >/dev/null 2>&1; then rm -f "$dir"/*Debug*.zip 2>/dev/null || true; fi

  if ! ls "$dir"/* >/dev/null 2>&1; then
    echo "::warning::$name: release $tag had no matching asset"
    echo "$name  $repo  $tag  (no downloadable asset)" >> "$MAN"
  else
    for f in "$dir"/*; do
      [ -f "$f" ] || continue
      echo "$name  $repo  $tag  $(basename "$f")  sha256:$(sha256sum "$f" | cut -d' ' -f1)" >> "$MAN"
    done
  fi
  echo "::endgroup::"
done

# in-repo Keybox Autopilot (my module) - zip it as a flashable module
KB="$REPO_ROOT/integrity/keybox-autopilot"
if [ -d "$KB" ]; then
  ( cd "$KB" && zip -rqX "$OUT/Keybox-Autopilot-DevDcS.zip" . -x 'err' )
  echo "Keybox-Autopilot  (in-repo, @Dev_DcS)  local  Keybox-Autopilot-DevDcS.zip  sha256:$(sha256sum "$OUT/Keybox-Autopilot-DevDcS.zip" | cut -d' ' -f1)" >> "$MAN"
fi

cp -f "$REPO_ROOT/integrity/INSTALL_ORDER.md" "$OUT/INSTALL_ORDER.md" 2>/dev/null || true

echo "----- MANIFEST -----"; cat "$MAN"
