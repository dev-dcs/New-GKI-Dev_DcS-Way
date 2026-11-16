# New-GKI-Dev_DcS-Way

CI-built GKI kernels for Google Pixel. Push, hit **Run workflow**, come back to a
flashable zip — no local toolchain, nothing runs on your machine.

By **@Dev_DcS**.

## What it does

GitHub Actions workflows that sync the GKI manifest, apply the WildKernels patch
set + SUSFS + the root manager you pick, bake in the extras below, compile with
Bazel/Kleaf and hand you an AnyKernel3 zip — plus the matching manager APK and
modules.

## Goals

- One-click kernel builds, zero local setup.
- Every root manager I use, with SUSFS, in one repo.
- Custom `uname` (e.g. `Dev_DcS`).
- Privacy/security baked into the kernel, default-safe (present but off until used).

## Devices

Pixel **8 → 11** — GKI `android14-6.1`, `android15-6.6`, `android16-6.12`.

**Validated working: Pixel 9 series and Pixel 10 series.** Pixel 8 and 11 build
from the same matrix but aren't device-verified yet — flash on something you can
recover. (Pixel 8 and 11 owners: congratulations, you're the QA team now. :P)

## Workflows

- **build-finasu-mosey.yml** — daily driver: FinaSU + its native SUSFS + Mosey.
- **build-wild-all-versions-mosey-fix.yml** — any GKI family (6.1 / 6.6 / 6.12),
  any root (KernelSU-Next, KernelSU, BakaSU, ReSukiSU, KowSU, FinaSU, No Root).
- **build-kernelsu-family / build-apatch-family / build-magisk-family /
  build_folkpatch** — per-root families.
- **build-finasu-xiaomi.yml** — FinaSU for Xiaomi targets.
- **build-integrity-tools / build-supplements** — companion tools and modules.

## Baked in

- **SUSFS** full hide set — path / mount / kstat / uname / bootconfig spoof,
  kallsyms and map hiding.
- **Mosey** — AirDrop inside Quick Share (`wonder` virtual phy + feelliao's
  enabler, baked into one flashable module).
- **USB hardening** — always compiled in, both default **0**, flip needs
  `CAP_SYS_ADMIN`. Meant to be driven by the
  **[USB Manager / DaddyUSB](https://github.com/dev-dcs/DaddyUSB)** root module:
  - `/proc/sys/kernel/deny_new_usb` — refuse newly inserted USB devices (host side).
  - `/proc/sys/kernel/deny_usb_gadget` — drop the gadget pullup (peripheral side);
    charging keeps working.
- **NV write guard** — opt-in LSM that protects `nv_protected.bin` (RF cal / IMEI)
  from torn writes.
- **Security patch level** — `monthly` (default), `latest-cve`, or `none`.
- **Custom uname** — set a build name like `Dev_DcS`.

## Output

GitHub wraps artifacts in an outer `.zip` — unzip once, then flash the inner zip
**as-is**. (A zip inside a zip, yes. No, I can't make GitHub stop — we've all
quietly made peace with it.)

- `*-AnyKernel3.zip` — the kernel (recovery / AnyKernel3).
- `*-Mosey-enabler.zip` — Quick Share AirDrop module.
- `*-Manager` — the root manager APK for the root you built.
- `*-NoMount` — NoMount module, version-matched to the kernel.

## Integrity note

The kernel does its part (SUSFS hiding), but flashing this alone won't pass Play
Integrity — that still needs userspace (a TrickyStore keybox + PlayIntegrityFix /
Next).

Weird one, flagged on purpose: out of nowhere, and I honestly don't know why, the
integrity **check shown inside apps like the Play Store** reads as fully failing —
if you're lucky it scrapes BASIC, mostly nothing at all. Yet everything that
actually *depends* on integrity works perfectly, no issues, exactly as if this
were a stock ROM on a locked bootloader — banking, wallet, DRM, the lot. So the
verdict the app prints and the behaviour you get don't line up. Not a bug I plan
to hunt down and fix; just know it's there.

---

## Credits

Nothing here is a new invention, and nothing's stolen either — it's just me
bolting extensions onto stuff that was already good, to make it a little better.
All giants; I just borrowed the shoulders. :')

So — none of this exists without the people below, and each is here for a reason.

**Base & packaging**
- **Google / AOSP** — the GKI `common` kernel everything is built from.
- **[WildKernels](https://github.com/WildKernels)** — the GKI patch set and build
  action the workflows are based on, plus the AnyKernel3 fork used to package.
- **[osm0sis](https://github.com/osm0sis/AnyKernel3)** — AnyKernel3, the
  flashable-zip framework.
- **[ssut](https://github.com/ssut/payload-dumper-go)** — payload-dumper-go, for
  pulling stock boot images.

**SUSFS**
- **[simonpunk](https://gitlab.com/simonpunk/susfs4ksu)** — SUSFS (susfs4ksu), the
  kernel-side hiding framework the whole hide set rides on.
- **[ShirkNeko](https://github.com/ShirkNeko/susfs4ksu)** — the susfs4ksu mirror /
  GKI branches the builds track.

**Root managers (the families this repo builds)**
- **[tiann](https://github.com/tiann/KernelSU)** — KernelSU, the original.
- **[KernelSU-Next](https://github.com/KernelSU-Next/KernelSU-Next)** +
  **[pershoot](https://github.com/pershoot/KernelSU-Next)** — KernelSU-Next and its
  native-SUSFS (`dev-susfs`) branch.
- **[backslashxx](https://github.com/backslashxx/KernelSU)**,
  **[rsuntk](https://github.com/rsuntk/KernelSU)**,
  **[KOWX712](https://github.com/KOWX712/KernelSU)** — the KernelSU forks behind the
  other variants (BakaSU base, rsuntk's fork, KowSU).
- **[Baka-SU](https://github.com/Baka-SU/BakaSU)** — BakaSU.
- **[LyraVoid](https://github.com/LyraVoid/FinaSU)** — FinaSU (the daily-driver root,
  native SUSFS) and the KernelPatch / KernelPatch-Aster forks behind FolkPatch.
- **[ReSukiSU](https://github.com/ReSukiSU/ReSukiSU)** — ReSukiSU.
- **[SukiSU-Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra)** — evaluated; not
  shipped (it won't link against current susfs4ksu — swapped for ReSukiSU).
- **[HyperSU-Team](https://github.com/HyperSU-Team/HyperSU)** — HyperSU.
- **[bmax121](https://github.com/bmax121/KernelPatch)** — KernelPatch / APatch, the
  APatch family.
- **[topjohnwu](https://github.com/topjohnwu/Magisk)** — Magisk, the Magisk family.

**Embedded modules / KPMs**
- **feelliao** — the Mosey enabler (AirDrop-in-Quick-Share userspace: `mosey_server`,
  SELinux policy, WebUI, watchdog). See `mosey/enabler/NOTICE-wonder.md`.
- **[thelok1s](https://github.com/thelok1s/mosey-extended)** — mosey-extended, the
  Mosey driver hook.
- **[maxsteeel](https://github.com/maxsteeel/nomount)** — NoMount module.
- **[vc-teahouse](https://github.com/vc-teahouse/Baseband-guard)** — Baseband-guard LSM.
- **[klcok](https://github.com/klcok/kpm-hide-env)** (hide-env),
  **[byBOOK10086](https://github.com/byBOOK10086/XEChide)** (XEChide),
  **[Admirepowered](https://github.com/Admirepowered/selinux_hook)** (selinux_hook) —
  the KPM hide modules the APatch / FolkPatch families use.

**USB / NV hardening**
- **[GrapheneOS](https://grapheneos.org)** — the `deny_new_usb` concept the host-side
  USB gate is modelled on (GPL-2.0).
- The `deny_usb_gadget` switch, the NV write guard, the kernel glue and all the
  workflows — **@Dev_DcS**.
