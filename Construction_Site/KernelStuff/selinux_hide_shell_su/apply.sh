#!/usr/bin/env bash
#
# apply.sh - wire the SELinux hide hooks into a GKI SELinux tree, idempotently.
# Fail-closed: a missing anchor stops the build instead of shipping a kernel
# that claims the hide but doesn't carry it. Re-running is a no-op.
#
# Supports two app-scoped features, both gated by their own Kconfig:
#   CONFIG_SELINUX_HIDE_SHELL_SU_TRANS  - shell->su transition mask (v2)
#   CONFIG_SELINUX_HIDE_ROOT_CONTEXTS   - ksu/root context EINVAL hide (v3)
#
# Anchors verified against android.googlesource.com kernel/common android15-6.6
# and android16-6.12. 6.1 (per-state SELinux) is skipped with a warning.
# @Dev_DcS
#
# usage: apply.sh <kernel common/ dir>
#
set -euo pipefail

K="${1:?usage: apply.sh <kernel common dir>}"
SRC="$(cd "$(dirname "$0")" && pwd)"
SEL="$K/security/selinux"

[ -d "$SEL" ] || { echo "::error::no $SEL - is '$K' the GKI common/ tree?"; exit 1; }

if ! grep -q 'avc_perm_nonode' "$SEL/avc.c" 2>/dev/null; then
  echo "::warning::shell->su hide targets 6.6/6.12; this tree looks like 6.1/older (per-state SELinux) - SKIPPING the hide for this build."
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
	  callers (uid >= 10000). See
	  Construction_Site/KernelStuff/selinux_hide_shell_su (@Dev_DcS).

config SELINUX_HIDE_ROOT_CONTEXTS
	bool "Hide KSU/root SELinux contexts from app probes"
	depends on SELINUX_HIDE_SHELL_SU_TRANS
	default n
	help
	  Returns -EINVAL from security_context_str_to_sid() for the KSU/root
	  context strings when an app (uid >= 10000) resolves them, so the
	  /context validity oracle, /proc/self/attr/current writes, and /access
	  edge queries all see those contexts as invalid. Edit hss_hidden_types[]
	  in hide_shell_su.c to change the list. Riskier than the transition mask
	  - test that KSU and (if you enable the Xposed types) LSPosed still work.
KEOF
echo "[+] Kconfig entries added"
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
    out = fn(s)
    if out is None:
        print("::error::anchor missing in %s - apply by hand per INTEGRATION.md" % path)
        sys.exit(1)
    if out != s:
        open(path, "w", encoding="utf-8").write(out)
        print("[+] patched %s" % path)
    else:
        print("[=] %s already patched" % path)

def ensure_include(s, after):
    if '#include "hide_shell_su.h"' in s:
        return s
    if after not in s:
        return None
    return s.replace(after, after + '#include "hide_shell_su.h"\n', 1)

def avc(s):
    s = ensure_include(s, '#include "avc.h"\n')
    if s is None:
        return None
    if 'ksu_mask_shell_su' not in s:
        a1 = ('\tavc_compute_av(ssid, tsid, tclass, avd, &xp_node);\n'
              '\tdenied = requested & ~(avd->allowed);')
        if s.count(a1) != 1:
            return None
        s = s.replace(a1,
            '\tavc_compute_av(ssid, tsid, tclass, avd, &xp_node);\n'
            '\tksu_mask_shell_su(ssid, tsid, tclass, avd, true);\n'
            '\tdenied = requested & ~(avd->allowed);', 1)
        a2 = ('\tdenied = requested & ~node->ae.avd.allowed;\n'
              '\tmemcpy(avd, &node->ae.avd, sizeof(*avd));')
        if s.count(a2) != 1:
            return None
        s = s.replace(a2,
            a2 + '\n\tksu_mask_shell_su(ssid, tsid, tclass, avd, true);\n'
                 '\tdenied = requested & ~(avd->allowed);', 1)
    return s

def services(s):
    s = ensure_include(s, '#include "security.h"\n')
    if s is None:
        return None
    # shell->su: mask the /access compute + resolve SIDs on policy load
    if 'ksu_mask_shell_su' not in s:
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
        pl = '\tselinux_status_update_policyload(seqno);'
        if s.count(pl) != 1:
            return None
        s = s.replace(pl, pl + '\n\tksu_hide_su_resolve_sids();', 1)
    # context-materialization hide: EINVAL root contexts at the str->sid chokepoint
    if 'ksu_hide_sepol_context' not in s:
        a = ('int security_context_str_to_sid(const char *scontext, u32 *sid, gfp_t gfp)\n'
             '{\n'
             '\treturn security_context_to_sid(scontext, strlen(scontext),')
        if s.count(a) != 1:
            return None
        s = s.replace(a,
             'int security_context_str_to_sid(const char *scontext, u32 *sid, gfp_t gfp)\n'
             '{\n'
             '\tif (ksu_hide_sepol_context(scontext))\n'
             '\t\treturn -EINVAL;\n'
             '\treturn security_context_to_sid(scontext, strlen(scontext),', 1)
    return s

patch(sel + "/avc.c", avc)
patch(sel + "/ss/services.c", services)
PYEOF

echo "[+] SELinux hide applied to $SEL"
