// Case-insensitive string comparison (see strings.h).
#include "game/strings.h"

// Lower-cases copies of a and b (std::transform with tolower, 0x404f40) and compares them with
// CompareStringA in the invariant locale: CSTR_LESS_THAN 1, CSTR_EQUAL 2, CSTR_GREATER_THAN 3.
static int compare_nocase(const MsString *a, const MsString *b) {
    guest::Local<MsString> la, lb;
    guest::Local<uint32_t> result;
    la->init();
    la->assign(*a);
    lb->init();
    lb->assign(*b);
    uint32_t tolower_fn = guest::at<uint32_t>(IMP_tolower);
    MsString *both[2] = {la.get(), lb.get()};
    for (MsString *s : both) {
        uint32_t first = guest::addr(s->data());
        guest::call_regs(0x404f40, guest::addr(result.get()), first, first + s->size, first, tolower_fn);
    }
    int r = static_cast<int>(guest::call_import(IMP_CompareStringA, 0x7fu, 0u, guest::addr(la->c_str()), la->size,
                                                guest::addr(lb->c_str()), lb->size));
    lb->destroy();
    la->destroy();
    return r;
}

bool differs_nocase(const MsString *a, const MsString *b) { return compare_nocase(a, b) != 2; }
bool less_nocase(const MsString *a, const MsString *b) { return compare_nocase(a, b) < 2; }
