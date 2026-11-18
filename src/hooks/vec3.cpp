// Entry points of the original's Vec3 methods (see game/vec3.h).
//
// The original passes `this` in ECX and float arguments in the XMM register of their argument position
// (MSVC whole-program optimisation); a returned Vec3 is written through a hidden pointer that is also
// returned in EAX.
#include "bridge/guest.h"
#include "game/vec3.h"

using namespace abi;

REWRITE(405eb0, "Vec3::Vec3()", 0, 0, RET_EAX) {
    *ecx<Vec3>(c) = Vec3();
    ret(c, ECX);
}

REWRITE(405ed0, "Vec3::Vec3(x, y, z)", 0, 0, RET_EAX) {
    *ecx<Vec3>(c) = Vec3(xmm(c, 1), xmm(c, 2), xmm(c, 3));
    ret(c, ECX);
}

REWRITE(405ef0, "Vec3::operator=", 1, 4, RET_EAX) {
    *ecx<Vec3>(c) = *argp<Vec3>(c, 0);
    ret(c, ECX);
}

REWRITE(405f10, "Vec3::operator/=", 0, 0, RET_EAX) {
    *ecx<Vec3>(c) /= xmm(c, 1);
    ret(c, ECX);
}

REWRITE(405f40, "Vec3::operator+=", 1, 4, RET_EAX) {
    *ecx<Vec3>(c) += *argp<Vec3>(c, 0);
    ret(c, ECX);
}

REWRITE(405f80, "Vec3::operator-=", 1, 4, RET_EAX) {
    *ecx<Vec3>(c) -= *argp<Vec3>(c, 0);
    ret(c, ECX);
}

REWRITE(405fc0, "Vec3::operator*(float)", 1, 4, RET_EAX) {
    *argp<Vec3>(c, 0) = *ecx<Vec3>(c) * xmm(c, 2);
    ret(c, arg(c, 0));
}

REWRITE(406000, "operator*(float, Vec3)", 0, 0, RET_EAX) {
    *ecx<Vec3>(c) = xmm(c, 1) * *edx<Vec3>(c);
    ret(c, ECX);
}

REWRITE(406030, "Vec3::operator+", 2, 8, RET_EAX) {
    *argp<Vec3>(c, 0) = *ecx<Vec3>(c) + *argp<Vec3>(c, 1);
    ret(c, arg(c, 0));
}

REWRITE(406070, "Vec3::operator-", 2, 8, RET_EAX) {
    *argp<Vec3>(c, 0) = *ecx<Vec3>(c) - *argp<Vec3>(c, 1);
    ret(c, arg(c, 0));
}
