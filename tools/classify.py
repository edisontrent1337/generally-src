#!/usr/bin/env python3
"""Classify every function of GeneRally.exe 1.2d as game code or library code.

Inputs : build/funcinfo.tsv  (tools/ghidra/ExportFuncInfo.java on the ORIGINAL exe)
         build/decomp.c      (tools/ghidra/DumpDecomp.java)
Output : analysis/functions.tsv  addr  size  class  label  reason

Classes
  game      game code -> to be rewritten as readable source
  stl       C++ standard library template instantiated into the exe (std::string, vector,
            fstream/filebuf, ostream operator<<, allocator) -> replaced by #include <...>
  crt       statically linked MSVC runtime (startup, /GS cookie, EH helpers, import thunks)
            -> provided by the compiler, nothing to write
  eh        exception-unwind funclet (Unwind@/Catch@): part of its parent game function
  init      compiler-generated constructor/destructor of a game global -> becomes the
            global's definition
  compiler  other compiler-generated code with no source of its own (implicit destructors,
            `vector deleting destructor', `vector constructor iterator', empty ctors)
Reviewed decisions live in OVERRIDES (addr -> (class, label, note)). Labels on game functions
are provisional names from that review.
"""
import re, sys, os, collections

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CRT_LO, CRT_HI = 0x496ADC, 0x4981A0   # vcruntime/scrt static code + import thunks
INIT_LO = 0x49AA00                    # dynamic-initializer / atexit destructor funclets

# addr -> (class, label, note). Filled in by manual review of the decompile.
def _ov(cls, table):
    return {a: (cls, l, "") for a, l in table.items()}

OVERRIDES = {}
OVERRIDES.update(_ov("stl", {
    0x401E20: "vector<8B>::_Tidy", 0x401ED0: "allocator<8B>::deallocate",
    0x401F90: "std::exception copy ctor", 0x401FD0: "std::exception scalar deleting dtor",
    0x402070: "facet delete (unwind helper)", 0x402770: "string::c_str",
    0x402780: "string::operator=(const string&)", 0x4027A0: "string::~string",
    0x402840: "string::string()", 0x402860: "basic_filebuf::close (fstream::close)",
    0x4029C0: "basic_filebuf::sync", 0x4029F0: "basic_filebuf::setbuf",
    0x402A40: "basic_filebuf::seekpos", 0x402B40: "basic_filebuf::seekoff",
    0x402E60: "basic_filebuf::underflow", 0x402EA0: "basic_filebuf::pbackfail",
    0x403120: "basic_filebuf::_Unlock", 0x403130: "basic_filebuf::_Lock",
    0x4031E0: "basic_fstream deleting dtor", 0x403240: "basic_filebuf deleting dtor",
    0x403270: "string::_Become_small", 0x403480: "string::assign(const string&)",
    0x403510: "basic_filebuf::_Set_back", 0x403550: "basic_filebuf::_Reset_back",
    0x403890: "trivial virtual (returns arg)", 0x4036C0: "basic_filebuf::_Init",
    0x403830: "allocator<char>::deallocate", 0x4039B0: "string::_Copy (grow)",
    0x403A52: "string::_Copy (tail)", 0x403C70: "bad_cast copy ctor",
    0x403CB0: "string moved-from reset", 0x403CC1: "basic_fstream vbase deleting dtor thunk",
    0x403CE0: "memcmp (char_traits::compare)", 0x402050: "std::exception::~exception",
    0x403A49: "string::_Copy (catch fragment)",
    0x404510: "vector<string>::push_back", 0x4045D0: "string::substr",
    0x404610: "string::string(string&&)", 0x404690: "string::string(const char*)",
    0x404780: "string::assign(const char*, size_t)", 0x4049F0: "string move assign",
    0x404A60: "vector<string>::_Reallocate", 0x404B00: "string::swap",
    0x404B80: "string::operator=(string&&)", 0x404C00: "allocator<string>::deallocate",
    0x404C70: "operator+(const char*, const string&)", 0x404D80: "operator+(string&&, ...)",
    0x404DB0: "string small-buffer swap", 0x404F40: "std::transform(tolower)",
    0x404FA0: "std::sort<string> _Sort_unchecked", 0x4050E0: "_Destroy_range<string>",
    0x405130: "std::sort<string> _Partition_by_median_guess", 0x405430: "string::~string (unwind)",
    0x405470: "std::sort<string> _Guess_median", 0x405530: "std::sort<string> _Insertion_sort",
    0x405660: "std::sort<string> _Sort_heap", 0x405740: "std::sort<string> _Insertion_sort",
    0x405970: "_Uninitialized_move<string>", 0x4059B0: "std::sort<string> _Med3",
    0x405A60: "std::sort<string> _Push_heap", 0x405BA0: "std::sort<string> _Adjust_heap",
    0x405CB0: "move_backward<string>", 0x405D50: "std::sort<string> _Pop_heap",
    0x405E00: "std::cos(float)", 0x405E20: "std::log10(float)", 0x405E40: "std::sin(float)",
    0x405E60: "std::sqrt(float)", 0x405E80: "cosf", 0x405E90: "sinf", 0x405EA0: "sqrtf",
    0x48F860: "std::pow(float)",
    0x440470: "vector<string>::_Tidy", 0x4404D0: "operator+(const string&, const char*)",
    0x443C70: "vector<int>::operator[]", 0x443C80: "vector<int>::size",
    0x443C90: "operator+(const string&, const string&)",
    0x46CFA0: "vector<8B>::_Reallocate", 0x46D010: "std::sort<8B> _Sort_unchecked",
    0x46D100: "std::sort<8B> _Partition_by_median_guess", 0x46D360: "std::sort<8B> _Guess_median",
    0x46D400: "std::sort<8B> _Make_heap", 0x46D460: "std::sort<8B> _Sort_heap",
    0x46D4D0: "std::sort<8B> _Insertion_sort", 0x46D5B0: "_Uninitialized_move<8B>",
    0x46D5E0: "std::sort<8B> _Med3", 0x46D660: "std::sort<8B> _Pop_heap_hole",
    0x46D6F0: "std::sort<8B> _Push_heap",
    0x46F910: "basic_stringstream::~basic_stringstream",
    0x4704B0: "basic_stringstream::str(const string&)", 0x4704D0: "basic_stringstream ctor",
    0x470890: "basic_stringbuf::underflow", 0x4708F0: "basic_stringbuf::pbackfail",
    0x470B20: "basic_stringstream deleting dtor", 0x470B90: "basic_stringbuf deleting dtor",
    0x470BD0: "basic_stringbuf::_Tidy", 0x470C50: "basic_stringbuf::str(const string&)",
    0x470D60: "basic_stringbuf::_Init", 0x470DFC: "basic_stringstream vbase deleting dtor thunk",
    0x471280: "allocator<16B>::deallocate", 0x4712E0: "vector<16B>::_Reallocate",
    0x4713D0: "_Uninitialized_move<16B>",
    0x476F30: "vector<T>::push_back", 0x477000: "vector<NamedInt>::_Tidy",
    0x477060: "vector<int>::_Tidy", 0x477130: "vector<string>::push_back",
    0x477420: "string::compare(const char*)", 0x477490: "string::find(const char*)",
    0x477500: "vector<NamedInt>::_Reallocate", 0x4775B0: "allocator<int>::deallocate",
    0x477610: "vector<int>::_Reallocate", 0x477670: "ostream::sentry::~sentry (unwind)",
    0x4776C0: "allocator<NamedInt>::deallocate", 0x477DA0: "_Destroy_range<NamedInt>",
    0x477C50: "std::sort<NamedInt> _Sort_unchecked",
    0x477DF0: "std::sort<NamedInt> _Partition_by_median_guess",
    0x478150: "std::sort<NamedInt> _Guess_median", 0x478220: "std::sort<NamedInt> _Make_heap",
    0x4782F0: "std::sort<NamedInt> _Sort_heap", 0x4783D0: "std::sort<NamedInt> _Insertion_sort",
    0x478680: "_Uninitialized_move<NamedInt>", 0x4786C0: "std::sort<NamedInt> _Med3",
    0x478790: "std::sort<NamedInt> _Pop_heap_hole", 0x478900: "std::sort<NamedInt> _Push_heap",
    0x478A30: "move_backward<NamedInt>", 0x478AE0: "std::sort<NamedInt> _Pop_heap",
    0x478B90: "std::fixed", 0x478BB0: "std::scientific",
    0x485010: "basic_stringstream::str(const string&)", 0x485040: "operator<<(ostream&, _Smanip) (setw)",
    0x48AA40: "vector<T*>::push_back", 0x48AAB0: "string::find(char)",
    0x490080: "move_backward<string>", 0x4485B0: "operator+(string, ...)",
    0x470590: "basic_stringbuf::seekpos", 0x4706C0: "basic_stringbuf::seekoff",
    0x470950: "basic_stringbuf::overflow",
}))
OVERRIDES.update(_ov("crt", {
    0x403CD0: "_guard_check_icall (CFG no-op)", 0x406180: "`vector constructor iterator'",
    0x46F2D0: "__local_stdio_printf_options", 0x46F4D0: "__local_stdio_scanf_options",
    0x46F2E0: "vsprintf (stdio.h inline)", 0x46F320: "sprintf (stdio.h inline)",
    0x46F350: "sprintf (stdio.h inline)", 0x472560: "sprintf (stdio.h inline)",
    0x46F4E0: "sscanf (stdio.h inline)",
}))
OVERRIDES.update(_ov("compiler", {
    0x403C60: "empty ctor", 0x46D000: "empty ctor",
    0x46F580: "`vector deleting destructor' (0x24-byte elements)",
    0x46FCF0: "`vector deleting destructor' (0x3c-byte elements)",
    0x46FD90: "implicit dtor (0x3c-byte struct with string)",
    0x47CF10: "implicit dtor (struct with string at +0x28)",
    0x47DB40: "implicit dtor (struct with string at +0xc), used by globals",
}))
OVERRIDES.update(_ov("game", {
    # File: thin wrapper class around std::fstream (string name +0xc0, mode +0xd8)
    0x4020C0: "File::File", 0x402160: "File::~File", 0x402210: "File::open(name, mode)",
    0x4022D0: "File::close", 0x402460: "File::read", 0x402510: "File::write",
    0x4025D0: "File::seek", 0x402640: "File::mark", 0x4026B0: "File::tell",
    0x402700: "File::size",
    0x403D40: "lessNoCase(string, string)", 0x403EB0: "lessNoCase variant (sort predicate)",
    0x404020: "listDirectory", 0x404490: "deleteFolder (SHFileOperation)",
    # Vec3 (float x, y, z) used by the physics code
    0x405EB0: "Vec3::Vec3()", 0x405ED0: "Vec3::Vec3(x, y, z)", 0x405EF0: "Vec3::operator=",
    0x405F10: "Vec3::operator/=", 0x405F40: "Vec3::operator+=", 0x405F80: "Vec3::operator-=",
    0x405FC0: "Vec3::operator*", 0x406000: "Vec3 operator*(float, Vec3)",
    0x406030: "Vec3::operator+", 0x406070: "Vec3::operator-",
    0x46C550: "display mode comparator (sort predicate)", 0x46C570: "enumerateDisplayModes",
    0x46EDC0: "Sample::Sample (refcounted)", 0x46EE50: "Sample::release",
    0x46EFA0: "loadWav", 0x46F7E0: "toString (stringstream)", 0x46F960: "toString (stringstream)",
    0x478C00: "struct ctor (int, int, byte, string)", 0x478C50: "car struct ctor",
    0x488A60: "list item ctor", 0x489090: "list item dtor",
    0x4716E0: "replace #NNNN# tokens in text",
}))
# reviewed: game code, not named yet
for _a in (0x43D070, 0x441D80, 0x441E20, 0x46D760, 0x46D830, 0x46F510, 0x46F5E0, 0x46F780,
           0x46FAA0, 0x46FE00, 0x472760, 0x4766E0, 0x489150, 0x4894D0, 0x4896D0, 0x489770,
           0x4897F0, 0x48D700, 0x48F9E0, 0x488EF0, 0x48A950, 0x4060B0, 0x441E80, 0x441EF0,
           0x43FC60, 0x470370, 0x489380):
    OVERRIDES[_a] = ("game", "", "")

SEED_PATTERNS = [
    (r"_Xlength_error|_Xout_of_range|_Xbad_alloc|_Xinvalid_argument", "STL throw helper"),
    (r"basic_filebuf<char,std::char_traits<char>_>::vftable", "filebuf"),
    (r"codecvt<char,char,_Mbstatet>|codecvt_base::always_noconv", "codecvt"),
    (r"std::_Lockit::_Lockit", "locale/facet lock"),
    (r"::_Osfx|std::uncaught_exception", "ostream sentry"),
    (r"std::_Fiopen", "filebuf::open"),
    (r"0xffffffe0", "allocator (32-byte aligned big blocks)"),
]
SEED_MAX_SIZE = 700   # large functions using inlined STL are game code


def load():
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(ROOT, "build/funcinfo.tsv"))]
    F = {}
    for r in rows:
        r += [""] * (7 - len(r))
        a = int(r[0], 16)
        F[a] = dict(name=r[1], size=int(r[2]), callers={int(x, 16) for x in r[4].split(",") if x},
                    callees=[c for c in r[5].split(",") if c], strings=r[6])
    src = open(os.path.join(ROOT, "build/decomp.c")).read()
    parts = re.split(r"\n//==== (\S+) @ ([0-9a-f]{8})\n", "\n" + src)
    body = {int(parts[i + 1], 16): parts[i + 2] for i in range(1, len(parts), 3)}
    return F, body


def classify(F, body):
    cls, label, why = {}, {}, {}
    for a, f in F.items():
        n = f["name"]
        if n.startswith(("Unwind@", "Catch")):
            cls[a], why[a] = "eh", "unwind funclet"
        elif CRT_LO <= a < CRT_HI:
            cls[a], why[a] = "crt", "runtime region"
            if not n.startswith("FUN_"):
                label[a] = n
        elif a >= INIT_LO:
            cls[a], why[a] = "init", "global ctor/dtor region"
    for a, f in F.items():
        if a in cls:
            continue
        b = body.get(a, "")
        sdl = any(c.startswith("SDL") for c in f["callees"])
        if f["size"] <= SEED_MAX_SIZE and not sdl:
            for pat, w in SEED_PATTERNS:
                if re.search(pat, b):
                    cls[a], why[a] = "stl", w
                    break
    # propagate: helpers reachable only from STL code and calling only STL/CRT/imports
    changed = True
    while changed:
        changed = False
        for a, f in F.items():
            if a in cls or a in OVERRIDES:
                continue
            callers = f["callers"] - {0x403CD0}
            internal = [int(c, 16) for c in f["callees"] if re.fullmatch(r"[0-9a-f]{8}", c)]
            if callers and all(cls.get(c) == "stl" for c in callers) and \
               all(cls.get(c) in ("stl", "crt") for c in internal):
                cls[a], why[a] = "stl", "only used by STL code"
                changed = True
    for a in F:
        cls.setdefault(a, "game")
        why.setdefault(a, "")
    for a, (c, l, note) in OVERRIDES.items():
        cls[a], label[a], why[a] = c, l, "reviewed" + (": " + note if note else "")
    return cls, label, why


def needs_review(a, F, body, cls):
    """Small 'game' functions touching no game globals / SDL / strings: maybe inlined STL."""
    if cls[a] != "game" or a in OVERRIDES:
        return False
    f, b = F[a], body.get(a, "")
    globs = set(re.findall(r"DAT_004[0-9a-f]{5}", b)) - {"DAT_004a400c"}
    sdl = any(c.startswith("SDL") for c in f["callees"])
    return f["size"] < 250 and not globs and not sdl and not f["strings"]


def main():
    F, body = load()
    cls, label, why = classify(F, body)
    os.makedirs(os.path.join(ROOT, "analysis"), exist_ok=True)
    with open(os.path.join(ROOT, "analysis/functions.tsv"), "w") as w:
        w.write("addr\tsize\tclass\tlabel\treason\n")
        for a in sorted(F):
            w.write(f"{a:08x}\t{F[a]['size']}\t{cls[a]}\t{label.get(a, '')}\t{why[a]}\n")
    cnt = collections.Counter(cls.values())
    sz = collections.Counter()
    for a in F:
        sz[cls[a]] += F[a]["size"]
    for c in ("game", "stl", "crt", "eh", "init", "compiler"):
        print(f"{c:5} {cnt[c]:4} functions {sz[c]:7} bytes")
    if "-r" in sys.argv:
        for a in sorted(F):
            if needs_review(a, F, body, cls):
                print(f"review {a:08x} {F[a]['size']:4} callers={len(F[a]['callers'])}")


if __name__ == "__main__":
    main()
