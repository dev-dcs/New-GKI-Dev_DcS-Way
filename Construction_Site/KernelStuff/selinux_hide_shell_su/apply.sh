#!/usr/bin/env bash
#
# apply.sh - drop the shell->su SELinux hide into a GKI SELinux tree and wire
# the touch points, idempotently. Fail-closed: if a required anchor isn't found,
# exit non-zero so the build STOPS instead of shipping a kernel that claims the
# hide but doesn't carry it. Re-running is a no-op.
#
# Anchors verified against android.googlesource.com kernel/common android15-6.6
# (security_compute_av_user is void + multi-line; avc_has_perm_noaudit's cache
# miss lives in avc_perm_nonode and the fast path computes denied from the node
# before memcpy - so both avc.c paths are masked). @Dev_DcS
#
# usage: apply.sh <kernel common/ dir>   (the dir that holds security/selinux)
#
set -euo pipefail

K="${1:?usage: apply.sh <kernel common dir>}"
SRC="$(cd "$(dirname "$0")" && pwd)"
SEL="$K/security/selinux"

[ -d "$SEL" ] || { echo "::error::no $SEL - is '$K' the GKI common/ tree?"; exit 1; }

# Supported: 6.6 / 6.12 (global-state SELinux, avc_has_perm_noaudit split into
# avc_perm_nonode). 6.1 and older use the per-state API (security_*(state, ...))
# and a different avc layout, so hide_shell_su.c won't compile there - skip with
# a loud warning rather than fail the build or silently ship nothing.
if ! grep -q 'avc_perm_nonode' "$SEL/avc.c" 2>/dev/null; then
  echo "::warning::shell->su hide targets 6.6/6.12; this tree looks like 6.1/older (per-state SELinux) - SKIPPING the hide for this build. Turn hide_shell_su off for 6.1, or hand-port a per-state variant."
  exit 0
fi

install -m0644 "$SRC/hide_shell_su.c" "$SEL/hide_shell_su.c"
install -m0644 "$SRC/hide_shell_su.h" "$SEL/include/hide_shell_su.h"
echo "[+] hide_shell_su.c/.h in place"

if ! grep -q 'SELINUX_HIDE_SHELL_SU_TRANS' "$SEL/Kconfig"; then
cat >> "$SEL/Kconfig" <<'KEOF'

config SELINUX_HIDE_SHELL_SU_TRANS
	bool "Hide the shell->su process transition from app SELinux probes"
	depends on SECURITY_SELINUX
	default n
	help
	  Masks PROCESS__TRANSITION in the shell->su access decision for app
	  callers (uid >= 10000). Defeats checkSELinuxAccess-style probes without
	  removing the rule from the policy image. See
	  Construction_Site/KernelStuff/selinux_hide_shell_su (@Dev_DcS).
KEOF
echo "[+] Kconfig entry added"
fi

if ! grep -q 'hide_shell_su.o' "$SEL/Makefile"; then
  printf 'selinux-$(CONFIG_SELINUX_HIDE_SHELL_SU_TRANS) += hide_shell_su.o\n' >> "$SEL/Makefile"
  echo "[+] Makefile object added"
fi

python3 - "$SEL" <<'PYEOF'
import sys
sel = sys.argv[1]

def patch(path, fn):
    s = open(path, encoding="utf-8").read()
    if "ksu_mask_shell_su" in s:
        print("[=] %s already patched" % path); return
    out = fn(s)
    if out is None:
        print("::error::anchor missing in %s - apply by hand per INTEGRATION.md" % path)
        sys.exit(1)
    open(path, "w", encoding="utf-8").write(out)
    print("[+] patched %s" % path)

def avc(s):
    inc = '#include "avc.h"\n'
    if inc not in s:
        return None
    if '#include "hide_shell_su.h"' not in s:
        s = s.replace(inc, inc + '#include "hide_shell_su.h"\n', 1)
    # slow path (avc_perm_nonode): mask the freshly computed avd
    a1 = ('\tavc_compute_av(ssid, tsid, tclass, avd, &xp_node);\n'
          '\tdenied = requested & ~(avd->allowed);')
    if s.count(a1) != 1:
        return None
    s = s.replace(a1,
        '\tavc_compute_av(ssid, tsid, tclass, avd, &xp_node);\n'
        '\tksu_mask_shell_su(ssid, tsid, tclass, avd, true);\n'
        '\tdenied = requested & ~(avd->allowed);', 1)
    # fast path: denied was taken from the cached node before memcpy, so after
    # copying into the local avd we mask it and recompute denied from the copy.
    a2 = ('\tdenied = requested & ~node->ae.avd.allowed;\n'
          '\tmemcpy(avd, &node->ae.avd, sizeof(*avd));')
    if s.count(a2) != 1:
        return None
    s = s.replace(a2,
        a2 + '\n'
        '\tksu_mask_shell_su(ssid, tsid, tclass, avd, true);\n'
        '\tdenied = requested & ~(avd->allowed);', 1)
    return s

def services(s):
    inc = '#include "security.h"\n'
    if inc not in s:
        return None
    if '#include "hide_shell_su.h"' not in s:
        s = s.replace(inc, inc + '#include "hide_shell_su.h"\n', 1)
    # probe path: mask after the compute inside security_compute_av_user only
    i = s.find('void security_compute_av_user(u32 ssid,')
    if i < 0:
        return None
    cc = s.find('context_struct_compute_av(', i)
    if cc < 0:
        return None
    end = s.find(';', cc)
    if end < 0:
        return None
    end += 1
    s = s[:end] + '\n\tksu_mask_shell_su(ssid, tsid, tclass, avd, false);' + s[end:]
    # resolve shell/su SIDs on every policy (re)load
    pl = '\tselinux_status_update_policyload(seqno);'
    if s.count(pl) != 1:
        return None
    s = s.replace(pl, pl + '\n\tksu_hide_su_resolve_sids();', 1)
    return s

patch(sel + "/avc.c", avc)
patch(sel + "/ss/services.c", services)
PYEOF

echo "[+] shell->su hide applied to $SEL"
