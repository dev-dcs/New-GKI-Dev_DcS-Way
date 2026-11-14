// SPDX-License-Identifier: GPL-2.0
/*
 * hide_shell_su.c - mask the shell -> su : process transition decision for app
 * callers, so a userspace probe (checkSELinuxAccess / reads of
 * /sys/fs/selinux/access) sees "denied" while the loaded policy keeps the rule.
 *
 * v2 - self-healing + instrumented. The SIDs for shell/su are re-resolved
 * lazily and re-cached whenever the policy generation (avd->seqno) changes, so
 * the mask no longer depends on a single resolve at policy-commit succeeding.
 * dmesg lines are tagged "hideshellsu:" so one `dmesg | grep hideshellsu`
 * (or logcat -b kernel) after a scan shows exactly what the hook saw. Once it's
 * confirmed working, drop the pr_* lines for a quiet build.
 *
 * Opt-in via CONFIG_SELINUX_HIDE_SHELL_SU_TRANS. @Dev_DcS
 */
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>
#include <linux/compiler.h>
#include "security.h"
#include "avc.h"
#include "hide_shell_su.h"

/* Android app uid floor (AID_APP_START); isolated + app_zygote sit above it. */
#define HSS_AID_APP_START 10000

/* Confirm against your policy: sesearch -A -s shell -t su -c process -p transition */
static const char hss_shell_ctx[] = "u:r:shell:s0";
static const char hss_su_ctx[]    = "u:r:su:s0";

static u32  hss_shell_sid;
static u32  hss_su_sid;
static u32  hss_seqno = (u32)-1;	/* policy generation the SIDs were cached at */
static bool hss_ready;

static bool hss_do_resolve(gfp_t gfp)
{
	u32 s = 0, t = 0;
	int rc1, rc2;

	rc1 = security_context_str_to_sid(hss_shell_ctx, &s, gfp);
	rc2 = security_context_str_to_sid(hss_su_ctx, &t, gfp);
	if (rc1 || rc2) {
		WRITE_ONCE(hss_ready, false);
		pr_warn_ratelimited("hideshellsu: resolve failed (shell rc=%d, su rc=%d)\n",
				    rc1, rc2);
		return false;
	}
	hss_shell_sid = s;
	hss_su_sid = t;
	smp_wmb();
	WRITE_ONCE(hss_ready, true);
	pr_info("hideshellsu: resolved shell_sid=%u su_sid=%u\n", s, t);
	return true;
}

/* Primer from selinux_policy_commit (sleepable); the lazy path below covers it
 * if this ever runs too early or fails. */
void ksu_hide_su_resolve_sids(void)
{
	hss_seqno = (u32)-1;
	hss_do_resolve(GFP_KERNEL);
}

static __always_inline bool hss_caller_is_app(void)
{
	return from_kuid(&init_user_ns, current_uid()) >= HSS_AID_APP_START;
}

void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
		       struct av_decision *avd, bool audit_too)
{
	bool app;

	if (tclass != SECCLASS_PROCESS)
		return;

	/* Self-healing: resolve on first use and whenever the policy changed.
	 * GFP_ATOMIC because the fast AVC path calls us under rcu_read_lock;
	 * shell/su already live in the sidtab, so this is a lookup, not an add. */
	if (!READ_ONCE(hss_ready) || avd->seqno != hss_seqno) {
		if (!hss_do_resolve(GFP_ATOMIC))
			return;
		hss_seqno = avd->seqno;
	}

	app = hss_caller_is_app();

	/* Diagnostic: every app-initiated process-transition decision, so a
	 * Duck shell->su probe shows its ssid/tsid next to what we resolved. */
	if (app && (avd->allowed & PROCESS__TRANSITION))
		pr_info_ratelimited("hideshellsu: xq ssid=%u tsid=%u want(shell=%u su=%u) uid=%u seqno=%u/%u\n",
				    ssid, tsid, hss_shell_sid, hss_su_sid,
				    from_kuid(&init_user_ns, current_uid()),
				    avd->seqno, hss_seqno);

	if (ssid != hss_shell_sid || tsid != hss_su_sid)
		return;
	if (!app)
		return;

	avd->allowed &= ~PROCESS__TRANSITION;
	if (audit_too)
		avd->auditdeny &= ~PROCESS__TRANSITION;
	pr_info_ratelimited("hideshellsu: MASKED shell->su transition for uid=%u\n",
			    from_kuid(&init_user_ns, current_uid()));
}
