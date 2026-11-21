// Entry adapters: sound handles, selected tracks.
#include "bridge/guest.h"
#include "game/sound.h"
#include "game/selected_tracks.h"

using namespace abi;

REWRITE(48f880, "sound_add", 0, 0, RET_EAX) { ret(c, sound_add(ecx<SoundRef>(c))); }
REWRITE(48f940, "sounds_free", 0, 0, RET_NONE) { sounds_free(); }
REWRITE(48f9e0, "sound_load", 0, 0, RET_EAX) { ret(c, sound_load(ecx<MsString>(c))); }
REWRITE(48fab0, "sound_share", 0, 0, RET_EAX) { ret(c, sound_share(ECX)); }
REWRITE(48fb00, "sound_set_volume", 0, 0, RET_NONE) { sound_set_volume(ECX, static_cast<int>(EDX)); }
REWRITE(48fb60, "sound_play", 0, 0, RET_NONE) { sound_play(ECX); }

REWRITE(490050, "selected_tracks_init", 1, 4, RET_EAX) { ret_ptr(c, selected_tracks_init()); }
REWRITE(48fcc0, "selected_tracks_free", 0, 0, RET_NONE) { selected_tracks_free(); }
REWRITE(48fd30, "selected_track_add", 1, 4, RET_NONE) { selected_track_add(argp<char>(c, 0)); }
REWRITE(48fe00, "selected_track_remove", 1, 4, RET_EAX) { ret(c, selected_track_remove(arg(c, 0))); }
REWRITE(48fe80, "selected_track_find", 2, 8, RET_EAX) { ret(c, selected_track_find(argp<char>(c, 0))); }
REWRITE(48ff50, "selected_track_find_nocase", 2, 8, RET_EAX) { ret(c, selected_track_find_nocase(argp<char>(c, 0))); }
REWRITE(490000, "selected_track_name", 1, 4, RET_EAX) { ret_ptr(c, selected_track_name(arg(c, 0))); }
REWRITE(46ec50, "audio_open", 0, 0, RET_AL) { ret(c, audio_open()); }
REWRITE(46ed40, "audio_close", 0, 0, RET_NONE) { audio_close(); }
REWRITE(46edc0, "sound_create", 0, 0, RET_EAX) { ret(c, guest::addr(sound_create(ecx<uint8_t>(c), EDX))); }
REWRITE(46ee50, "sound_ref_free", 0, 0, RET_NONE) { sound_ref_free(ecx<SoundRef>(c)); }
REWRITE(46eea0, "sound_stop", 0, 0, RET_NONE) { sound_stop(ecx<SoundRef>(c)); }
REWRITE(46efa0, "sound_load_wav", 0, 0, RET_EAX) { ret(c, guest::addr(sound_load_wav(ecx<MsString>(c)))); }
REWRITE(46f080, "audio_mix", 3, 0, RET_NONE) { audio_mix(arg(c, 0), argp<int16_t>(c, 1), arg(c, 2)); }
REWRITE(472580, "selected_track_count", 0, 0, RET_EAX) { ret(c, selected_track_count()); }
REWRITE(48fcb0, "sounds", 1, 4, RET_EAX) { ret(c, guest::addr(sounds())); }
REWRITE(48fc30, "sounds_reallocate", 1, 4, RET_NONE) { sounds_reallocate(arg(c, 0)); }
