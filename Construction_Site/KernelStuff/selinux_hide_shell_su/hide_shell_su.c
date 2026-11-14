// SPDX-License-Identifier: GPL-2.0
/*
 * hide_shell_su.c - hide KernelSU's SELinux footprint from app probes.
 *
 * Two independently-toggled features, both app-scoped (uid >= 10000) so the
 * system, adb shell and root are never affected:
 *
 *   CONFIG_SELINUX_HIDE_SHELL_SU_TRANS  (v2, proven)
 *     Masks the shell -> su : process transition decision, so
 *     checkSELinuxAccess / reads of /sys/fs/selinux/access see "denied".
 *
 *   CONFIG_SELINUX_HIDE_ROOT_CONTEXTS   (v3, new - riskier, test it)
 *     Makes the KSU/root context strings look INVALID to app probes by
 *     returning -EINVAL from security_context_str_to_sid(). That single
 *     chokepoint feeds the /context validity oracle, /proc/self/attr/current
 *     writes, and the /access edge queries, so it hides all three
 *     "materialized context" surfaces at once.
 *
 * dmesg is tagged "hideshellsu:". Drop the pr_* lines for a quiet build once
 * you've confirmed it. @Dev_DcS
 */
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/string.h>
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

static __always_inline bool hss_caller_is_app(void)
{
	return from_kuid(&init_user_ns, current_uid()) >= HSS_AID_APP_START;
}

/* ------------------------------------------------------------------ */
/*  shell -> su : process transition mask                             */
/* ------------------------------------------------------------------ */

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

	/* Resolve through the lower, UNHOOKED helper so our own lookup of
	 * u:r:su:s0 is never swallowed by ksu_hide_sepol_context() below, which
	 * now forces EINVAL on :r:su: for app callers. The lazy path runs in app
	 * context, so without this bypass the mask would fail to prime itself. */
	rc1 = security_context_to_sid(hss_shell_ctx, strlen(hss_shell_ctx), &s, gfp);
	rc2 = security_context_to_sid(hss_su_ctx, strlen(hss_su_ctx), &t, gfp);
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

	if (app && (avd->allowed & PROCESS__TRANSITION))
		pr_info_ratelimited("hideshellsu: xq ssid=%u tsid=%u want(shell=%u su=%u) uid=%u\n",
				    ssid, tsid, hss_shell_sid, hss_su_sid,
				    from_kuid(&init_user_ns, current_uid()));

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

/* ------------------------------------------------------------------ */
/*  root-context materialization hide (v3)                            */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_SELINUX_HIDE_ROOT_CONTEXTS
/*
 * TYPE fields of the contexts Duck's materialization oracles look for. The
 * match is on the ":<role>:<type>:" slice, so MLS/category suffixes don't
 * matter.
 *
 *   :r:su:     The one still leaking as "shell -> su transition: Allowed".
 *              A stock USER build has no su type, so u:r:su:s0 is an invalid
 *              context there; forcing EINVAL for app callers makes the
 *              DirtySepolicy edge query abort with INVALID_CONTEXT instead of
 *              returning Allowed - i.e. it looks exactly like stock. Real su
 *              callers (shell/root, uid < 10000) are never touched, and our
 *              own primer resolves su through the unhooked helper above.
 *   :r:ksu: / :object_r:ksu_file:
 *              Safe to hide: nothing in an app process legitimately
 *              string-resolves these (KSU talks to its driver, not selinuxfs).
 *
 * The Xposed types are left commented because LSPosed/Zygisk living *inside*
 * an app process may resolve them for real file labeling - enabling them also
 * clears the LSPosed "dirty policy" warning but TEST that LSPosed still works.
 */
static const char * const hss_hidden_types[] = {
	":r:su:",
	":r:ksu:",
	":object_r:ksu_file:",
	/* ":object_r:xposed_data:",   enable to also hide LSPosed (see note) */
	/* ":object_r:xposed_file:", */
	NULL,
};

bool ksu_hide_sepol_context(const char *scontext)
{
	const char * const *p;

	if (!scontext)
		return false;
	if (!hss_caller_is_app())		/* spare system / root / adb */
		return false;

	for (p = hss_hidden_types; *p; p++) {
		if (strstr(scontext, *p)) {
			pr_info_ratelimited("hideshellsu: hid context '%s' from uid=%u\n",
				scontext,
				from_kuid(&init_user_ns, current_uid()));
			return true;
		}
	}
	return false;
}
#endif /* CONFIG_SELINUX_HIDE_ROOT_CONTEXTS */
