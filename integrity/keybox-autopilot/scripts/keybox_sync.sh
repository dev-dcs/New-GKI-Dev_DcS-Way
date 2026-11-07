#!/system/bin/sh
# Keybox Autopilot - fetch, verify, apply. @Dev_DcS
# Usage: keybox_sync.sh [boot|now]   (both behave the same; "now" is for the WebUI)
DIR="$(dirname "$0")"
. "$DIR/common.sh"

MODE="${1:-now}"
mkdir -p "$DATA"
log "sync start ($MODE)"

SOURCE_URL="$(cfg SOURCE_URL)"
APPLY_RECOMMENDED="$(cfg APPLY_RECOMMENDED)"
CHECK_REVOCATION="$(cfg CHECK_REVOCATION)"
KEYBOX_DEST="$(cfg KEYBOX_DEST)"; [ -n "$KEYBOX_DEST" ] || KEYBOX_DEST="/data/adb/tricky_store/keybox.xml"
TARGET_DEST="$(cfg TARGET_DEST)"; [ -n "$TARGET_DEST" ] || TARGET_DEST="/data/adb/tricky_store/target.txt"

store_dir="$(dirname "$KEYBOX_DEST")"
if [ ! -d "$store_dir" ]; then
  log "backend dir $store_dir missing - is TrickyStore/TEESimulator installed? skipping keybox install"
fi

# ---- 1. keybox -------------------------------------------------------------
if [ -z "$SOURCE_URL" ]; then
  log "no SOURCE_URL set - skipping keybox fetch (set one in the WebUI)"
else
  TMP="$DATA/.incoming.xml"
  rm -f "$TMP"
  if ! fetch "$SOURCE_URL" "$TMP"; then
    log "fetch failed from $SOURCE_URL - keeping current keybox"
  else
    # structure check: must look like a keybox
    if ! grep -qi "Keybox" "$TMP" || ! grep -qi "PrivateKey\|Certificate" "$TMP"; then
      log "rejected: downloaded file is not a keybox.xml"
      rm -f "$TMP"
    else
      revoked=0
      if [ "$CHECK_REVOCATION" = "1" ] && command -v openssl >/dev/null 2>&1; then
        STATUS="$DATA/.status.json"
        if fetch "https://android.googleapis.com/attestation/status" "$STATUS"; then
          # pull each cert's serial (hex, lowercase, no leading zeros) and look
          # it up in Google's status list. Any hit -> refuse the whole keybox.
          awk '/-----BEGIN CERTIFICATE-----/{c=1} c{print} /-----END CERTIFICATE-----/{c=0; print "###"}' "$TMP" \
            | csplit -sz -f "$DATA/.c" - '/###/' '{*}' 2>/dev/null || true
          for cf in "$DATA"/.c*; do
            [ -f "$cf" ] || continue
            sed -i '/###/d' "$cf" 2>/dev/null
            ser="$(openssl x509 -in "$cf" -noout -serial 2>/dev/null | cut -d= -f2 | tr 'A-F' 'a-f' | sed 's/^0*//')"
            if [ -n "$ser" ] && grep -qi "\"$ser\"" "$STATUS"; then
              log "REVOKED serial $ser found in Google status list - refusing keybox"
              revoked=1
            fi
            rm -f "$cf"
          done
          rm -f "$STATUS"
        else
          log "could not fetch attestation status list - proceeding (unverified)"
        fi
      elif [ "$CHECK_REVOCATION" = "1" ]; then
        log "revocation check requested but openssl not present - proceeding (unverified)"
      fi

      if [ "$revoked" = "0" ]; then
        if [ -d "$store_dir" ]; then
          cp -f "$KEYBOX_DEST" "$KEYBOX_DEST.bak" 2>/dev/null
          cp -f "$TMP" "$KEYBOX_DEST"
          chmod 600 "$KEYBOX_DEST" 2>/dev/null
          log "installed new keybox -> $KEYBOX_DEST"
        else
          log "staged keybox at $TMP (backend dir missing)"
          cp -f "$TMP" "$DATA/keybox.pending.xml"
        fi
      fi
      rm -f "$TMP"
    fi
  fi
fi

# ---- 2. target list --------------------------------------------------------
if [ -d "$store_dir" ]; then
  TL="$DATA/.targets.build"; : > "$TL"
  if [ "$APPLY_RECOMMENDED" = "1" ]; then
    echo "$RECOMMENDED" >> "$TL"
  fi
  [ -f "$DATA/targets.custom" ] && cat "$DATA/targets.custom" >> "$TL"
  # clean: drop blanks/dups, keep order
  awk 'NF && !seen[$0]++' "$TL" > "$TARGET_DEST.new" 2>/dev/null
  if [ -s "$TARGET_DEST.new" ]; then
    mv "$TARGET_DEST.new" "$TARGET_DEST"
    chmod 644 "$TARGET_DEST" 2>/dev/null
    log "wrote $(wc -l < "$TARGET_DEST") targets -> $TARGET_DEST"
  fi
  rm -f "$TL" "$TARGET_DEST.new"
fi

# stamp last run for the WebUI / refresh timer
date '+%s' > "$DATA/.last_run"
log "sync done"
