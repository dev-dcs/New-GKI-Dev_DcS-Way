// SPDX-License-Identifier: GPL-2.0
/*
 * hide_shell_su.c - mask the shell -> su : process transition decision for
 * app callers, so a userspace probe (android.os.SELinux.checkSELinuxAccess /
 * reads of /sys/fs/selinux/access) sees "denied" while the loaded policy keeps
 * the allow rule. This is the kernel-side (option C) counterpart to stripping
 * the rule from plat_sepolicy.cil - OTA-durable, no partition edit, no boot
 * recompile, travels with the kernel on a GKI/SUSFS rebase.
 *
 * Rides alongside SUSFS. Opt-in via CONFIG_SELINUX_HIDE_SHELL_SU_TRANS.
 *
 * Scope: only app callers (uid >= AID_APP_START, which also covers isolated
 * processes and app_zygote children) see the masked decision. adb shell, the
 * system, and anything < 10000 keep the real answer, so a legit scripted
 * shell->su is untouched.
 *
 * Correctness notes:
 *   - SIDs are resolved once per policy load (from selinux_policy_commit),
 *     never in the AV hot path; the fast path is two u32 compares + a uid read.
 *   - The masks only ever touch the CALLER'S on-stack av_decision, never the
 *     shared AVC node, so the cache is not polluted and non-app callers that
 *     hit the same cached node still get allow.
 *
 * @Dev_DcS
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>
#include <linux/compiler.h>
#include "security.h"
#include "avc.h"
#include "hide_shell_su.h"

/*
 * Android app uid floor (AID_APP_START). Isolated apps (99000+) and app_zygote
 * children sit above this too, so one comparison covers them all.
 */
#define HSS_AID_APP_START 10000

/*
 * Domain contexts whose edge we hide. Confirm against your policy with
 *   sesearch -A -s shell -t su -c process -p transition
 * and adjust the strings if crDroid labels su differently.
 */
static const char hss_shell_ctx[] = "u:r:shell:s0";
static const char hss_su_ctx[]    = "u:r:su:s0";

static u32  hss_shell_sid __read_mostly;
static u32  hss_su_sid    __read_mostly;
static bool hss_ready     __read_mostly;

/*
 * Resolve the two domain SIDs once the (new) policy is live. Called from
 * selinux_policy_commit(), i.e. outside the AV hot path and after the policy
 * is readable - never under the AVC rcu read side.
 */
void ksu_hide_su_resolve_sids(void)
{
	u32 s = 0, t = 0;

	WRITE_ONCE(hss_ready, false);

	if (security_context_str_to_sid(hss_shell_ctx, &s, GFP_KERNEL))
		return;
	if (security_context_str_to_sid(hss_su_ctx, &t, GFP_KERNEL))
		return;

	hss_shell_sid = s;
	hss_su_sid = t;
	smp_wmb();			/* publish SIDs before the ready flag */
	WRITE_ONCE(hss_ready, true);
}

static __always_inline bool hss_caller_is_app(void)
{
	uid_t uid = from_kuid(&init_user_ns, current_uid());

	return uid >= HSS_AID_APP_START;
}

/*
 * If (ssid, tsid, tclass) is exactly shell -> su : process and the caller is an
 * app, clear PROCESS__TRANSITION from the caller's av_decision. With audit_too
 * set we also clear it from auditdeny, so the resulting denial is not written
 * to the audit log (a quiet, dontaudit-style deny).
 *
 * avd is always the caller's on-stack copy:
 *   - avc_has_perm_noaudit() memcpy's the cached node into it, then we trim it;
 *   - security_compute_av_user() fills a fresh one for the /access query.
 * So this never writes through to the shared AVC node.
 */
void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
		       struct av_decision *avd, bool audit_too)
{
	if (tclass != SECCLASS_PROCESS)
		return;
	if (!READ_ONCE(hss_ready))
		return;
	if (ssid != hss_shell_sid || tsid != hss_su_sid)
		return;
	if (!hss_caller_is_app())
		return;

	avd->allowed &= ~PROCESS__TRANSITION;
	if (audit_too)
		avd->auditdeny &= ~PROCESS__TRANSITION;
}
