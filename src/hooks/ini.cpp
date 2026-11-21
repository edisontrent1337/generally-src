// Entry adapters: settings files.
#include "bridge/guest.h"
#include "game/ini.h"

using namespace abi;

REWRITE(46f510, "ini_construct", 0, 0, RET_EAX) { ret(c, guest::addr(ini_construct(ecx<IniFile>(c)))); }
REWRITE(46f5e0, "ini_destruct", 0, 0, RET_NONE) { ini_destruct(ecx<IniFile>(c)); }
REWRITE(470370, "ini_clear", 0, 0, RET_NONE) { ini_clear(ecx<IniFile>(c)); }
REWRITE(46fe00, "ini_find_section", 1, 4, RET_EAX) {
    ret(c, guest::addr(ini_find_section(ecx<IniFile>(c), argp<MsString>(c, 0))));
}
REWRITE(46fe60, "ini_add_section", 1, 4, RET_EAX) {
    ret(c, guest::addr(ini_add_section(ecx<IniFile>(c), argp<MsString>(c, 0))));
}
REWRITE(46f620, "ini_get", 4, 16, RET_EAX) {
    ret(c, ini_get(ecx<IniFile>(c), argp<char>(c, 0), argp<char>(c, 1), argp<int32_t>(c, 2), argp<int32_t>(c, 3)));
}
REWRITE(46f780, "ini_find", 2, 8, RET_EAX) { ret(c, guest::addr(ini_find(argp<IniSection>(c, 0), argp<MsString>(c, 1)))); }
REWRITE(46fb60, "ini_add", 6, 24, RET_EAX) {
    float f;
    uint32_t bits = arg(c, 3);
    std::memcpy(&f, &bits, 4);
    ret(c, guest::addr(ini_add(argp<IniSection>(c, 0), argp<char>(c, 1), static_cast<int32_t>(arg(c, 2)), f,
                               argp<MsString>(c, 4), static_cast<int32_t>(arg(c, 5)))));
}
REWRITE(46f7e0, "ini_set_int", 3, 12, RET_EAX) {
    ret(c, guest::addr(ini_set_int(argp<IniSection>(c, 0), argp<char>(c, 1), static_cast<int32_t>(arg(c, 2)))));
}
REWRITE(46f960, "ini_set_float", 2, 8, RET_EAX) {
    ret(c, guest::addr(ini_set_float(argp<IniSection>(c, 0), argp<char>(c, 1), XMM_F(3, 0))));
}
REWRITE(46faa0, "ini_set_string", 3, 12, RET_EAX) {
    ret(c, guest::addr(ini_set_string(argp<IniSection>(c, 0), argp<char>(c, 1), argp<char>(c, 2))));
}
REWRITE(46fd60, "ini_entry_construct", 0, 0, RET_EAX) { ret(c, guest::addr(ini_entry_construct(ecx<IniEntry>(c)))); }
REWRITE(46ff90, "ini_load", 1, 4, RET_EAX) { ret(c, ini_load(ecx<IniFile>(c))); }
REWRITE(485070, "settings_load", 0, 0, RET_NONE) { settings_load(); }
