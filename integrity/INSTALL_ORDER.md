# Integrity bundle - what to flash and in what order

This is the combination that, with a **valid keybox**, is currently the most
reliable way to reach **Certified + Device + Strong** on a FinaSU/SUSFS kernel.
Read the honest caveat at the bottom before you count on "Strong".

Flash order (reboot after the Zygisk provider, then after the rest):

1. **FinaSU kernel** (this repo's build) - SUSFS hide-set + Inline hooks + uname
   spoof are already baked in. That's the whole kernel-side job.
2. **A Zygisk provider - pick ONE:** ReZygisk or Zygisk Next. Reboot.
3. **Keystore / attestation layer - pick ONE path:**
   - **TEESimulator-RS** (`JingMatrix/TEESimulator`) - intercepts `keystore2`
     and builds the attestation chain from your keybox. This is what carries
     **Strong**. OR the pre-bundled **AlwaysStrong** (TEESimulator-RS +
     PlayIntegrityFork in one zip) if you'd rather not wire them separately.
   - **OhMyKeymint (OMK)** (`qwq233/OhMyKeymint`) - alternative keystore spoofer;
     use it *instead of* TEESimulator, not alongside.
4. **PlayIntegrityFork** (`osm0sis/PlayIntegrityFork`) - spoofs `Build` fields /
   props for DroidGuard. (Skip if you used the AlwaysStrong bundle.)
5. **Keybox Autopilot** (`@Dev_DcS`, in this repo) - point it at your keybox
   source, pick your target apps, let it keep the keybox + `target.txt` current.
6. **HMA-OSS** (`XiaoTong6666/HMA-OSS`) - hide the app list / manager / installer
   from the apps that probe for them.
7. **Duck-ToolBox** (`eltavine/Duck-ToolBox`) - diagnostics: run it to see which
   signal still leaks, then fix that one thing.

Recommended DenyList/hiding: whatever provider you chose, keep its enforce-list
**off** if you're relying on TEESimulator/TrickyStore-style spoofing (forcing it
on tends to trip Strong).

## The honest caveat (read this)

- **Certified + Device** are realistic and stable with the above.
- **Strong is gated on the keybox, not on the kernel or these modules.** Strong
  needs a *valid, non-revoked, hardware* keybox plus a security patch level from
  roughly the last year. Keyboxes you can't mint yourself; leaked ones get
  revoked, often within days. When a keybox dies, Strong drops to Device until
  you load a fresh valid one - that's why Keybox Autopilot exists, and why
  nobody can honestly promise Strong "forever".
- **Locked bootloader** with a custom kernel means relocking with your own AVB
  key via **avbroot**. Attestation then reflects a non-stock root of trust; the
  keystore layer above is what masks that. Relocking with the wrong image
  bricks - back up and understand avbroot first.
- Some banks suspend accounts that fail integrity. Google can break any of this
  with a server-side change at any time.

@Dev_DcS
