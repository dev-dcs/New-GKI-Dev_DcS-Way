/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hide_shell_su.h - app-scoped hiding of KernelSU's SELinux footprint.
 * Stubs out to nothing when a feature is off, so call sites stay clean (no
 * #ifdef at the call site). @Dev_DcS
 */
#ifndef _SELINUX_HIDE_SHELL_SU_H_
#define _SELINUX_HIDE_SHELL_SU_H_

#include <linux/types.h>

struct av_decision;

/* shell -> su : process transition mask (CONFIG_SELINUX_HIDE_SHELL_SU_TRANS) */
#ifdef CONFIG_SELINUX_HIDE_SHELL_SU_TRANS
void ksu_hide_su_resolve_sids(void);
void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
		       struct av_decision *avd, bool audit_too);
#else
static inline void ksu_hide_su_resolve_sids(void) { }
static inline void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
				     struct av_decision *avd, bool audit_too) { }
#endif

/* root-context materialization hide (CONFIG_SELINUX_HIDE_ROOT_CONTEXTS) */
#ifdef CONFIG_SELINUX_HIDE_ROOT_CONTEXTS
bool ksu_hide_sepol_context(const char *scontext);
#else
static inline bool ksu_hide_sepol_context(const char *scontext) { return false; }
#endif

#endif /* _SELINUX_HIDE_SHELL_SU_H_ */
