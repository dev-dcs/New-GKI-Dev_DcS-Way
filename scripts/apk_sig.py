#!/usr/bin/env python3
# apk_sig.py <apk> -> prints "0xSIZE SHA256" of the v2/v3 signer cert,
# the exact pair KernelSU/FinaSU bake as KSU_EXPECTED_SIZE / KSU_EXPECTED_HASH.
# Lets the build trust whatever manager the current release actually ships,
# so it never goes stale across FinaSU versions.
#
# @Dev_DcS
import sys, struct, hashlib

def main(path):
    d = open(path, "rb").read()
    eocd = d.rfind(b"PK\x05\x06")
    if eocd < 0:
        return 1
    cd_off = struct.unpack("<I", d[eocd + 16:eocd + 20])[0]
    mpos = d.rfind(b"APK Sig Block 42", 0, cd_off)
    if mpos < 0:
        return 1
    blk = struct.unpack("<Q", d[mpos - 8:mpos])[0]
    p = cd_off - blk - 8 + 8
    end = mpos - 8
    u32 = lambda b, o: struct.unpack("<I", b[o:o + 4])[0]
    while p < end:
        plen = struct.unpack("<Q", d[p:p + 8])[0]
        p += 8
        pid = u32(d, p)
        val = d[p + 4:p + 4 + plen - 4]
        p += plen - 4
        if pid in (0x7109871a, 0xf05368c0):          # v2 / v3 scheme
            o = 4                                     # skip signers-seq len
            slen = u32(val, o); o += 4
            signer = val[o:o + slen]
            so = 0
            sdlen = u32(signer, so); so += 4
            sd = signer[so:so + sdlen]               # signed data
            do = 0
            dlen = u32(sd, do); do += 4 + dlen        # skip digests seq
            do += 4                                   # skip certs seq len
            clen = u32(sd, do); do += 4
            cert = sd[do:do + clen]
            print("0x%04x %s" % (len(cert), hashlib.sha256(cert).hexdigest()))
            return 0
    return 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]) if len(sys.argv) > 1 else 2)
