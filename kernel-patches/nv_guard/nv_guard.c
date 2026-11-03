// SPDX-License-Identifier: GPL-2.0
/*
 * nv_guard - keep the modem's irreplaceable NV blob from being clobbered.
 *
 * Background: on these Tensor/Exynos "Shannon" radios the per-stack working
 * state (nv_normal.bin) and the RF calibration + IMEI data (nv_protected.bin)
 * both live on the efs/vendor partition. nv_normal regenerates on its own if
 * it's ever wiped; nv_protected does not - lose it and the radio is dead. A
 * torn write to that blob during a bootloop is exactly how an efs ends up
 * bricked.
 *
 * So this guard does one narrow thing: deny write-open / unlink / rename of
 * the protected blob (default nv_protected.bin) unless someone deliberately
 * unlocks it. nv_normal.bin is left alone on purpose, so the modem can still
 * heal itself. It's a VFS-path guard, so it covers the usual case where the
 * efs is a mounted filesystem; it does not see a modem that DMAs straight to
 * the raw partition (Baseband-guard covers the block node for that).
 *
 * Off unless CONFIG_NV_WRITE_GUARD=y and 'nv_guard' is in the active LSM list.
 * Knobs: /sys/module/nv_guard/parameters/{enforce,unlocked,names}.
 *
 * @Dev_DcS
 */
#include <linux/init.h>
#include <linux/lsm_hooks.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/sched.h>

static int nvg_enforce = 1;
static bool nvg_unlocked;
static char *nvg_names = "nv_protected.bin";

module_param_named(enforce, nvg_enforce, int, 0644);
MODULE_PARM_DESC(enforce, "1 = guard the protected NV blob, 0 = stand down");
module_param_named(unlocked, nvg_unlocked, bool, 0644);
MODULE_PARM_DESC(unlocked, "1 = temporarily allow writes (for a real recalibration)");
module_param_named(names, nvg_names, charp, 0644);
MODULE_PARM_DESC(names, "comma-separated basenames to protect (default nv_protected.bin)");

/* cheap because these files are opened rarely; no allocation on the path */
static bool nvg_match(const char *base)
{
	char buf[256];
	char *p, *tok;

	if (!base || !nvg_names || !*nvg_names)
		return false;

	strscpy(buf, nvg_names, sizeof(buf));
	p = buf;
	while ((tok = strsep(&p, ",")) != NULL) {
		if (*tok && !strcmp(tok, base))
			return true;
	}
	return false;
}

static inline bool nvg_passive(void)
{
	return !nvg_enforce || nvg_unlocked;
}

static int nvg_file_open(struct file *file)
{
	const char *name;

	if (nvg_passive())
		return 0;
	if (!(file->f_mode & FMODE_WRITE))
		return 0;

	name = file->f_path.dentry->d_name.name;
	if (nvg_match(name)) {
		pr_warn_ratelimited("nv_guard: blocked write-open of '%s' by %s(%d)\n",
				    name, current->comm, current->pid);
		return -EPERM;
	}
	return 0;
}

static int nvg_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	if (nvg_passive())
		return 0;
	if (nvg_match(dentry->d_name.name)) {
		pr_warn_ratelimited("nv_guard: blocked unlink of '%s' by %s(%d)\n",
				    dentry->d_name.name, current->comm, current->pid);
		return -EPERM;
	}
	return 0;
}

static int nvg_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry)
{
	if (nvg_passive())
		return 0;
	if (nvg_match(old_dentry->d_name.name) ||
	    nvg_match(new_dentry->d_name.name)) {
		pr_warn_ratelimited("nv_guard: blocked rename touching protected NV by %s(%d)\n",
				    current->comm, current->pid);
		return -EPERM;
	}
	return 0;
}

static struct security_hook_list nvg_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(file_open, nvg_file_open),
	LSM_HOOK_INIT(inode_unlink, nvg_inode_unlink),
	LSM_HOOK_INIT(inode_rename, nvg_inode_rename),
};

static int __init nvg_init(void)
{
	security_add_hooks(nvg_hooks, ARRAY_SIZE(nvg_hooks), "nv_guard");
	pr_info("nv_guard: watching [%s] (enforce=%d unlocked=%d)\n",
		nvg_names, nvg_enforce, nvg_unlocked);
	return 0;
}

DEFINE_LSM(nv_guard) = {
	.name = "nv_guard",
	.init = nvg_init,
};
