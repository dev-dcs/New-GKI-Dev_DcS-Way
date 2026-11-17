#!/bin/sh
# Give the two guard LSMs (Baseband-guard, nv_guard) a seat on 6.12+.
#
# Since 6.12 the LSM core hands out one static-call slot per LSM and sizes
# the pool at compile time: include/linux/lsm_count.h counts the *in-tree*
# LSMs that are enabled and that number becomes MAX_LSM_COUNT. An LSM that
# isn't on that list gets no slot - the core prints
#     "out of LSM static calls!?"
# and skips it, and whatever comes after it in CONFIG_LSM (bpf, on GKI) is
# dropped too. So the guards would build fine and then quietly not run.
#
# This adds both guards to the count. It only touches a compile-time array
# size inside security/security.c: no exported symbol, no struct a vendor
# module can see, so the KMI is unaffected.
#
# Older trees (6.1 / 6.6) have no lsm_count.h and need nothing - the script
# says so and leaves. Safe to run twice.
#
# usage: add-lsm-slots.sh <kernel source root>
#   -- @Dev_DcS
set -eu

root=${1:-.}
hdr=$root/include/linux/lsm_count.h

if [ ! -f "$hdr" ]; then
	echo "[i] lsm-slots: no lsm_count.h here (pre-6.12 LSM core), nothing to do"
	exit 0
fi

if grep -q 'NV_WRITE_GUARD_ENABLED' "$hdr"; then
	echo "[i] lsm-slots: guards already counted"
	exit 0
fi

tmp=$hdr.dcs.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

awk '
function guard(cfg, name) {
	print "#if IS_ENABLED(" cfg ")"
	print "#define " name " 1,"
	print "#else"
	print "#define " name
	print "#endif"
	print ""
}
/^#define MAX_LSM_COUNT/ && !defs {
	guard("CONFIG_BBG", "BBG_ENABLED")
	guard("CONFIG_NV_WRITE_GUARD", "NV_WRITE_GUARD_ENABLED")
	defs = 1
	inmacro = 1
}
# last entry of the list is the only line that closes the paren
inmacro && /_ENABLED\)[ \t]*$/ {
	sub(/\)[ \t]*$/, "\t\\")
	print
	print "\t\tBBG_ENABLED\t\t\\"
	print "\t\tNV_WRITE_GUARD_ENABLED)"
	inmacro = 0
	done = 1
	next
}
{ print }
END { exit !(defs && done) }
' "$hdr" > "$tmp" || {
	echo "lsm-slots: lsm_count.h has a layout this script does not know" >&2
	exit 1
}

cat "$tmp" > "$hdr"
echo "[+] lsm-slots: Baseband-guard and nv_guard added to MAX_LSM_COUNT"
