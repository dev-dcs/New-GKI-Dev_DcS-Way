# GKI builder — FinaSU, SUSFS, and Quick Share AirDrop

This is how I build my GKI kernels without touching a local toolchain: push it,
hit **Run workflow**, come back to a flashable zip. It covers the WildKernels
feature set, every root manager I use, SUSFS, and the Mosey add-on that brings
**AirDrop into Quick Share**.

There are two workflows:

- **`build-finasu-mosey.yml`** — my daily driver. FinaSU, done the way FinaSU
  wants it (its own SUSFS, nothing stapled on), defaulted to the Pixel 10
  series (`android15-6.6`, latest LTS). If you're on FinaSU, use this.
- **`build-wild-all-versions-mosey-fix.yml`** — the kitchen sink. Any of the
  current GKI families — **6.1, 6.6, or 6.12** — any root manager (KernelSU-Next,
  KernelSU, BakaSU, ReSukiSU, KowSU, FinaSU, or No Root), one at a time or `all`
  in one run.

Both spit out a kernel zip plus the Mosey module as build artifacts, and can cut
a Release if you ask them to.

## Using it

Drop the workflow in `.github/workflows/`, push, then go to the Actions tab and
run it. Pick your family and root, leave the rest on the defaults unless you
know you want otherwise, and wait. When it's done:

- `*-AnyKernel3.zip` is your kernel — flash it in recovery / AnyKernel3.
- `*-Mosey-enabler.zip` is the Quick Share AirDrop module with the matching
  `wonder` kernel module already inside it. Flash it in KernelSU/Magisk if you
  want Mosey. (No `mosey/enabler/` in the repo? Then you get a stripped
  `*-Mosey-wonder.zip` that only carries the phy.)

The runner does the whole thing itself — syncs the GKI manifest, pulls the
WildKernels patches, SUSFS and your root, patches and configures, compiles
(Bazel/Kleaf, which all three families use), and packages. Nothing runs
on your machine.

### The options that aren't obvious

Most toggles are self-explanatory. The handful that bite if you get them wrong:

- **`kernel_build_version`** — the GKI family, or `all` to fan out across every
  one of them at once (that's 3 families × 3 core roots = 9 jobs, so don't run
  it casually).
- **`gki_lts_version`** — pin an exact LTS like `6.6.144`, or leave it blank and
  take the newest.
- **`page_size`** — `16k` only exists for 6.1 / 6.6 / 6.12, and it has to match
  what your phone actually runs or it won't boot. When in doubt, 4k.
- **`root_variant`** — on the all-versions builder, any manager; `All` fans out
  to KernelSU-Next + KernelSU + BakaSU.
- **`mosey_mode`** — `both`, `module`, or `disabled`. More on that below.
- **`security_patches`** — `monthly` (default), `latest-cve`, or `none`. What
  these mean, and why `latest-cve` comes with a warning, is its own section
  below. If you don't have a reason to touch it, leave it on `monthly`.

## Which roots actually build

I compile-tested each root's `drivers/kernelsu` against a real SUSFS-patched 6.6
tree, wired the way the workflow wires it. Status as of the last check:

- **KernelSU** (tiann + susfs4ksu's `10_enable`) — builds.
- **KernelSU-Next** (pershoot `dev-susfs`, native SUSFS) — builds.
- **BakaSU** (native SUSFS + its compat symbols + `KALLSYMS_ALL`) — builds.
- **KowSU** (no SUSFS, SUSFS-off) — builds.
- **FinaSU** (native SUSFS) — builds.
- **ReSukiSU** (native SUSFS) — builds.

Why ReSukiSU and not SukiSU-Ultra: I ran SukiSU-Ultra first and it wouldn't
compile against current susfs4ksu — its `builtin` branch calls
`susfs_parse_priority_app_strings()` unconditionally but never defines
`CONFIG_KSU_SUSFS_PRIORITY_UID_FILTER` in its own Kconfig, so the link dies on a
missing symbol. KernelSU-Next guards the exact same call behind an `#ifdef` and
builds fine. That's an upstream thing to fix, not something the workflow can
paper over, so I swapped it out for ReSukiSU, which builds clean the way it's
wired here. Every root in the list above is compile-verified, and FinaSU has its
own dedicated workflow on top.

## Why not 5.x or 6.18

The build list is **6.1 / 6.6 / 6.12** on purpose. The old 5.10/5.15 families
are dropped — they're legacy and I don't ship them. 6.18 is left out too: there
isn't an upstream SUSFS branch for `android17-6.18` yet (SUSFS stops at 6.12),
so a 6.18 build would come out SUSFS-off and the SUSFS-dependent roots couldn't
run on it. Once SUSFS tags a 6.18 branch it can go back in; for now the three
families above are the ones that actually build with the full feature set.

## Mosey (AirDrop in Quick Share)

The kernel side is `mosey/wonder_mosey_wild.c` — a cfg80211-only virtual phy
called `wonder`. GKI ships cfg80211 as a vendor module and never builds
mac80211 into the image, so this driver leans only on the cfg80211 the phone
already has and is built out-of-tree against the kernel you just compiled, then
dropped into the module. Your kernel image stays exactly what a normal build
produces.

It answers the vendor commands `mosey_server` pokes it with (OUI `0x001A11`,
subcmds 1–5) and hands back a locally-administered MAC. There's no real radio
underneath — it's there so `mosey_server`'s bring-up finishes. Which brings me
to the Pixel 10 note.

### Pixel 10's

Pixel 10 runs the **6.6** kernel, so the FinaSU workflow defaults to
`android15-6.6` at the latest LTS. That's the number that matters here — double
check it against `uname -r` on your phone, and leave `page_size` at 4k unless
`getconf PAGE_SIZE` says 16384.

On Mosey specifically: Pixel 10's BCM4398 already does the `wondertap` dance in
hardware, so AirDrop-in-Quick-Share works off the real radio. The bundled
virtual phy knows to get out of the way — `wonder-load.sh` checks for a native
`wonder` phy and does nothing if it finds one. So on a Pixel 10 you're really
flashing this for the **kernel** (rooted, SUSFS hiding, integrity) and Mosey
just keeps running on the hardware. The virtual phy is for devices that don't
have wondertap at all.

### The bundled enabler

`mosey/enabler/` is the userspace half — `mosey_server`, the bridge shim,
SELinux policy, the Wi-Fi bits, the WebUI switch, the watchdog. **This is
feelliao's Mosey enabler**; I've only added the kernel glue (`wonder-load.sh`
and one line in its `post-fs-data.sh`) so the build can bake the matching
`wonder_mosey_wild.ko` straight in and hand you one flashable module instead of
two. See `mosey/enabler/NOTICE-wonder.md` for exactly what's feelliao's and
what's mine. A heads-up: the enabler's radio files (`bdwlan*`,
`WCNSS_qcom_cfg.ini`, LOWI) are Qualcomm's — they do nothing on Pixel's
Broadcom stack, which is fine, Pixel doesn't need them.

### If the MAC reply format is wrong

Some `mosey_server` builds want the subcmd-5 reply wrapped differently. Flip it
live, no rebuild:

```
echo 1 > /sys/module/wonder_mosey_wild/parameters/mac_reply_fmt
```

## Integrity

The FinaSU build turns on the whole SUSFS hide set — path, mount, kstat, uname
and bootconfig/cmdline spoof, open-redirect, map hiding, kallsyms hiding — and
FinaSU's dynamic manager keeps the manager itself hidden. That's the kernel
doing its part.

It is not the whole story, so don't get your hopes up that flashing this alone
passes Play Integrity. BASIC/DEVICE still needs userspace the kernel can't
carry — a TrickyStore keybox and a PlayIntegrityFix/Next module. Flash those
too, and don't go spoofing your build fingerprint to a Pixel 10 just to chase
Mosey; it'll only fight your integrity setup.

## Security patches: monthly vs latest-cve

The `security_patches` input picks how aggressive you want to be about CVE
fixes. Three settings:

- **`monthly`** (default) — the sane one. Your base is the latest tagged GKI /
  LTS release, and that tag already carries the Android monthly security
  bulletin CVE set, backported and tested *together* upstream by Google and the
  stable maintainers. Nothing extra is applied. This is what you want unless you
  have a specific reason not to.
- **`latest-cve`** — on top of the monthly base, the build fast-forwards this
  series' **post-release** stable CVE fixes (the commits that landed on
  `linux-<ver>.y` *after* the last numbered release) and applies them with
  `git am --3way`. Anything that doesn't apply cleanly is **aborted and logged,
  never force-fuzzed in**. You get `cve-out/cve-applied.txt` and
  `cve-out/cve-skipped.txt` listing exactly what went in and what didn't.
- **`none`** — apply nothing extra; build the base exactly as pinned.

> ⚠️ **Read before you pick `latest-cve`.** These are patches that haven't made
> it into a tagged release yet — they're newer and less-baked than the monthly
> set. Specifically:
>
> - They are applied best-effort. If the base tag isn't reachable in the synced
>   tree, or the stable tree can't be fetched, the step **warns and applies
>   nothing** — your build silently falls back to the monthly base. Check the
>   log and `cve-out/` to see what actually happened; don't assume.
> - A patch that conflicts is skipped, not fixed. So `latest-cve` can leave you
>   with a *partial* set, which is a weird state to reason about security-wise.
> - Backporting arbitrary stable commits onto GKI can break the **KMI/ABI**
>   (fail the ABI check, or break vendor modules), and nothing here boot-tests
>   the result. Flash it on a device you can recover.
> - It pulls from kernel.org at build time, so two builds of the same source on
>   different days can differ. Not reproducible by design.
>
> For a daily driver, stay on `monthly`. Reach for `latest-cve` only when a
> specific just-published CVE matters to you and you're ready to test the build.

## NV write guard (optional, off by default)

If you've ever had a Tensor/Exynos "Shannon" radio go dead after a bootloop,
this is for you. Two NV blobs live on the efs/vendor partition:
`nv_normal.bin` (the modem's working state — if it gets wiped the modem just
rebuilds it) and `nv_protected.bin` (RF calibration and your IMEIs — lose or
corrupt that one and the radio doesn't come back). The classic brick is a
*torn* write to `nv_protected.bin`: power cut or a reboot lands mid-write and
you're left with half a file.

`kernel-patches/nv_guard/` is a tiny LSM that does exactly one thing: it
refuses write-opens, unlinks and renames of `nv_protected.bin` unless you
explicitly unlock it. **`nv_normal.bin` is deliberately left writable** — that's
the blob the modem is *supposed* to rewrite, and the one you wipe to recover a
confused radio, so guarding it would do more harm than good. The guard only
stands between a bad actor (or a bad write) and the data you can't get back.

Turn it on with the **`nv_write_guard`** toggle when you run a build (it's a
boolean, defaults to **off**). With it off, the build is byte-for-byte what it
always was — nothing is copied or configured. With it on, the workflow copies
`kernel-patches/nv_guard/` into `security/nv_guard/`, wires it into
`security/Kconfig` and `security/Makefile` **by anchor** (not a version-specific
diff, so it's safe on every GKI series we build — 6.1, 6.6 and 6.12), sets
`CONFIG_NV_WRITE_GUARD=y`, and adds `nv_guard` to the active LSM list. If
`kernel-patches/` isn't in the repo, the step warns and skips rather than
failing the build. The bundled `.patch` is just a 6.6-cut copy for applying by
hand — the workflow uses the sources directly, not the patch, which is why it
no longer corrupts `security/Kconfig` on the older trees.

Runtime knobs live under `/sys/module/nv_guard/parameters`:

```
# allow a genuine recalibration through, then lock it back down
echo 1 > /sys/module/nv_guard/parameters/unlocked
# ... do the calibration ...
echo 0 > /sys/module/nv_guard/parameters/unlocked

# widen what's protected (comma-separated basenames)
echo 'nv_protected.bin,nv.bin' > /sys/module/nv_guard/parameters/names
```

**Be honest with yourself about what this does and doesn't catch.** It's a
VFS-path guard, so it covers the normal case where efs is a mounted filesystem
and something opens the file to write it. It does **not** see a modem that DMAs
straight to the raw partition (that's block-device territory — Baseband-guard,
already in this build, is what guards the partition node), it won't retroactively
close an fd that's already open, and it matches on filename, so a protected name
is protected wherever it lives. It's also not a substitute for a bootloop
protector — if you're bootlooping, fix that too. Think of it as a seatbelt for
the one file you can't replace, not a force field.

Status: the LSM object compiles clean against a SUSFS-patched `android15-6.6`
tree with the same clang/LLVM toolchain the workflow uses. I have **not**
flashed it to a device — treat the on-device behaviour as "should work, test it
yourself on something you can recover," and keep an `efs` backup before you flip
it on. Standard "it's your radio" caveat applies.

## Why FinaSU has its own workflow

FinaSU already has SUSFS built in (`kernel/extension/susfs.c`). The generic
WildKernels path assumes a KernelSU that doesn't, so it runs the tiann
"enable SUSFS" patch plus the `finasu_compat` fix-ups on top — and against
current FinaSU those half-apply and wreck `core/init.c` and `selinux_hide.c`
(if you ever saw `ksu_app_profile_init` undeclared, that was it). So the FinaSU
workflow skips all of that: it applies the kernel-side SUSFS (the `50_add`
patch and the `fs/`/`include/` sources), sets `CONFIG_KALLSYMS_ALL=y` because
FinaSU's Kbuild demands it, and lets FinaSU's own code do the rest.

## What's in here

```
.github/workflows/
  build-finasu-mosey.yml                 FinaSU, Pixel 10 defaults
  build-wild-all-versions-mosey-fix.yml  6.1 / 6.6 / 6.12, all roots
mosey/
  wonder_mosey_wild.c                    the cfg80211 wonder driver
  Kbuild
  ksu-module/                            minimal module (build's fallback)
  enabler/                               feelliao's enabler + my wonder-load.sh
kernel-patches/
  nv_guard/                              optional NV write guard LSM
    0001-nv_guard-...patch               ready to apply (patch -p1)
    nv_guard.c  Kconfig  Makefile        the same sources, to read by hand
```

The driver and the minimal module are kept out here so you can read them; the
workflow carries its own inline copy of the driver, so it builds on GitHub
without needing to check `mosey/` out.

## Thanks

Big one first: **feelliao** — the entire userspace Mosey enabler under
`mosey/enabler/` is their work; all I did was bake a kernel-matched `wonder`
phy into it.

And the rest of the shoulders this stands on:

- **SUSFS** — simonpunk — https://gitlab.com/simonpunk/susfs4ksu
- **WildKernels** — the patches and the pipeline this grew out of —
  https://github.com/WildKernels
- **FinaSU** — LyraVoid — https://github.com/LyraVoid/FinaSU
- **KernelSU** — tiann — https://github.com/tiann/KernelSU
- **KernelSU-Next** — https://github.com/KernelSU-Next/KernelSU-Next
- **BakaSU** — https://github.com/Baka-SU/BakaSU
- **ReSukiSU** — https://github.com/ReSukiSU/ReSukiSU (fork of SukiSU-Ultra —
  https://github.com/SukiSU-Ultra/SukiSU-Ultra)
- **Baseband-guard** — vc-teahouse — https://github.com/vc-teahouse/Baseband-guard
- **Mosey Extended** (the whole wonder / Quick Share effort) — thelok1s —
  https://github.com/thelok1s/mosey-extended
- **DroidSpaces-OSS** — ravindu644 — https://github.com/ravindu644/Droidspaces-OSS
- **NoMount** — maxsteeel — https://github.com/maxsteeel/nomount
- **AnyKernel3** — osm0sis — https://github.com/osm0sis/AnyKernel3
- **Google / AOSP** for the GKI common kernel.

Missed someone? That's on me, open an issue and I'll add you.

## License

GPL-2.0-only — the module links the kernel, so it can't be anything else.
See [LICENSE](LICENSE).

---

— **@Dev_DcS**
