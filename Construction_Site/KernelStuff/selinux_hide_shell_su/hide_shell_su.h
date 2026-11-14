/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hide_shell_su.h - app-scoped masking of the shell -> su process transition.
 * Stubs out to nothing when the feature is off, so call sites stay clean (no
 * #ifdef at the call site). @Dev_DcS
 */
#ifndef _SELINUX_HIDE_SHELL_SU_H_
#define _SELINUX_HIDE_SHELL_SU_H_

#include <linux/types.h>

struct av_decision;

#ifdef CONFIG_SELINUX_HIDE_SHELL_SU_TRANS

void ksu_hide_su_resolve_sids(void);
void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
		       struct av_decision *avd, bool audit_too);

#else

static inline void ksu_hide_su_resolve_sids(void) { }
static inline void ksu_mask_shell_su(u32 ssid, u32 tsid, u16 tclass,
				     struct av_decision *avd, bool audit_too) { }

#endif /* CONFIG_SELINUX_HIDE_SHELL_SU_TRANS */

#endif /* _SELINUX_HIDE_SHELL_SU_H_ */
