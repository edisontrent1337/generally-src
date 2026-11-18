#include <stdlib.h>
// MSVCP140 imports: the iostream base classes behind the game's std::fstream use, implemented on the
// guest objects with MSVC 2015 x86 layouts (verified against the game's inlined code):
//   basic_streambuf: 0 vptr, 4 _Gfirst, 8 _Pfirst, c _IGfirst, 10 _IPfirst, 14 _Gnext, 18 _Pnext,
//                    1c _IGnext, 20 _IPnext, 24 _Gcount, 28 _Pcount, 2c _IGcount, 30 _IPcount, 34 _Plocale
//   virtual slots:   0 dtor 1 _Lock 2 _Unlock 3 overflow 4 pbackfail 5 showmanyc 6 underflow 7 uflow
//                    8 xsgetn 9 xsputn 10 seekoff 11 seekpos 12 setbuf 13 sync 14 imbue
//   ios_base/basic_ios: 0 vptr, 4 _Stdstr, 8 _Mystate, c _Except, 10 _Fmtfl, 18 _Prec(i64), 20 _Wide(i64),
//                    28 _Arr, 2c _Calls, 30 _Ploc, 38 _Mystrbuf, 3c _Tiestr, 40 _Fillch
//   locale: 8 bytes, _Locimp* at +4; _Locimp: 8 _Facetvec, c _Facetcount, 14 _Xparent
//   istream/ostream: 0 vbptr (vbtable[1] = offset of the basic_ios virtual base), istream 8 _Chcount(i64)
//   fpos<_Mbstatet>: 0 _Myoff(i64), 8 _Fpos(i64), 10 _Mystate(8)
#include "rt.h"
#include "image.h"
#include <stdio.h>
#include <string.h>

#define SHIM(n) static void n(CPU *c)
#define EOF_ (-1)

enum { GOOD = 0, EOFBIT = 1, FAILBIT = 2, BADBIT = 4 };

static uint32_t ios_of(uint32_t self) { return self + G32(G32(self) + 4); }

// ---- streambuf helpers (MSVC inline semantics)
static int32_t sb_gnavail(uint32_t sb) { return G32(G32(sb + 0x1c)) ? (int32_t)G32(G32(sb + 0x2c)) : 0; }
static int32_t sb_pnavail(uint32_t sb) { return G32(G32(sb + 0x20)) ? (int32_t)G32(G32(sb + 0x30)) : 0; }
static uint32_t sb_vfn(uint32_t sb, int slot) { return G32(G32(sb) + 4 * slot); }

static void sb_init(uint32_t sb) {
    G32(sb + 0x0c) = sb + 0x04;
    G32(sb + 0x10) = sb + 0x08;
    G32(sb + 0x1c) = sb + 0x14;
    G32(sb + 0x20) = sb + 0x18;
    G32(sb + 0x2c) = sb + 0x24;
    G32(sb + 0x30) = sb + 0x28;
    G32(sb + 0x04) = G32(sb + 0x08) = G32(sb + 0x14) = G32(sb + 0x18) = 0;
    G32(sb + 0x24) = G32(sb + 0x28) = 0;
}

static int64_t sb_xsgetn(CPU *c, uint32_t sb, uint32_t ptr, int64_t count) {
    int64_t copied = 0;
    while (count > 0) {
        int32_t n = sb_gnavail(sb);
        if (n > 0) {
            if (count < n) n = (int32_t)count;
            uint32_t g = G32(G32(sb + 0x1c));
            memcpy(GP(ptr), GP(g), (size_t)n);
            G32(G32(sb + 0x1c)) = g + (uint32_t)n;
            G32(G32(sb + 0x2c)) -= (uint32_t)n;
            ptr += (uint32_t)n; copied += n; count -= n;
        } else {
            int32_t ch = (int32_t)guest_thiscall(c, sb_vfn(sb, 7), sb, 0, NULL);  // uflow
            if (ch == EOF_) break;
            G8(ptr) = (uint8_t)ch;
            ptr++; copied++; count--;
        }
    }
    return copied;
}

static int64_t sb_xsputn(CPU *c, uint32_t sb, uint32_t ptr, int64_t count) {
    int64_t copied = 0;
    while (count > 0) {
        int32_t n = sb_pnavail(sb);
        if (n > 0) {
            if (count < n) n = (int32_t)count;
            uint32_t p = G32(G32(sb + 0x20));
            memcpy(GP(p), GP(ptr), (size_t)n);
            G32(G32(sb + 0x20)) = p + (uint32_t)n;
            G32(G32(sb + 0x30)) -= (uint32_t)n;
            ptr += (uint32_t)n; copied += n; count -= n;
        } else {
            uint32_t a = G8(ptr);
            if ((int32_t)guest_thiscall(c, sb_vfn(sb, 3), sb, 1, &a) == EOF_) break;  // overflow
            ptr++; copied++; count--;
        }
    }
    return copied;
}

// virtual dispatch through the object's own vtable (the game's filebuf overrides most slots)
static int64_t v_xsgetn(CPU *c, uint32_t sb, uint32_t ptr, int64_t n) {
    uint32_t a[3] = {ptr, (uint32_t)n, (uint32_t)((uint64_t)n >> 32)};
    guest_thiscall(c, sb_vfn(sb, 8), sb, 3, a);
    return (int64_t)((uint64_t)EAX | ((uint64_t)EDX << 32));
}
static int64_t v_xsputn(CPU *c, uint32_t sb, uint32_t ptr, int64_t n) {
    uint32_t a[3] = {ptr, (uint32_t)n, (uint32_t)((uint64_t)n >> 32)};
    guest_thiscall(c, sb_vfn(sb, 9), sb, 3, a);
    return (int64_t)((uint64_t)EAX | ((uint64_t)EDX << 32));
}
static int64_t fpos_off(uint32_t p) { return (int64_t)G64(p) + (int64_t)G64(p + 8); }
static uint32_t scratch_fpos(void) { return gcalloc(1, 32); }
static int64_t v_seekoff(CPU *c, uint32_t sb, int64_t off, int way, int mode) {
    uint32_t r = scratch_fpos();
    uint32_t a[5] = {r, (uint32_t)off, (uint32_t)((uint64_t)off >> 32), (uint32_t)way, (uint32_t)mode};
    guest_thiscall(c, sb_vfn(sb, 10), sb, 5, a);
    int64_t v = fpos_off(r);
    gfree(r);
    return v;
}
static void v_seekoff_into(CPU *c, uint32_t sb, uint32_t ret, int64_t off, int way, int mode) {
    uint32_t a[5] = {ret, (uint32_t)off, (uint32_t)((uint64_t)off >> 32), (uint32_t)way, (uint32_t)mode};
    guest_thiscall(c, sb_vfn(sb, 10), sb, 5, a);
}
static int64_t v_seekpos(CPU *c, uint32_t sb, uint32_t pos /* 24-byte fpos */, int mode) {
    uint32_t r = scratch_fpos();
    uint32_t a[8] = {r, G32(pos), G32(pos + 4), G32(pos + 8), G32(pos + 12), G32(pos + 16), G32(pos + 20), (uint32_t)mode};
    guest_thiscall(c, sb_vfn(sb, 11), sb, 8, a);
    int64_t v = fpos_off(r);
    gfree(r);
    return v;
}
static int32_t v_sync(CPU *c, uint32_t sb) { return (int32_t)guest_thiscall(c, sb_vfn(sb, 13), sb, 0, NULL); }

// ---- ios helpers
static void ios_clear(uint32_t ios, uint32_t st) {
    st &= 0x17;
    if (!G32(ios + 0x38)) st |= BADBIT;
    G32(ios + 0x08) = st;
}
static void ios_setstate(uint32_t ios, uint32_t st) { if (st) ios_clear(ios, G32(ios + 0x08) | st); }

// ---- fake locale machinery: one _Locimp and codecvt facets that report "no conversion"
static uint32_t facet_vt, locimp, ios_vt, sb_vt, id_counter = 40;

// std::locale (MSVC 2015 x86) is 8 bytes with the _Locimp pointer at +4
static uint32_t new_locale(void) {
    uint32_t loc = gcalloc(1, 8);
    G32(loc + 4) = locimp;
    return loc;
}

static void write_out(CPU *c, uint32_t os, const char *s, size_t n) {
    uint32_t ios = ios_of(os);
    if (G32(ios + 0x08) != GOOD) { ios_setstate(ios, BADBIT); return; }
    // width / fill / adjustfield
    int64_t w = (int64_t)G64(ios + 0x20);
    uint32_t fl = G32(ios + 0x10);
    char fill = (char)G8(ios + 0x40);
    size_t pad = (w > 0 && (size_t)w > n) ? (size_t)w - n : 0;
    size_t total = n + pad;
    uint32_t buf = gmalloc((uint32_t)total + 1);
    char *b = GS(buf);
    if (fl & 0x40) { memcpy(b, s, n); memset(b + n, fill, pad); }
    else { memset(b, fill, pad); memcpy(b + pad, s, n); }
    if (v_xsputn(c, G32(ios + 0x38), buf, (int64_t)total) != (int64_t)total) ios_setstate(ios, BADBIT);
    gfree(buf);
    G64(ios + 0x20) = 0;
}

static void fmt_int(CPU *c, uint32_t os, int64_t v, int is_signed) {
    uint32_t fl = G32(ios_of(os) + 0x10);
    char spec[16], out[64];
    int k = 0;
    spec[k++] = '%';
    if ((fl & 0x20) && is_signed) spec[k++] = '+';
    if (fl & 0x08) spec[k++] = '#';
    spec[k++] = 'l'; spec[k++] = 'l';
    if (fl & 0x800) spec[k++] = (fl & 0x04) ? 'X' : 'x';
    else if (fl & 0x400) spec[k++] = 'o';
    else spec[k++] = is_signed ? 'd' : 'u';
    spec[k] = 0;
    int n = is_signed ? snprintf(out, sizeof out, spec, (long long)v) : snprintf(out, sizeof out, spec, (unsigned long long)v);
    write_out(c, os, out, (size_t)n);
}

SHIM(o_put_int) { uint32_t os = ECX; fmt_int(c, os, (int32_t)ARG(0), 1); ret_std(c, os, 1); }
SHIM(o_put_uint) { uint32_t os = ECX; fmt_int(c, os, ARG(0), 0); ret_std(c, os, 1); }
SHIM(o_put_float) {
    uint32_t os = ECX;
    uint32_t ios = ios_of(os);
    uint32_t fl = G32(ios + 0x10);
    int64_t prec = (int64_t)G64(ios + 0x18);
    char spec[16], out[512];
    int k = 0;
    spec[k++] = '%';
    if (fl & 0x20) spec[k++] = '+';
    if (fl & 0x10) spec[k++] = '#';
    spec[k++] = '.'; spec[k++] = '*';
    uint32_t ff = fl & 0x3000;
    char conv = ff == 0x2000 ? 'f' : ff == 0x1000 ? 'e' : ff == 0x3000 ? 'a' : 'g';
    if (fl & 0x04) conv = (char)(conv - 32);
    spec[k++] = conv;
    spec[k] = 0;
    int n = snprintf(out, sizeof out, spec, (int)(prec > 0 || ff ? prec : 6), (double)f4(ARG(0)));
    write_out(c, os, out, (size_t)n);
    ret_std(c, os, 1);
}
SHIM(o_put_manip) {
    uint32_t os = ECX;
    uint32_t a = ios_of(os);
    guest_call(c, ARG(0), 1, &a);
    ret_std(c, os, 1);
}
SHIM(o_Osfx) {
    uint32_t ios = ios_of(ECX);
    if (G32(ios + 0x08) == GOOD && (G32(ios + 0x10) & 0x02) && G32(ios + 0x38))
        if (v_sync(c, G32(ios + 0x38)) == -1) ios_setstate(ios, BADBIT);
    ret_std(c, 0, 0);
}
SHIM(o_flush) {
    uint32_t os = ECX, ios = ios_of(os);
    if (G32(ios + 0x38) && G32(ios + 0x08) == GOOD)
        if (v_sync(c, G32(ios + 0x38)) == -1) ios_setstate(ios, BADBIT);
    ret_std(c, os, 0);
}
SHIM(o_write) {
    uint32_t os = ECX, ios = ios_of(os);
    int64_t n = (int64_t)((uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32));
    if (G32(ios + 0x08) != GOOD) ios_setstate(ios, BADBIT);
    else if (v_xsputn(c, G32(ios + 0x38), ARG(0), n) != n) ios_setstate(ios, BADBIT);
    ret_std(c, os, 3);
}
SHIM(o_seekp_off) {
    uint32_t os = ECX, ios = ios_of(os);
    int64_t off = (int64_t)((uint64_t)ARG(0) | ((uint64_t)ARG(1) << 32));
    if (!(G32(ios + 0x08) & (FAILBIT | BADBIT)) && v_seekoff(c, G32(ios + 0x38), off, (int)ARG(2), 2) == -1)
        ios_setstate(ios, FAILBIT);
    ret_std(c, os, 3);
}
SHIM(o_seekp_pos) {
    uint32_t os = ECX, ios = ios_of(os);
    uint32_t pos = ESP + 4;
    if (!(G32(ios + 0x08) & (FAILBIT | BADBIT)) && v_seekpos(c, G32(ios + 0x38), pos, 2) == -1)
        ios_setstate(ios, FAILBIT);
    ret_std(c, os, 6);
}
static void fpos_bad(uint32_t r) { G64(r) = (uint64_t)-1; G64(r + 8) = 0; G64(r + 16) = 0; }
SHIM(o_tellp) {
    uint32_t os = ECX, ios = ios_of(os), r = ARG(0);
    if (!(G32(ios + 0x08) & (FAILBIT | BADBIT))) v_seekoff_into(c, G32(ios + 0x38), r, 0, 1, 2);
    else fpos_bad(r);
    ret_std(c, r, 1);
}

SHIM(i_read) {
    uint32_t is = ECX, ios = ios_of(is);
    int64_t n = (int64_t)((uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32));
    G64(is + 8) = 0;
    if (G32(ios + 0x08) != GOOD) ios_setstate(ios, FAILBIT);
    else {
        int64_t got = v_xsgetn(c, G32(ios + 0x38), ARG(0), n);
        G64(is + 8) = (uint64_t)got;
        if (got != n) ios_setstate(ios, EOFBIT | FAILBIT);
        static int trace = -1;
        if (trace < 0) trace = getenv("GRX_TRACEIO") != NULL;
        if (trace) rt_log("read %u bytes -> %u got %d first %08x", (unsigned)n, ARG(0), (int)got, n >= 4 ? G32(ARG(0)) : G8(ARG(0)));
    }
    ret_std(c, is, 3);
}
SHIM(i_seekg_off) {
    uint32_t is = ECX, ios = ios_of(is);
    int64_t off = (int64_t)((uint64_t)ARG(0) | ((uint64_t)ARG(1) << 32));
    ios_clear(ios, G32(ios + 0x08) & ~(uint32_t)EOFBIT);
    if (!(G32(ios + 0x08) & (FAILBIT | BADBIT)) && v_seekoff(c, G32(ios + 0x38), off, (int)ARG(2), 1) == -1)
        ios_setstate(ios, FAILBIT);
    ret_std(c, is, 3);
}
SHIM(i_seekg_pos) {
    uint32_t is = ECX, ios = ios_of(is);
    uint32_t pos = ESP + 4;
    ios_clear(ios, G32(ios + 0x08) & ~(uint32_t)EOFBIT);
    if (!(G32(ios + 0x08) & (FAILBIT | BADBIT)) && v_seekpos(c, G32(ios + 0x38), pos, 1) == -1)
        ios_setstate(ios, FAILBIT);
    ret_std(c, is, 6);
}
SHIM(i_tellg) {
    uint32_t is = ECX, ios = ios_of(is), r = ARG(0);
    if (G32(ios + 0x08) != GOOD) { ios_setstate(ios, FAILBIT); fpos_bad(r); }
    else v_seekoff_into(c, G32(ios + 0x38), r, 0, 1, 1);
    ret_std(c, r, 1);
}

// ---- basic_ios / basic_iostream
SHIM(ios_ctor) {
    uint32_t ios = ECX;
    memset(GP(ios), 0, 0x48);
    G32(ios) = ios_vt;
    ret_std(c, ios, 0);
}
SHIM(ios_dtor) {
    uint32_t ios = ECX;
    if (G32(ios + 0x30)) { gfree(G32(ios + 0x30)); G32(ios + 0x30) = 0; }
    ret_std(c, 0, 0);
}
SHIM(ios_clear_m) { ios_clear(ECX, ARG(0)); ret_std(c, 0, 2); }
SHIM(ios_setstate_m) { ios_clear(ECX, G32(ECX + 0x08) | ARG(0)); ret_std(c, 0, 2); }

// basic_iostream(streambuf *sb) with the hidden "most derived" flag (the game passes 0)
SHIM(iostream_ctor) {
    uint32_t self = ECX, sb = ARG(0);
    if (ARG(1)) rt_fatal(c, "basic_iostream constructed as most-derived object (unsupported)");
    uint32_t ios = ios_of(self);
    G64(self + 8) = 0;  // _Chcount
    // basic_ios::init(sb)
    G32(ios + 0x38) = sb;
    G32(ios + 0x3c) = 0;
    G8(ios + 0x40) = ' ';
    G32(ios + 0x04) = 0;
    G32(ios + 0x0c) = 0;
    G32(ios + 0x10) = 0x201;   // skipws | dec
    G64(ios + 0x18) = 6;
    G64(ios + 0x20) = 0;
    G32(ios + 0x28) = 0;
    G32(ios + 0x2c) = 0;
    uint32_t loc = new_locale();
    G32(ios + 0x30) = loc;
    ios_clear(ios, GOOD);
    ret_std(c, self, 2);
}
SHIM(nop_this0) { ret_std(c, ECX, 0); }
SHIM(nop_this1) { ret_std(c, ECX, 1); }

// ---- basic_streambuf members
SHIM(sb_ctor) {
    uint32_t sb = ECX;
    G32(sb) = sb_vt;
    G32(sb + 0x34) = new_locale();
    sb_init(sb);
    ret_std(c, sb, 0);
}
SHIM(sb_dtor) {
    uint32_t sb = ECX;
    if (G32(sb + 0x34)) { gfree(G32(sb + 0x34)); G32(sb + 0x34) = 0; }
    ret_std(c, 0, 0);
}
SHIM(sb_Init) { sb_init(ECX); ret_std(c, 0, 0); }
SHIM(sb_getloc) {
    uint32_t sb = ECX, r = ARG(0);
    G32(r) = 0;
    G32(r + 4) = G32(G32(sb + 0x34) + 4);
    ret_std(c, r, 1);
}
SHIM(sb_imbue) { ret_std(c, 0, 1); }
SHIM(sb_sync) { ret_std(c, 0, 0); }
SHIM(sb_setbuf) { ret_std(c, ECX, 3); }
SHIM(sb_showmanyc) { EDX = 0; ret_std(c, 0, 0); }
SHIM(sb_lock) { ret_std(c, 0, 0); }
SHIM(sb_uflow) {
    uint32_t sb = ECX;
    int32_t ch = (int32_t)guest_thiscall(c, sb_vfn(sb, 6), sb, 0, NULL);  // underflow
    if (ch != EOF_) {
        uint32_t g = G32(G32(sb + 0x1c));
        ch = G8(g);
        G32(G32(sb + 0x1c)) = g + 1;
        G32(G32(sb + 0x2c)) -= 1;
    }
    ret_std(c, (uint32_t)ch, 0);
}
SHIM(sb_xsgetn_m) {
    uint32_t sb = ECX;
    int64_t n = (int64_t)((uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32));
    int64_t r = sb_xsgetn(c, sb, ARG(0), n);
    EDX = (uint32_t)((uint64_t)r >> 32);
    ret_std(c, (uint32_t)r, 3);
    EDX = (uint32_t)((uint64_t)r >> 32);
}
SHIM(sb_xsputn_m) {
    uint32_t sb = ECX;
    int64_t n = (int64_t)((uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32));
    int64_t r = sb_xsputn(c, sb, ARG(0), n);
    ret_std(c, (uint32_t)r, 3);
    EDX = (uint32_t)((uint64_t)r >> 32);
}
SHIM(sb_sputn) {
    uint32_t sb = ECX;
    int64_t n = (int64_t)((uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32));
    int64_t r = v_xsputn(c, sb, ARG(0), n);
    ret_std(c, (uint32_t)r, 3);
    EDX = (uint32_t)((uint64_t)r >> 32);
}
SHIM(sb_sputc) {
    uint32_t sb = ECX, ch = ARG(0) & 0xff;
    int32_t r;
    if (sb_pnavail(sb) > 0) {
        uint32_t p = G32(G32(sb + 0x20));
        G8(p) = (uint8_t)ch;
        G32(G32(sb + 0x20)) = p + 1;
        G32(G32(sb + 0x30)) -= 1;
        r = (int32_t)ch;
    } else r = (int32_t)guest_thiscall(c, sb_vfn(sb, 3), sb, 1, &ch);
    ret_std(c, (uint32_t)r, 1);
}
SHIM(sb_eof0) { ret_std(c, (uint32_t)EOF_, 0); }
SHIM(sb_eof1) { ret_std(c, (uint32_t)EOF_, 1); }
SHIM(sb_seek_bad5) { fpos_bad(ARG(0)); ret_std(c, ARG(0), 5); }
SHIM(sb_seek_bad8) { fpos_bad(ARG(0)); ret_std(c, ARG(0), 8); }

// ---- locale / facets
SHIM(lockit_ctor) { ret_std(c, ECX, 1); }
SHIM(lockit_dtor) { ret_std(c, 0, 0); }
SHIM(getgloballocale) { ret_c(c, locimp); }
SHIM(id_op) {
    uint32_t id = ECX;
    if (!G32(id)) G32(id) = ++id_counter;
    ret_std(c, G32(id), 0);
}
SHIM(codecvt_getcat) {
    uint32_t ppf = ARG(0);
    if (ppf && !G32(ppf)) {
        uint32_t f = gcalloc(1, 0x20);
        G32(f) = facet_vt;
        G32(f + 4) = 1;
        G32(ppf) = f;
    }
    ret_c(c, 2);
}
SHIM(always_noconv) { ret_std(c, 1, 0); }
SHIM(codecvt_unshift) { G32(ARG(3)) = ARG(1); ret_std(c, 3, 4); }
SHIM(codecvt_inout) { G32(ARG(3)) = ARG(1); G32(ARG(6)) = ARG(4); ret_std(c, 3, 7); }
SHIM(facet_dtor) { ret_std(c, ECX, 1); }
SHIM(facet_incref) { ret_std(c, 0, 0); }
SHIM(facet_decref) { ret_std(c, 0, 0); }
SHIM(facet_bad) { rt_fatal(c, "unexpected virtual call on a runtime-provided locale facet"); }
SHIM(uncaught_exception) { ret_c(c, 0); }
SHIM(x_bad_alloc) { rt_fatal(c, "std::bad_alloc"); }
SHIM(x_length_error) { rt_fatal(c, "std::length_error: %s", GS(ARG(0))); }
SHIM(x_out_of_range) { rt_fatal(c, "std::out_of_range: %s", GS(ARG(0))); }

// setw(n) returns _Smanip<streamsize>{ pfun, arg } by hidden pointer; pfun is our swfun
SHIM(setw_fn) {
    uint32_t r = ARG(0);
    G32(r) = rt_thunk_for("rt.swfun");
    G64(r + 8) = (uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32);
    ret_c(c, r);
}
SHIM(swfun) { G64(ARG(0) + 0x20) = (uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32); ret_c(c, 0); }

// _Fiopen(const char *name, ios_base::openmode mode, int prot) -> FILE*
uint32_t crt_fopen(const char *gpath, const char *mode);
SHIM(fiopen) {
    const char *name = GS(ARG(0));
    uint32_t mode = ARG(1);
    static const uint32_t valid[] = {0x01, 0x02, 0x12, 0x0a, 0x08, 0x03, 0x13, 0x0b, 0x09, 0};
    static const char *mods[] = {"r", "w", "w", "a", "a", "r+", "w+", "a+", "a+"};
    uint32_t m = mode;
    if (m & 0x40) m |= 0x01;  // _Nocreate
    if (m & 0x80) {           // _Noreplace
        uint32_t f = crt_fopen(name, "r");
        if (f) { uint32_t a = f; guest_call(c, rt_thunk_for("fclose"), 1, &a); ret_c(c, 0); return; }
    }
    uint32_t key = m & ~(uint32_t)(0x40 | 0x80 | 0x04 | 0x20);
    int i;
    for (i = 0; valid[i] && valid[i] != key; i++) {}
    if (!valid[i]) { ret_c(c, 0); return; }
    char fm[8];
    snprintf(fm, sizeof fm, "%s%s", mods[i], (mode & 0x20) ? "b" : "");
    uint32_t f = crt_fopen(name, fm);
    if (f && (mode & 0x04)) {
        uint32_t a[4] = {f, 0, 0, 2};
        if (guest_call(c, rt_thunk_for("_fseeki64"), 4, a) != 0) {
            guest_call(c, rt_thunk_for("fclose"), 1, a);
            f = 0;
        }
    }
    ret_c(c, f);
}

const ShimDef shims_msvcp[] = {
    {"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@M@Z", o_put_float},
    {"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@H@Z", o_put_int},
    {"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@I@Z", o_put_uint},
    {"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@P6AAAVios_base@1@AAV21@@Z@Z", o_put_manip},
    {"?imbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEXABVlocale@2@@Z", sb_imbue},
    {"?sync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ", sb_sync},
    {"?setbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEPAV12@PAD_J@Z", sb_setbuf},
    {"?uflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ", sb_uflow},
    {"?_Unlock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAEXXZ", sb_lock},
    {"?_Lock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAEXXZ", sb_lock},
    {"?uncaught_exception@std@@YA_NXZ", uncaught_exception},
    {"?sputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAE_JPBD_J@Z", sb_sputn},
    {"?_Osfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEXXZ", o_Osfx},
    {"?setw@std@@YA?AU?$_Smanip@_J@1@_J@Z", setw_fn},
    {"?sputc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHD@Z", sb_sputc},
    {"?_Xbad_alloc@std@@YAXXZ", x_bad_alloc},
    {"?_Xlength_error@std@@YAXPBD@Z", x_length_error},
    {"?_Xout_of_range@std@@YAXPBD@Z", x_out_of_range},
    {"??1_Lockit@std@@QAE@XZ", lockit_dtor},
    {"??0_Lockit@std@@QAE@H@Z", lockit_ctor},
    {"?_Getgloballocale@locale@std@@CAPAV_Locimp@12@XZ", getgloballocale},
    {"?_Fiopen@std@@YAPAU_iobuf@@PBDHH@Z", fiopen},
    {"?_Getcat@?$codecvt@DDU_Mbstatet@@@std@@SAIPAPBVfacet@locale@2@PBV42@@Z", codecvt_getcat},
    {"??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAE@XZ", sb_ctor},
    {"?getloc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QBE?AVlocale@2@XZ", sb_getloc},
    {"?_Init@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXXZ", sb_Init},
    {"?unshift@?$codecvt@DDU_Mbstatet@@@std@@QBEHAAU_Mbstatet@@PAD1AAPAD@Z", codecvt_unshift},
    {"?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z", ios_setstate_m},
    {"??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ", ios_ctor},
    {"??0?$basic_iostream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z", iostream_ctor},
    {"?in@?$codecvt@DDU_Mbstatet@@@std@@QBEHAAU_Mbstatet@@PBD1AAPBDPAD3AAPAD@Z", codecvt_inout},
    {"?out@?$codecvt@DDU_Mbstatet@@@std@@QBEHAAU_Mbstatet@@PBD1AAPBDPAD3AAPAD@Z", codecvt_inout},
    {"??1?$basic_ios@DU?$char_traits@D@std@@@std@@UAE@XZ", ios_dtor},
    {"?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z", ios_clear_m},
    {"??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAE@XZ", sb_dtor},
    {"?showmanyc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE_JXZ", sb_showmanyc},
    {"?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE_JPAD_J@Z", sb_xsgetn_m},
    {"?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE_JPBD_J@Z", sb_xsputn_m},
    {"?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PAD_J@Z", i_read},
    {"?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@V?$fpos@U_Mbstatet@@@2@@Z", i_seekg_pos},
    {"?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@_JH@Z", i_seekg_off},
    {"?tellg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@U_Mbstatet@@@2@XZ", i_tellg},
    {"?write@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@PBD_J@Z", o_write},
    {"?seekp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@V?$fpos@U_Mbstatet@@@2@@Z", o_seekp_pos},
    {"?seekp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@_JH@Z", o_seekp_off},
    {"?tellp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@U_Mbstatet@@@2@XZ", o_tellp},
    {"??1?$basic_iostream@DU?$char_traits@D@std@@@std@@UAE@XZ", nop_this0},
    {"?always_noconv@codecvt_base@std@@QBE_NXZ", always_noconv},
    {"??Bid@locale@std@@QAEIXZ", id_op},
    {"?flush@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@XZ", o_flush},
    // runtime-internal thunks
    {"rt.swfun", swfun},
    {"rt.facet_dtor", facet_dtor},
    {"rt.facet_incref", facet_incref},
    {"rt.facet_decref", facet_decref},
    {"rt.facet_bad", facet_bad},
    {"rt.sb_eof0", sb_eof0},
    {"rt.sb_eof1", sb_eof1},
    {"rt.sb_seek_bad5", sb_seek_bad5},
    {"rt.sb_seek_bad8", sb_seek_bad8},
    {"rt.nop_this1", nop_this1},
    {NULL, NULL},
};

// Point data imports at runtime-owned guest variables and build the fake vtables.
void rt_data_imports(void) {
    facet_vt = gdata_alloc(4 * 24);
    G32(facet_vt + 0) = rt_thunk_for("rt.facet_dtor");
    G32(facet_vt + 4) = rt_thunk_for("rt.facet_incref");
    G32(facet_vt + 8) = rt_thunk_for("rt.facet_decref");
    for (int i = 3; i < 24; i++) G32(facet_vt + 4 * i) = rt_thunk_for("rt.facet_bad");
    locimp = gdata_alloc(0x40);
    G32(locimp) = facet_vt;
    G32(locimp + 4) = 1;
    ios_vt = gdata_alloc(16);
    G32(ios_vt) = rt_thunk_for("rt.nop_this1");
    sb_vt = gdata_alloc(4 * 16);
    const char *slots[15] = {
        "rt.nop_this1",
        "?_Lock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAEXXZ",
        "?_Unlock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAEXXZ",
        "rt.sb_eof1", "rt.sb_eof1",
        "?showmanyc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE_JXZ",
        "rt.sb_eof0",
        "?uflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ",
        "?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE_JPAD_J@Z",
        "?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE_JPBD_J@Z",
        "rt.sb_seek_bad5", "rt.sb_seek_bad8",
        "?setbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEPAV12@PAD_J@Z",
        "?sync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ",
        "?imbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEXABVlocale@2@@Z",
    };
    for (int i = 0; i < 15; i++) G32(sb_vt + 4 * i) = rt_thunk_for(slots[i]);

    for (unsigned i = 0; i < img_nimports; i++) {
        const ImgImport *im = &img_imports[i];
        if (!strcmp(im->name, "?_BADOFF@std@@3_JB")) {
            uint32_t v = gdata_alloc(8);
            G64(v) = (uint64_t)-1;
            G32(im->iat) = v;
        } else if (!strcmp(im->name, "?id@?$codecvt@DDU_Mbstatet@@@std@@2V0locale@2@A")) {
            G32(im->iat) = gdata_alloc(8);
        }
    }
}
