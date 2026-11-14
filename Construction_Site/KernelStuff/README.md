# KernelStuff

Kernel-side patches / sources merged into the GKI tree at build time.

- selinux_hide_shell_su/  shell->su transition hide (option C). build-finasu-mosey.yml
  runs apply.sh against kernel_workspace/common when the hide_shell_su input is on:
  it drops hide_shell_su.c/.h in, adds the Kconfig/Makefile entries, and wires the
  two SELinux hooks (avc.c + ss/services.c) idempotently. Fail-closed: if an anchor
  drifts in your tree, the build stops instead of shipping an unprotected kernel -
  then apply by hand per INTEGRATION.md.

@Dev_DcS
