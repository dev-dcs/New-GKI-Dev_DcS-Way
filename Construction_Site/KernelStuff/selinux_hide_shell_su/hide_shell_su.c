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
#include <linux/slab.h>
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

/* Primer kept as a no-op: the string-match mask needs no cached SIDs. apply.sh
 * still injects a call to this after policy load, so keep the symbol defined. */
void ksu_hide_su_resolve_sids(void)
{
}

void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
		       struct av_decision *avd, bool audit_too)
{
	char *sctx = NULL, *tctx = NULL;
	u32 slen, tlen;

	if (tclass != SECCLASS_PROCESS)
		return;
	/* Only act when the transition is currently allowed - nothing to strip
	 * otherwise, and this gates the string lookups to the rare case. */
	if (!(avd->allowed & PROCESS__TRANSITION))
		return;
	if (!hss_caller_is_app())		/* spare system / root / adb shell */
		return;

	/*
	 * Match by CONTEXT STRING, not SID. KSU's post-boot sepolicy injection
	 * renumbers shell/su to unpredictable SIDs (93/94 at policy commit, but
	 * 3840+/4016+/4393+ at query time), so a cached ssid==shell_sid test never
	 * fired - see the "xq ... want(shell=93 su=94)" dmesg. We only reach here
	 * for an app-initiated, transition-allowing process check (rare), and
	 * security_sid_to_context() stringifies with GFP_ATOMIC internally, so it
	 * is safe on the rcu/avc fast path - the AVC audit path stringifies SIDs
	 * the same way under the same lock.
	 */
	if (security_sid_to_context(ssid, &sctx, &slen))
		return;
	if (security_sid_to_context(tsid, &tctx, &tlen))
		goto out;

	/* ":r:shell:" / ":r:su:" match the role:type slice; the user field, MLS
	 * and categories are ignored, and neither substring hits another domain
	 * (shell_data_file is :object_r:, surfaceflinger is :r:surfaceflinger:). */
	if (strstr(sctx, ":r:shell:") && strstr(tctx, ":r:su:")) {
		avd->allowed &= ~PROCESS__TRANSITION;
		if (audit_too)
			avd->auditdeny &= ~PROCESS__TRANSITION;
		pr_info_ratelimited("hideshellsu: MASKED %s -> %s for uid=%u (ssid=%u tsid=%u)\n",
				    sctx, tctx,
				    from_kuid(&init_user_ns, current_uid()),
				    ssid, tsid);
	}
out:
	kfree(sctx);
	kfree(tctx);
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
