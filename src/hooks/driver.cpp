// Entry adapters: driver files.
#include "bridge/guest.h"
#include "game/driver.h"
#include "game/file.h"

using namespace abi;

REWRITE(4872b0, "driver_save", 0, 0, RET_NONE) { driver_save(ecx<Driver>(c), edx<MsString>(c)); }
REWRITE(443870, "driver_write", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(driver_write(ecx<GameFile>(c), edx<Driver>(c)))); }
REWRITE(486e40, "driver_load", 0, 0, RET_NONE) { driver_load(ecx<Driver>(c), edx<MsString>(c)); }
REWRITE(4429d0, "driver_read", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(driver_read(ecx<GameFile>(c), edx<Driver>(c)))); }
REWRITE(487470, "driver_delete", 0, 0, RET_NONE) { driver_delete(ecx<MsString>(c)); }
