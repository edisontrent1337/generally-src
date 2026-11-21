// Entry adapters: the game's file class (all thiscall).
#include "bridge/guest.h"
#include "game/file.h"

using namespace abi;

REWRITE(4020c0, "file_construct", 0, 0, RET_EAX) { ret(c, guest::addr(file_construct(ecx<GameFile>(c)))); }
REWRITE(402160, "file_destruct", 0, 0, RET_NONE) { file_destruct(ecx<GameFile>(c)); }
REWRITE(402210, "file_open", 2, 8, RET_AL) { ret(c, file_open(ecx<GameFile>(c), argp<MsString>(c, 0), arg(c, 1))); }
REWRITE(4022d0, "file_close", 0, 0, RET_AL) { ret(c, file_close(ecx<GameFile>(c))); }
REWRITE(402460, "file_read", 2, 8, RET_EAX) { ret(c, file_read(ecx<GameFile>(c), argp<uint8_t>(c, 0), arg(c, 1))); }
REWRITE(402510, "file_write", 2, 8, RET_EAX) { ret(c, file_write(ecx<GameFile>(c), argp<uint8_t>(c, 0), arg(c, 1))); }
REWRITE(4025d0, "file_seek", 2, 8, RET_AL) {
    ret(c, file_seek(ecx<GameFile>(c), static_cast<int>(arg(c, 0)), static_cast<int>(arg(c, 1))));
}
REWRITE(402640, "file_truncate_here", 1, 4, RET_AL) { ret(c, file_truncate_here(ecx<GameFile>(c))); }
REWRITE(4026b0, "file_tell", 0, 0, RET_EAX) { ret(c, file_tell(ecx<GameFile>(c))); }
REWRITE(402700, "file_size", 0, 0, RET_EAX) { ret(c, file_size(ecx<GameFile>(c))); }
REWRITE(441ef0, "file_read_i16x2", 1, 0, RET_EAX) { ret(c, file_read_i16x2(ecx<GameFile>(c), edx<int16_t>(c), arg(c, 0))); }
REWRITE(441e20, "file_read_i16x3", 1, 0, RET_EAX) { ret(c, file_read_i16x3(ecx<GameFile>(c), edx<int16_t>(c), arg(c, 0))); }
REWRITE(441e80, "file_read_i16x4", 1, 0, RET_EAX) { ret(c, file_read_i16x4(ecx<GameFile>(c), edx<int16_t>(c), arg(c, 0))); }
REWRITE(441d80, "file_read_tagged_i16x3", 1, 0, RET_EAX) {
    ret(c, file_read_tagged_i16x3(ecx<GameFile>(c), edx<uint8_t>(c), arg(c, 0)));
}
REWRITE(404020, "list_directory", 3, 0, RET_NONE) {
    list_directory(ecx<MsString>(c), edx<MsString>(c), argp<MsVector<MsString>>(c, 0), arg(c, 1), arg(c, 2));
}
REWRITE(404490, "delete_folder", 0, 0, RET_AL) { ret_bool(c, delete_folder(ecx<MsString>(c))); }
