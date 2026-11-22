// Float maths as the original computes it.
#pragma once
#include "bridge/guest.h"

// The <cmath> float sqrt overload (0x405e60): computed through double sqrt in the original's CRT.
inline float guest_sqrtf(float x) {
    guest::set_xmm(0, x);
    guest::call_regs(0x405e60, 0, 0);
    return guest::xmm0();
}

// The <cmath> float log10 overload (0x405e20): the CRT's double log10 (0x498174) on the widened value.
inline float guest_log10f(float x) {
    guest::set_xmm(0, x);
    guest::call_regs(0x405e20, 0, 0);
    return guest::xmm0();
}

// The <cmath> float pow overload (0x48f860): the CRT's double pow (0x49817a) on the widened values.
inline float guest_powf(float x, float y) {
    guest::set_xmm(0, x);
    guest::set_xmm(1, y);
    guest::call_regs(0x48f860, 0, 0);
    return guest::xmm0();
}

// The <cmath> float cos / sin overloads (0x405e00, 0x405e40): the CRT's double cos / sin on the widened value.
inline float guest_cosf(float x) {
    guest::set_xmm(0, x);
    guest::call_regs(0x405e00, 0, 0);
    return guest::xmm0();
}
inline float guest_sinf(float x) {
    guest::set_xmm(0, x);
    guest::call_regs(0x405e40, 0, 0);
    return guest::xmm0();
}

// The CRT's double cos / sin (libm_sse2_cos_precise / _sin_precise through the thunks 0x49816e, 0x498180).
inline double guest_cos(double x) {
    CPU *c = guest::cpu();
    XMM_D(0) = x;
    guest::call_regs(0x49816e, 0, 0);
    return XMM_D(0);
}
inline double guest_sin(double x) {
    CPU *c = guest::cpu();
    XMM_D(0) = x;
    guest::call_regs(0x498180, 0, 0);
    return XMM_D(0);
}
