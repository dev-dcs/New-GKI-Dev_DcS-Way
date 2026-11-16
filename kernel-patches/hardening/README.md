# Extra hardening (@Dev_DcS)

A thin layer on top of what the GKI defconfig already does — nothing here fights
KernelSU, SUSFS, Zygisk or Frida, and nothing touches the KMI, so stock vendor
modules still load.

Two halves:

## 1. Config side — baked into the kernel

`hardening.config` is appended onto `gki_defconfig` by **block 2b-hard** in
`build-finasu-mosey.yml` and resolved with `olddefconfig`:

- `CONFIG_ZERO_CALL_USED_REGS=y` — zero call-used registers on return (kills
  ROP-gadget reuse + register infoleaks, ~1%).
- `CONFIG_LIST_HARDENED=y` — fatal list corruption (6.6 / 6.12 only; skipped on
  6.1).
- `CONFIG_SECURITY_DMESG_RESTRICT=y` — dmesg behind `CAP_SYSLOG` from boot.

All codegen/instrumentation only — no exported symbol, signature or
module-visible struct changes. If a symbol isn't available on a given tree,
`olddefconfig` just drops it; the build doesn't fail.

`0001-proc-hide-tracerpid.patch` is applied by **block 2b-ptrace**. It adds
`kernel.hide_tracerpid` (default 0, `CAP_SYS_ADMIN` to flip): set it to 1 and
`/proc/<pid>/status` reports `TracerPid: 0`, so anti-debug / anti-Frida checks
can't see a ptrace injection (Zygisk, Frida, gdb). Flip it back to 0 to see
tracers yourself. Also KMI-safe — file-scope static + a `register_sysctl()` in a
`late_initcall`, `task_state()` stays static.

## 2. Runtime side — applied by a root module

Secure sysctl defaults that don't belong in the image (they're policy, not
kernel ABI). Ship them with any KSU/Magisk module — e.g. **DaddyUSB** — and run
the script from `post-fs-data.sh`:

```sh
sh /data/adb/modules/<your_module>/apply-hardening.sh
```

- `99-devdcs-hardening.conf` — the values (symlink/TOCTOU protection, no suid
  core dumps, kexec latch, kptr/dmesg/perf restriction, BPF-JIT hardening,
  `yama.ptrace_scope=1`).
- `apply-hardening.sh` — writes each key to `/proc/sys`, skipping anything the
  kernel doesn't support. Never bootloops.

`ptrace_scope` is deliberately **1**, not 2/3 — root, Zygisk and Frida still
attach. Raising it would break injection on a phone that exists to do injection.

## Deliberately left out

`RANDSTRUCT` (breaks KMI), lockdown / `MODULE_SIG_FORCE` (breaks KSU + vendor
modules), kprobe restrictions (break KSU), `ptrace_scope ≥ 2` (breaks
Zygisk/Frida), `STACKLEAK` (GCC-plugin only — this is a clang build),
`modules_disabled` (kills late module loading). None of these fit a rooted GKI.
