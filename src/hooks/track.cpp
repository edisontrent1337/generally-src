// Entry adapters: the loaded track.
#include "bridge/guest.h"
#include "game/track.h"

using namespace abi;

REWRITE(416f70, "terrain_height", 3, 0, RET_XMM0) {
    ret_xmm0(c, terrain_height(Vec3(argf(c, 0), argf(c, 1), argf(c, 2))));
    for (int i = 1; i < 4; i++) XMM_F(0, i) = 0.0f;  // the original's result register has zero upper lanes
}
REWRITE(416c50, "terrain_sample", 0, 0, RET_NONE) { terrain_sample(ecx<Vec3>(c), edx<Vec3>(c)); }
REWRITE(43d390, "track_load", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(track_load())); }
REWRITE(490120, "track_preview_load", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(track_preview_load(ecx<MsString>(c)))); }
