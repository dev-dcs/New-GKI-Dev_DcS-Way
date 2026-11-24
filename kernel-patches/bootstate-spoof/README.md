# bootstate-spoof — bootloader lock-state concealment

`@Dev_DcS`

An unlocked phone advertises itself through the verified-boot tokens the
bootloader leaves on the kernel command line and in bootconfig. init turns them
into `ro.boot.*` properties; apps also read `/proc/cmdline` straight. SUSFS
`SPOOF_CMDLINE_OR_BOOTCONFIG` can replace that line, but only once the manager
sets it at runtime — after first-stage init has already latched the properties.

This splits the job along the only line that actually works:

**Kernel (`devdcs_bootstate.c`, `CONFIG_DEVDCS_BOOTSTATE_SPOOF=y`, default n).**
An early initcall rewrites the lock tokens in `saved_command_line` in place
before init reads it:

| token | becomes |
| --- | --- |
| `verifiedbootstate` | `green` |
| `vbmeta.device_state` | `locked` |
| `flash.locked` | `1` |
| `warranty_bit` | `0` |
| `verifiedbooterror` | *(blanked)* |
| `androidboot.selinux` | `enforcing` |

Values are only ever shortened and padded with spaces, so the line keeps its
exact length — no realloc, no field shift. This fixes `/proc/cmdline` (which
userspace `resetprop` cannot touch) and any `ro.boot.*` that the bootloader put
on the command line. It is copied into `init/` and wired into `init/Kconfig` +
`init/Makefile` by the build, the same way `nv_guard` is — no patch-context
fragility, and it does not collide with the SUSFS cmdline hooks.

**Userspace complement (`../concealment/`).** Props the bootloader delivers via
*bootconfig* (common on Pixel / A13+) are latched by init before any kernel
initcall output reaches it through that path, so they are reset from
`post-fs-data` with `resetprop`. That is what the concealment conf's `prop`
lines do; ship them off and flip them on deliberately.

**Out of scope, stated plainly.** Hardware key attestation (KeyMint / Titan M)
certifies the real lock state with an off-SoC key and cannot be spoofed from the
kernel or from `resetprop`. A remote server doing hardware-backed attestation
still sees `locked=false`. The complement for that is a valid keybox
(`integrity/keybox-autopilot`), not this.

Not compile- or boot-tested in this checkout (no kernel tree here) — build with
`CONFIG_DEVDCS_BOOTSTATE_SPOOF=y` in CI and boot-test before trusting it.
