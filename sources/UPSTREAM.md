# Vendored kernel sources

Everything under `sources/` is a pinned copy of an upstream tree that gets
**compiled into the kernel image**. The FinaSU workflow
(`.github/workflows/build-finasu-mosey.yml`) uses these copies first and only
falls back to cloning upstream if a directory here is missing. Vendoring them
means the build is reproducible, survives upstream force-pushes / deletions,
and always matches the tree that actually boots on the device.

Userspace/packaging bits are **not** vendored on purpose: the FinaSU manager
APK, NoMount and AnyKernel3 are still fetched live, because the kernel-side
manager-signature trust is derived from whatever manager release is current
(see `scripts/apk_sig.py`) and freezing them would break that freshness.

| Path | Upstream | Ref pinned | Notes |
|------|----------|-----------|-------|
| `finasu/kernel/` | https://github.com/LyraVoid/FinaSU | `2d5786b9129a254a87ac358c8bec0203ccd53351` (master, 2026-11-02) | Becomes `drivers/kernelsu` (symlink). The tree that currently boots. |
| `susfs4ksu/kernel_patches/` | https://github.com/ShirkNeko/susfs4ksu (branch `gki-android15-6.6`) | `4a4cb1be85a4694372d3efb9420de69982eac29a` | The exact commit FinaSU pins (`docs/builtin-integration.md` / `scripts/check-susfs-upstream.sh`). Already at the pin, so the workflow skips the git-checkout step when this is present. |
| `wild_patches/` | https://github.com/WildKernels/kernel_patches | `c78291aeb0dca025f21e6b197e0aa4fde42e065d` | Only `common/` + `gki_ptrace.patch` are kept - the perf / NTSync / BBR / unicode / ptrace patch set the build actually applies. The 30 MB of unused `oneplus/`, `ccache/`, `wild/` etc. are dropped. |
| `baseband-guard/` | https://github.com/vc-teahouse/Baseband-guard | `028ff7fd509e2104596da7cc1ef8e6ce2a0c9082` | Its own `setup.sh` does the wiring. A throwaway `git init` (no remote) is created at build time so `setup.sh` takes its no-clone / no-fetch path and the pin is preserved. |
| `rekernel/` | https://github.com/Sakion-Team/Re-Kernel | `ac08296174d7fb2801c0eee1084f067a34f8a0fe` | **Not compiled into the kernel.** Built out-of-tree as a `.ko` (like Mosey) only when `enable_rekernel=true`. `LKM-Source/` is the module, `loader/` is the flashable wrapper. Opt-in and non-fatal - can't affect the kernel image. |

`kernel-patches/nv_guard/` (one level up, outside `sources/`) is the original
custom NV write-guard LSM and is likewise vendored - it was never fetched.

## Refreshing a pin

To move a pin forward, replace the directory with a fresh checkout at the new
ref and update the table above. For susfs, keep it at whatever commit FinaSU's
`scripts/check-susfs-upstream.sh` expects (`EXPECTED_COMMIT`) or the kernel can
double-hook and fail to boot.

@Dev_DcS
