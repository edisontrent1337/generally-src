// MSVC 2015 (x86) standard library objects as they sit in game data.
//
// Game structures embed std::string and std::vector, and original code still creates, copies and frees
// them, so while the two halves coexist the rewrite must use the exact MSVC layouts and the original's own
// code for anything that allocates (same heap blocks, same growth policy). At the end these become
// std::string / std::vector.
#pragma once
#include <cstring>

#include "bridge/guest.h"

// std::string: 16-byte small buffer, used while capacity < 16
struct MsString {
    union {
        char buf[16];
        ptr32<char> ptr;
    };
    uint32_t size;
    uint32_t cap;

    const char *c_str() const { return cap >= 16 ? ptr.get() : buf; }
    char *data() { return cap >= 16 ? ptr.get() : buf; }

    // the inlined default constructor
    void init() {
        cap = 15;
        size = 0;
        buf[0] = 0;
    }
    // basic_string::assign(const char *, size_t)                       (0x404780, thiscall)
    void assign(const char *s, uint32_t n) { guest::call_thiscall(0x404780, this, s, n); }
    // basic_string::assign(const basic_string &, size_t pos, size_t n)  (0x4033a0, thiscall)
    void assign(const MsString &src) { guest::call_thiscall(0x4033a0, this, &src, 0u, 0xffffffffu); }
    // basic_string(const char *) (0x404690, thiscall), s a guest C string
    void construct(uint32_t s) { guest::call_thiscall(0x404690, this, s); }
    // find(char) (0x477490, thiscall); -1 if absent
    int find(char ch) {
        guest::Local<char> c;
        *c = ch;
        return static_cast<int>(guest::call_thiscall(0x477490, this, c.get(), 0u, 0u));
    }
    // substr(pos, n) into out (0x4045d0, thiscall)
    void substr(MsString *out, uint32_t pos, uint32_t n) const {
        guest::call_thiscall(0x4045d0, const_cast<MsString *>(this), out, pos, n);
    }
    // basic_string(basic_string &&) (0x404610, thiscall): takes src's contents, leaves it empty
    void move_from(MsString *src) { guest::call_thiscall(0x404610, this, src); }
    // operator=(basic_string &&) (0x4049f0, thiscall); the original clears this first
    void move_assign(MsString *src) { guest::call_thiscall(0x4049f0, this, src); }
    // operator+(const basic_string &, const char *) (0x4485b0): out = a + b, b a guest C string
    static void concat(MsString *out, const MsString *a, uint32_t b) {
        guest::call_regs(0x4485b0, guest::addr(out), guest::addr(a), b);
    }
    // operator+(const basic_string &, const basic_string &) (0x404d80): out = a + b
    static void concat(MsString *out, const MsString *a, const MsString *b) {
        guest::call_regs(0x404d80, guest::addr(out), guest::addr(a), guest::addr(b));
    }
    // operator+(const char *, const basic_string &) (0x404c70): out = a + b, a a guest C string
    static void concat(MsString *out, uint32_t a, const MsString *b) {
        guest::call_regs(0x404c70, guest::addr(out), a, guest::addr(b));
    }
    // operator+(basic_string &&, const char *) (0x4404d0): out = move(a) + b
    static void concat_move(MsString *out, MsString *a, uint32_t b) {
        guest::call_regs(0x4404d0, guest::addr(out), guest::addr(a), b);
    }
    // operator+(basic_string &&, const basic_string &) (0x443c90): out = move(a) + b
    static void concat_move(MsString *out, MsString *a, const MsString *b) {
        guest::call_regs(0x443c90, guest::addr(out), guest::addr(a), guest::addr(b));
    }
    // append(const char *, n) (0x404840) and append(const basic_string &, pos, n) (0x404930), thiscall
    void append(uint32_t s, uint32_t n) { guest::call_thiscall(0x404840, this, s, n); }
    void append(const MsString &s) { guest::call_thiscall(0x404930, this, &s, 0u, 0xffffffffu); }
    // the inlined reserve(n): _Grow (0x4038a0, thiscall) keeps the contents
    void reserve(uint32_t n) {
        uint32_t old = size;
        if (old <= n && cap != n && (guest::call_thiscall(0x4038a0, this, n, true) & 0xff)) {
            size = old;
            data()[old] = 0;
        }
    }
    // the inlined clear()
    void clear() {
        size = 0;
        data()[0] = 0;
    }
    // the inlined destructor: frees a heap buffer through allocator<char>::deallocate (0x403830, stdcall)
    void destroy() {
        if (cap >= 16) guest::call_stdcall(0x403830, ptr, cap + 1);
        init();
    }
};
static_assert(sizeof(MsString) == 0x18);

// std::vector<T>: three pointers
template <class T> struct MsVector {
    ptr32<T> first, last, end;
    int size() const { return static_cast<int>(last.a - first.a) / static_cast<int>(sizeof(T)); }
    T &operator[](int i) const { return first.get()[i]; }
    T &back() const { return last.get()[-1]; }
    T *begin() const { return first.get(); }
};
static_assert(sizeof(MsVector<int>) == 12);

// MSVC _stricmp in the "C" locale: ASCII case folding, compared as unsigned chars
inline int ms_stricmp(const char *a, const char *b) {
    int ca, cb;
    do {
        ca = static_cast<unsigned char>(*a++);
        cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    } while (ca && ca == cb);
    return ca - cb;
}

// std::stringstream (MSVC 2015 x86 layout; the ostream part starts at +0x10). Lives in guest memory
// (use guest::Local<MsStringStream>); construct() / destruct() run the original's constructor and
// destructor.
struct MsStringStream {
    uint8_t raw[0x100];

    void construct() { guest::call_thiscall(0x4704d0, this, 0u, 0u); }
    void destruct() { guest::call_thiscall(0x46f910, this); }
    uint32_t os() { return guest::addr(raw + 0x10); }
    MsStringStream &operator<<(int v) {
        guest::call_regs(guest::at<uint32_t>(IMP_ostream_insert_int), os(), 0, v);
        return *this;
    }
    MsStringStream &operator<<(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        guest::call_regs(guest::at<uint32_t>(IMP_ostream_insert_float), os(), 0, bits);
        return *this;
    }
    MsStringStream &put(uint32_t cstr) {  // a guest C string
        guest::call_regs(0x477730, os(), cstr);
        return *this;
    }
    MsStringStream &operator<<(const MsString *s) {
        guest::call_regs(0x477980, os(), guest::addr(s));
        return *this;
    }
    // setw(n) (the msvcp import builds the manipulator, 0x485040 applies it)
    MsStringStream &width(int n) {
        guest::Local<uint32_t[4]> m;
        guest::call_cdecl(guest::at<uint32_t>(IMP_setw), guest::addr(m.get()), n, 0);
        guest::call_regs(0x485040, os(), guest::addr(m.get()));
        return *this;
    }
    MsStringStream &left() { return manip(0x478b90); }   // std::left
    MsStringStream &right() { return manip(0x478bb0); }  // std::right
    MsStringStream &manip(uint32_t fn) {
        guest::call_regs(guest::at<uint32_t>(IMP_ostream_insert_manip), os(), 0, fn);
        return *this;
    }
    MsStringStream &put_uint(uint32_t v) {
        guest::call_regs(guest::at<uint32_t>(IMP_ostream_insert_uint), os(), 0, v);
        return *this;
    }
    void str(MsString *out) { guest::call_thiscall(0x4704b0, this, out); }
    // str(const string &s) with s empty, inlined: stringbuf::_Tidy (0x470bd0) then
    // stringbuf::_Init(s.c_str(), 0, mode) (0x470d60); empty is a default-constructed guest string
    void reset(MsString *empty) {
        uint8_t *buf = raw + 0x18;
        guest::call_thiscall(0x470bd0, buf);
        guest::call_thiscall(0x470d60, buf, empty, 0u, *reinterpret_cast<uint32_t *>(raw + 0x54));
    }
};
