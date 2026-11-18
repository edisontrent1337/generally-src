#!/usr/bin/env python3
"""Embed a PE image's sections, import table and TLS directory as C data (port/gen/c/image.c).
usage: gen_image.py <exe> <out.c>"""
import struct, sys

def main():
    exe, out = sys.argv[1], sys.argv[2]
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    entry = struct.unpack_from("<I", d, opt + 16)[0]
    base = struct.unpack_from("<I", d, opt + 28)[0]
    size_img = struct.unpack_from("<I", d, opt + 56)[0]
    size_hdr = struct.unpack_from("<I", d, opt + 60)[0]
    dirs = opt + 96
    imp_rva = struct.unpack_from("<I", d, dirs + 8)[0]
    tls_rva = struct.unpack_from("<I", d, dirs + 9 * 8)[0]
    secs = []
    so = opt + optsz
    for i in range(nsec):
        name, vs, va, rs, ro = struct.unpack_from("<8sIIII", d, so + 40 * i)
        secs.append((name.rstrip(b"\0").decode(), va, vs, ro, rs))

    def r2o(rva):
        for (_, va, vs, ro, rs) in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        raise ValueError(hex(rva))

    def cstr(off):
        return d[off:d.index(b"\0", off)].decode()

    imports = []
    p = r2o(imp_rva)
    while True:
        ilt, _, _, nm, iat = struct.unpack_from("<IIIII", d, p)
        if nm == 0:
            break
        dll = cstr(r2o(nm))
        t = r2o(ilt or iat)
        slot = iat
        while True:
            e = struct.unpack_from("<I", d, t)[0]
            if not e:
                break
            name = "#%d" % (e & 0xFFFF) if e & 0x80000000 else cstr(r2o(e) + 2)
            imports.append((base + slot, dll, name))
            t += 4
            slot += 4
        p += 20

    tls = (0, 0, 0, 0, 0)
    if tls_rva:
        tls = struct.unpack_from("<IIIII", d, r2o(tls_rva))  # start, end, index, callbacks, zerofill

    o = ['#include "image.h"']
    blobs = [("hdr", 0, d[:size_hdr])]
    for (name, va, vs, ro, rs) in secs:
        blobs.append((name.strip(".") or "sec", va, d[ro:ro + min(rs, vs if vs else rs)]))
    for i, (name, va, data) in enumerate(blobs):
        o.append("static const unsigned char blob%d[%d] = {" % (i, max(len(data), 1)))
        for k in range(0, len(data), 32):
            o.append(",".join(str(b) for b in data[k:k + 32]) + ",")
        o.append("};")
    o.append("const ImgSection img_sections[] = {")
    for i, (name, va, data) in enumerate(blobs):
        o.append('  {"%s", 0x%xu, %d, blob%d},' % (name, base + va, len(data), i))
    o.append("};")
    o.append("const unsigned img_nsections = %d;" % len(blobs))
    o.append("const ImgImport img_imports[] = {")
    for (slot, dll, name) in imports:
        o.append('  {0x%xu, "%s", "%s"},' % (slot, dll, name))
    o.append("};")
    o.append("const unsigned img_nimports = %d;" % len(imports))
    o.append("const uint32_t img_base = 0x%xu, img_size = 0x%xu, img_entry = 0x%xu;" % (base, size_img, base + entry))
    o.append("const uint32_t img_tls[5] = {0x%xu, 0x%xu, 0x%xu, 0x%xu, 0x%xu};" % tls)
    open(out, "w").write("\n".join(o) + "\n")
    print("sections", [(n, hex(v), len(x)) for n, v, x in blobs], "imports", len(imports), "tls", [hex(x) for x in tls])

if __name__ == "__main__":
    main()
