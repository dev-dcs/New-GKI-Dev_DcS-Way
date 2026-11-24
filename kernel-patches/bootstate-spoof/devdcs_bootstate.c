// SPDX-License-Identifier: GPL-2.0
/*
 * DevDcS boot-state concealment.
 *
 * On an unlocked phone the bootloader leaves its lock state in the kernel
 * command line - verifiedbootstate=orange, vbmeta.device_state=unlocked,
 * flash.locked=0 and friends. init turns those into ro.boot.* properties, and
 * any app can read /proc/cmdline directly, so both the properties and the raw
 * line advertise "unlocked". This rewrites the handful of lock tokens in
 * saved_command_line in place, in an early initcall, before init latches the
 * properties, so a software read of /proc/cmdline sees a stock locked line.
 *
 * It only ever shortens a value and pads the slack with spaces, so the command
 * line keeps its exact length and layout - no realloc, no shift, nothing a
 * later parser trips over. A word-boundary check keeps a short token (e.g.
 * verifiedbootstate) from matching inside the androidboot.* form and being
 * rewritten twice.
 *
 * Scope: this is presentation only. It does not relock anything, and hardware
 * key attestation (Titan M) is signed off-SoC and is NOT affected - that stays
 * a userspace keybox job. Lock signals the bootloader delivers through
 * bootconfig instead of the command line are handled by the companion resetprop
 * step, not here.
 *
 * Compiled only when CONFIG_DEVDCS_BOOTSTATE_SPOOF=y (default n).  -- @Dev_DcS
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

struct devdcs_token {
	const char *key;	/* matched as "<key>=" on a cmdline word boundary */
	const char *val;	/* replacement, never longer than the live value */
};

static const struct devdcs_token devdcs_tokens[] __initconst = {
	{ "androidboot.verifiedbootstate",   "green"     },
	{ "verifiedbootstate",               "green"     },
	{ "androidboot.vbmeta.device_state", "locked"    },
	{ "vbmeta.device_state",             "locked"    },
	{ "androidboot.flash.locked",        "1"         },
	{ "flash.locked",                    "1"         },
	{ "androidboot.warranty_bit",        "0"         },
	{ "warranty_bit",                    "0"         },
	{ "androidboot.verifiedbooterror",   ""          },
	{ "androidboot.selinux",             "enforcing" },
};

static void __init devdcs_rewrite(char *line, const char *key, const char *val)
{
	size_t klen = strlen(key);
	size_t nlen = strlen(val);
	char *p = line;

	while ((p = strstr(p, key))) {
		char *v, *end;
		size_t vlen;

		if ((p != line && p[-1] != ' ') || p[klen] != '=') {
			p += klen;
			continue;
		}

		v = p + klen + 1;
		for (end = v; *end && *end != ' '; end++)
			;
		vlen = end - v;

		if (nlen <= vlen) {
			memcpy(v, val, nlen);
			memset(v + nlen, ' ', vlen - nlen);
		}
		p = end;
	}
}

static int __init devdcs_bootstate_init(void)
{
	size_t i;

	if (!saved_command_line)
		return 0;

	for (i = 0; i < ARRAY_SIZE(devdcs_tokens); i++)
		devdcs_rewrite(saved_command_line,
			       devdcs_tokens[i].key, devdcs_tokens[i].val);
	return 0;
}
early_initcall(devdcs_bootstate_init);
