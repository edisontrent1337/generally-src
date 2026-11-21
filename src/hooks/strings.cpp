// Entry adapters: string helpers.
#include "bridge/guest.h"
#include "game/strings.h"

using namespace abi;

REWRITE(403d40, "differs_nocase", 0, 0, RET_AL) { ret_bool(c, differs_nocase(ecx<MsString>(c), edx<MsString>(c))); }
REWRITE(403eb0, "less_nocase", 2, 0, RET_AL) { ret_bool(c, less_nocase(argp<MsString>(c, 0), argp<MsString>(c, 1))); }  // cdecl (a sort predicate)
