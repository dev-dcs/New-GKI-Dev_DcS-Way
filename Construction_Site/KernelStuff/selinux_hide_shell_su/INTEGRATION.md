# shell->su transition hide (kernel-side, option C) — integration

Masks the `shell -> su : process transition` decision for **app** callers
(uid >= 10000) so `checkSELinuxAccess` / reads of `/sys/fs/selinux/access` come
back **denied**, while the loaded policy keeps the rule. OTA-durable, no
partition edit, no boot-time CIL recompile. @Dev_DcS

**You normally don't apply this by hand** — `build-finasu-mosey.yml` runs
`apply.sh` against `kernel_workspace/common` when the `hide_shell_su` input is
on, and `apply.sh` does everything below idempotently and fail-closed. These
notes are the manual fallback.

**Verified against** android.googlesource.com `kernel/common` **android15-6.6**
and **android16-6.12** (global-state SELinux). **android14-6.1 is NOT supported**
— it uses the per-state API (`security_*(state, …)`) and the older
`avc_has_perm_noaudit` layout, so the hook won't compile there; `apply.sh`
detects that (no `avc_perm_nonode`) and skips with a warning.

Two new files:
- `hide_shell_su.c` → `security/selinux/hide_shell_su.c`
- `hide_shell_su.h` → `security/selinux/include/hide_shell_su.h`

Then the insertions below.

---

## 1) `security/selinux/Kconfig` — add the option

```kconfig
config SELINUX_HIDE_SHELL_SU_TRANS
	bool "Hide the shell->su process transition from app SELinux probes"
	depends on SECURITY_SELINUX
	default n
	help
	  Masks PROCESS__TRANSITION in the shell->su access decision when the
	  caller is an app (uid >= 10000). adb shell / system / < 10000 keep the
	  real answer. An evaluator reading the on-disk CIL still sees the allow
	  rule — strip the policy if you need it gone everywhere.
```

## 2) `security/selinux/Makefile`

```make
selinux-$(CONFIG_SELINUX_HIDE_SHELL_SU_TRANS) += hide_shell_su.o
```

## 3) `security/selinux/avc.c` — mask in-kernel enforcement (both paths)

Add the include after `#include "avc.h"`:

```c
#include "hide_shell_su.h"
```

**3a. Cache-miss path** (`avc_perm_nonode`). Find:

```c
	avc_compute_av(ssid, tsid, tclass, avd, &xp_node);
	denied = requested & ~(avd->allowed);
```

Insert the mask between the two lines:

```c
	avc_compute_av(ssid, tsid, tclass, avd, &xp_node);
	ksu_mask_shell_su(ssid, tsid, tclass, avd, true);
	denied = requested & ~(avd->allowed);
```

**3b. Cache-hit fast path** (`avc_has_perm_noaudit`). Here `denied` is taken from
the cached node *before* the copy, so after copying into the local `avd` we mask
it and recompute. Find:

```c
	denied = requested & ~node->ae.avd.allowed;
	memcpy(avd, &node->ae.avd, sizeof(*avd));
```

Append two lines:

```c
	denied = requested & ~node->ae.avd.allowed;
	memcpy(avd, &node->ae.avd, sizeof(*avd));
	ksu_mask_shell_su(ssid, tsid, tclass, avd, true);
	denied = requested & ~(avd->allowed);
```

`ksu_mask_shell_su(..., true)` also clears `auditdeny`, so the masked deny is not
logged (dontaudit). The masks only touch the caller's on-stack `avd`, never the
shared AVC node — cache-safe.

## 4) `security/selinux/ss/services.c` — mask the /access probe + resolve SIDs

Add the include after `#include "security.h"`:

```c
#include "hide_shell_su.h"
```

**4a. Probe path.** Inside `void security_compute_av_user(…)`, after its compute
call (the one that ends `…, avd, NULL);`, before the `out:` label:

```c
	context_struct_compute_av(policydb, scontext, tcontext, tclass, avd,
				  NULL);
	ksu_mask_shell_su(ssid, tsid, tclass, avd, false);   /* <-- add */
 out:
```

> The cached in-kernel `security_compute_av()` is left alone on purpose — masking
> there would poison the AVC node for every uid.

**4b. Resolve SIDs on policy (re)load.** In `selinux_policy_commit()`, after:

```c
	selinux_status_update_policyload(seqno);
```

add:

```c
	ksu_hide_su_resolve_sids();   /* <-- add */
```

---

## Build it

```
CONFIG_SELINUX_HIDE_SHELL_SU_TRANS=y
```

The finasu workflow sets this via its `hide_shell_su` input and runs `apply.sh`
before "Configure Kernel", so on 6.6/6.12 it's automatic. For a standalone tree,
run `apply.sh <common/>` yourself, then enable the config.

## How it defeats the probe

- `checkSELinuxAccess` → libselinux → `/sys/fs/selinux/access` →
  **`security_compute_av_user()`**. Not AVC-cached, runs in the probing app's
  context, so 4a returns `denied` to exactly that caller — per-query, no cache
  side effects.
- In-kernel enforcement goes through **`avc_has_perm_noaudit()`** (3a/3b); we
  trim the caller's local `avd` and dontaudit, so a real app attempt is denied
  quietly and consistently.

## Caveats

- **Evaluator-based detector still wins.** Anything that reads the on-disk CIL /
  precompiled policy and runs its own `compute_av` never calls this code — only
  the strip (A/B) removes the rule everywhere.
- **Verify the labels** — confirm su's context is `u:r:su:s0`
  (`sesearch -A -s shell -t su -c process -p transition`); fix the strings in
  `hide_shell_su.c` if not.
- **6.1 unsupported** (per-state SELinux API) — `apply.sh` skips it with a
  warning; turn `hide_shell_su` off for 6.1 builds or hand-port a per-state
  variant.
- Anchors validated against the real android15-6.6 / android16-6.12 `avc.c` and
  `ss/services.c`; SUSFS doesn't touch these functions, but `apply.sh` is
  fail-closed if an anchor ever drifts.

_Signed: @Dev_DcS_
