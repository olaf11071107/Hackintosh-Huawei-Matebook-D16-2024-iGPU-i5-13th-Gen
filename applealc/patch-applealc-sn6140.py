#!/usr/bin/env python3
"""
Build an AppleALC.kext for the Synaptics/Conexant SN6140 (codec 14F1:1F87) from the official AppleALC 1.9.6 release.

AppleALC has no SN6140 profile. Its CX8070 profile (14F1:1F86, layouts 15 and 21, both Lenovo ThinkPads) matches the
SN6140's node layout (DACs 0x10/0x11, ADCs 0x13/0x14, HP 0x16, speaker 0x17, jack mic 0x19, internal mic 0x1a), so
this script retargets that profile to codec 0x1F87:

  1. the CX8070 entry in AppleALC's codec table (CodecModInfo.codec)            0x1F86 -> 0x1F87
  2. CodecID in the zlib-compressed layout15/layout21 resources                 351346566 -> 351346567
  3. the AppleHDA patch that renames ADI 0x11D4198B to the codec ID             86 1F F1 14 -> 87 1F F1 14
  4. CodecID of the CX8070 pin configurations (HDAConfigDefault) in Info.plist  351346566 -> 351346567

Variant "v3" additionally changes layout 15's pin configuration for the built-in mic (node 0x1a): connection type
digital -> unknown (as layout 21) and pin control 0x24 at init and wake. Experimental, see README.

The SN6140 reports its headphone pin 0x16 as a vendor-defined widget; AppleHDA only builds the headphone path with
NootedGreen's XE-HPFIX hook, which corrects that answer. On macOS 26 AppleHDA itself comes from SimpleHDA.

Usage: python3 patch-applealc-sn6140.py AppleALC-1.9.6-RELEASE.zip OUTDIR [--variant v1|v3]
Only the standard library is used. The offsets below are specific to the 1.9.6 RELEASE build and are verified.
"""
import argparse, base64, hashlib, io, os, plistlib, re, struct, sys, zipfile, zlib

ZIP_SHA256 = "56c763a551dfe3ee39cf9168d74042f485b133aa98714bea09b4d404e4b30426"
BIN_SHA256 = "63c9747ed1501f98ea2f34cfeb7927eaa32870954138456cd3caabd0bc86932a"
SLICE = 0x1000          # x86_64 slice offset in the fat binary (vmaddr == slice offset)
ENTRY = 0x1ACCE0        # CodecModInfo of CX8070 (file offset)
HDA_PATCH_REPLACE = 0x29FB2  # replace bytes of the 11D4198B -> codec patch (file offset), used only by CX8070
OLD_ID, NEW_ID = 351346566, 351346567


def fail(msg):
    sys.exit("error: " + msg)


def patch_binary(d):
    if struct.unpack_from("<H", d, ENTRY + 8)[0] != 0x1F86:
        fail("CX8070 codec entry not found")
    struct.pack_into("<H", d, ENTRY + 8, 0x1F87)
    la = struct.unpack_from("<Q", d, ENTRY + 48)[0]
    for i in range(2):
        fo = SLICE + la + 24 * i
        dp, dl, _mn, _mx, lay = struct.unpack_from("<QIIII", d, fo)
        x = zlib.decompress(bytes(d[SLICE + dp:SLICE + dp + dl]))
        y = x.replace(b"<integer>%d</integer>" % OLD_ID, b"<integer>%d</integer>" % NEW_ID)
        if y == x:
            fail("layout %d has no CodecID" % lay)
        z = zlib.compress(y, 9)
        if len(z) > dl:
            z = min((zlib.compress(y, l) for l in range(1, 10)), key=len)
        if len(z) > dl:
            fail("layout %d does not fit" % lay)
        d[SLICE + dp:SLICE + dp + dl] = z + b"\0" * (dl - len(z))
        struct.pack_into("<I", d, fo + 8, len(z))
    if d[HDA_PATCH_REPLACE:HDA_PATCH_REPLACE + 4] != bytes.fromhex("861ff114"):
        fail("AppleHDA codec patch not found")
    d[HDA_PATCH_REPLACE:HDA_PATCH_REPLACE + 4] = bytes.fromhex("871ff114")


def set_data(block, key, old, new):
    m = re.search(r"(<key>" + key + r"</key>\s*<data>)([^<]*)(</data>)", block)
    if not m or base64.b64decode("".join(m.group(2).split())) != old:
        fail("unexpected " + key)
    return block[:m.start(2)] + "\n\t\t\t\t\t" + base64.b64encode(new).decode() + "\n\t\t\t\t\t" + block[m.end(2):]


def patch_plist(text, variant):
    if text.count("<integer>%d</integer>" % OLD_ID) != 2:
        fail("expected two CX8070 pin configurations")
    text = text.replace("<integer>%d</integer>" % OLD_ID, "<integer>%d</integer>" % NEW_ID)
    if variant == "v3":
        p = plistlib.loads(text.encode())
        cfgs = p["IOKitPersonalities"]["as.vit9696.AppleALC"]["HDAConfigDefault"]
        e = [c for c in cfgs if c.get("CodecID") == NEW_ID and c["LayoutID"] == 15][0]
        cfg, wake = e["ConfigData"], e["WakeConfigData"]
        i = text.index("MacPeet - Conexant CX8070 (CX11880) for Lenovo ThinkPad E590")
        s, j = text.rfind("<dict>", 0, i), text.index("</dict>", i)
        blk = set_data(text[s:j], "ConfigData", cfg, cfg.replace(bytes.fromhex("01a71ea6"), bytes.fromhex("01a71ea0")) + bytes.fromhex("01a70724"))
        blk = set_data(blk, "WakeConfigData", wake, wake + bytes.fromhex("01a70724"))
        text = text[:s] + blk + text[j:]
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("outdir")
    ap.add_argument("--variant", choices=["v1", "v3"], default="v1")
    a = ap.parse_args()
    raw = open(a.zip, "rb").read()
    if hashlib.sha256(raw).hexdigest() != ZIP_SHA256:
        fail("not the AppleALC 1.9.6 RELEASE zip")
    z = zipfile.ZipFile(io.BytesIO(raw))
    names = [n for n in z.namelist() if n.startswith("AppleALC.kext/") and not n.endswith("/")]
    for n in names:
        dst = os.path.join(a.outdir, n)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        data = z.read(n)
        if n == "AppleALC.kext/Contents/MacOS/AppleALC":
            if hashlib.sha256(data).hexdigest() != BIN_SHA256:
                fail("unexpected AppleALC binary")
            b = bytearray(data)
            patch_binary(b)
            data = bytes(b)
        elif n == "AppleALC.kext/Contents/Info.plist":
            data = patch_plist(data.decode(), a.variant).encode()
            plistlib.loads(data)
        with open(dst, "wb") as f:
            f.write(data)
        info = z.getinfo(n)
        mode = (info.external_attr >> 16) & 0o777
        if mode:
            os.chmod(dst, mode)
    out = os.path.join(a.outdir, "AppleALC.kext/Contents")
    for f in ("MacOS/AppleALC", "Info.plist"):
        print("%s  %s" % (hashlib.md5(open(os.path.join(out, f), "rb").read()).hexdigest(), f))
    print("built %s/AppleALC.kext (%s)" % (a.outdir, a.variant))


if __name__ == "__main__":
    main()
