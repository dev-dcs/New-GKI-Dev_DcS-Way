# Credit where it's due

This folder is a tweaked copy of the **Mosey enabler** — the userspace side of
Quick Share AirDrop: `mosey_server`, the bridge shim, the SELinux policy, the
Wi-Fi bits, the WebUI toggle, the watchdog, all of it.

**That part is feelliao's.** I didn't write it. The `module.prop` here lists me
as the author of *this* variant (the Pixel 10 / FinaSU packaging plus the kernel
glue below), which is the usual how-forks-get-labelled thing — not me taking
credit for feelliao's work. They're thanked in the project README and credited
right here.

What I actually added on top:

- `wonder-load.sh`, which loads the matching `wonder_mosey_wild.ko` at boot and
  quietly does nothing if the phone already has a real wonder phy (Pixel 10's
  BCM4398 does), or if it's already loaded, or if no `.ko` got bundled.
- one line in `post-fs-data.sh` to call it.
- a refreshed `module.prop` name/description/author.

The `.ko` isn't committed here — it's kernel-specific, so the build drops the
right one in next to `wonder-load.sh` when it packages the module.

Heads-up: the radio files in here (`bdwlan*`, `WCNSS_qcom_cfg.ini`, LOWI) are
Qualcomm's. They do nothing on a Pixel (Broadcom), which is fine — Pixel doesn't
need them.

Variant and kernel glue: **@Dev_DcS**. Original enabler: **feelliao**.
