# Notes

Findings about GeneRally 1.2d needed for the rewrite, and the progress log. Addresses are in the original exe
(image base 0x400000). Earlier reverse engineering (12-car mod, netplay, macOS port) lives in the sibling repo
`generally-mod` (its NOTES.md documents many globals and per-car structures); the harness here was vendored
from it at the commit in `tools/VENDORED_FROM` and adapted to the unmodified exe.

## Code classes (analysis/functions.tsv)

328 game functions (~467 KB) to rewrite; 182 STL template instantiations (std::string, vector, fstream/filebuf,
stringstream, std::sort ×3, <cmath> float overloads), 144 statically linked CRT pieces, 573 exception-unwind
funclets, 9 global constructors/destructors, 7 other compiler-generated functions. The game links UCRT,
VCRUNTIME140 and MSVCP140 as DLLs.

## Calling conventions seen

- MSVC whole-program optimisation: `this` in ECX; float arguments in the XMM register of their *position*
  (Vec3(x,y,z) at 0x405ed0: xmm1..xmm3; Vec3::operator*(float) at 0x405fc0: out pointer on the stack, factor in
  xmm2); class results through a hidden pointer, returned in EAX.
- thiscall / stdcall pop their stack arguments (`ret N`), cdecl does not.

## Calling conventions seen (continued)

- Custom LTCG conventions are common: e.g. gui_open (0x488ab0) takes ECX=screen, EDX=widgets, XMM1=scale and six
  stack args; button_init (0x489150) takes x/y in XMM2/XMM3 (argument positions); fastcall functions whose stack
  arguments the caller pops; bool results only in AL (`RET_AL`, the shadow check compares the low byte only).
- MSVCP imports are called with `this` in ECX (e.g. ostream::operator<<(int) via import slot 0x49b084).

## Data layouts

Authoritative definitions are the structs in `src/game/*.h` (with offsets in comments). Summary:

- Vec3: `{float x, y, z}` (12 bytes), methods 0x405eb0..0x406070.
- GUI toolkit (src/game/gui.h). GuiState at 0x4a5180 (0x38 bytes): screen, draw callbacks per widget type [4],
  scale, widget count, focus index, dragging/pressed bytes, drag offset float +0x24, editing index, wheel
  accumulator (int16 +0x2c), flags (1 keyboard navigation, 4 focus first widget), widget array. A stack of saved
  states is kept by gui_push/gui_pop (0x488da0/0x488e00). g_gui_active 0x4a47f2.
- Widget (0x54): handle, redraw mask, state bits (1 focus, 2 down, 4 editing, 0x10/0x20 slider +/- held),
  rect floats x,y,w,h at +0x0c..+0x18, min/pos/max ints +0x1c..+0x24 (slider value, list scroll), label
  std::string +0x28, type byte +0x40 (1 button, 2 text field, 3 slider, 4 list), id int +0x44, flags +0x48
  (1 hidden, 2 no focus, 4 wide slider, 8 click on list item, 0x20 default=Enter, 0x40 cancel=Esc), list data
  +0x4c, on_click callback +0x50.
- ListData (0x28): selected, previous, visible rows, columns, column width / row height / scrollbar width
  (floats), vector<ListItem*> +0x1c. ListItem (0x24): marked +0, std::string text +4, data +0x20 (Driver* in the
  driver list).
- Widget draw callbacks: 0x4735a0 buttons/menu widgets, 0x473930 text field, 0x473b00 slider, 0x473ee0 list box.
  List ids: 1 driver list (non-human drivers drawn in colour 0x5a), 20+p car list of player p (car previews via
  0x443cc0), other ids plain.
- Video (src/game/video.h): back surface 0x4a4804, background 0x4a4c08, palette SDL_Color[256] 0x4a4808, lock
  state pitch 0x4a4800 / pixels 0x4a4c0c / locked surface 0x4a4c10, screen size 0x4a4d3c × 0x4a4d1c.
  Menu border width 0x4a4df8. Font size 0x4a4db4, font 0x4a4c84; draw_text 0x484e50 (flags 1 centre x,
  2 centre y).
- Main menu widgets (array pointer 0x4a4f0c): 3+k car button and 9+k name button of player slot k (1-based),
  16 = available drivers list (0x4a4f04).
- Players: count 0x4a4dc0 (max 6), Driver* [6] at 0x4a4d04 (set via 0x487020, ECX=slot, EDX=driver), names
  char[6][13] at 0x4a4e08. Driver: car +0x40, human flag +0x7c. Car models: array of 0x540-byte records at
  0x4a4d44, name first.
- Driver record (0x8c bytes, array at 0x4a4c98; src/game/driver.h): car +0x40, colours 0x00RRGGBB +0x44/+0x48,
  JoyBinding[4] +0x4c (joy, index, kind byte +9: 0 axis / 1 button / 2 hat, negative byte +10), key[4] +0x7c
  (Accelerate, Brake, Steer left, Steer right; -1 = use the joystick binding; key[0] == 0 marks an AI driver,
  whose skill is kept in key[1]). In .drv files: car 0x44, colours 0x48/0x4c, keys 0x50, bindings 0x60.
- Driver options screen (src/game/driver_options.h): widgets at 0x4a4c8c — 4..7 control buttons, 8 driver
  dropdown, 9 name button, 10..15 colour sliders, 16 AI skill, 17 "no drivers" note. Preview colours are
  palette entries 192..199. Layout origin = round((screen - 80·font)/2), round((screen_h - 60·font)/2) via
  the CRT float→int64 helper 0x497df0 (argument in xmm0).
- MSVC library code used through src/bridge/msvc.h: std::string assign 0x404780/0x4033a0, ctor(const char*)
  0x404690, move ctor 0x404610, substr 0x4045d0, find(char) 0x477490, _Grow 0x4038a0, deallocate 0x403830,
  operator+(string, const char*) 0x4485b0; stringstream ctor 0x4704d0, dtor 0x46f910, str() 0x4704b0,
  `<< const char*` 0x477730, `<< string` 0x477980; vector<ListItem*> push_back 0x48aa40; operator new 0x496bdc,
  delete 0x496c1d.

- File class (src/game/file.h, 0x4020c0..0x402700): std::fstream (FILE* of the filebuf at +0x64) + path
  string +0xc0 + open mode +0xd8 + "truncate on close" (+0xdc flag, keep size +0xe0): close() of such a file
  reads the part to keep, reopens it with out|trunc|binary and writes it back. read/write return how far
  the position moved. filebuf::open is passed the leftover ECX (= the security cookie) as `prot`.
- Ghost car (src/game/ghost.h): two buffers of 6001 frames × 4 Vec3 (0x4a4c14/0x4a4c18); recording
  0x4a4c24 + count 0x4a4c28, best lap 0x4a4c1c + count 0x4a4c20. Files store every second frame packed
  (position ×150 int, two direction vectors ×120 signed bytes; the third vector is their cross product).
  Hot-lap records: 7 entries (times u16 0x4a4ed0, names 0x4a4ca0, car records 0x4a6b90, dates 0x4a4e58);
  entry 0 is never displaced. A single driver races a time trial.
- Main menu: track list data 0x4a4f14 (items carry their folder index at +0x1c), folder list 0x4a4f10;
  selected tracks are "\name" or "\folder\name". Message box 0x484120: ECX = redraw callback, EDX = text,
  stack: title, buttons (1 OK, 2 Yes/No; returns nonzero for Yes).

- Audio (src/game/audio.cpp, sound.h): device format 0x4a4c2c (SDL_AudioSpec, null while closed), 16
  channels {SoundRef*, float pos, loop} at 0x4ab068, 16 mix buffers at 0x4ab028. The callback 0x46f080 renders
  each *playing* channel into the next free mix buffer (buffer index = count of playing channels so far, not
  the channel index), sums those buffers with soft clipping, and memsets the stream when nothing plays.
  SoundRef = {Sample*, float rate, volume 0..128}; Sample = {data, length in int16 values, refs}.

- Settings (src/game/ini.h, 0x46f510..0x470370): gr.ini parsed into sections of typed entries (int,
  float, string; every entry also keeps its text). Section and entry arrays grow by 10 via array new.
  Lookups are case-insensitive (`_stricmp`). The loader trims trailing spaces of string values only.
- Widget +0x42 (int16 `variant`): 3 marks the highlighted menu entry; dialogs store their result (id & 1 of
  the button pressed) in the first widget's field. Message box widgets 0x4a517c, input box 0x4a5120.
- Name entry dialog (src/game/name_entry.h): driver 0x4a4c88, text 0x4a6b78, widgets 0x4a511c; the screen
  below is saved in g_menu_gui (main menu) or 0x4a4c7c (Driver options).

- Race flow (src/game/race_flow.cpp): game_resume continues by state (0 info box, 1..2 racing, 5 results,
  6/7 next race) and sets g_racing (0x4a4797: the race runs; 0 in menus and paused) and the ticks (0x4a47d8). Skip race finishes a race that has a
  result (single player with a lap done, or leader laps 0x4a47fc >= race laps 0x4a4760), otherwise saves
  the records and moves to the next track or the season results.
- File dialog (save/load game; widgets 0x4a4c70): 2 title, 3 file list, 4 name field (pos = text length).
  The saves folder ships with "Autosave".

- Track info dialog (right-click a track; src/game/track_info.h): track path 0x4a4c74 (60-byte heap copy),
  widgets 0x4a4c9c. Bug of the original, kept: Reset frees the track path and then reopens the dialog
  with the freed pointer, so the reopened dialog shows no track (name empty, 0 m).

## Harness facts

- The install has no options.set: every run starts with the race-option defaults unless a scenario puts
  tests/fixtures/options.set in place. GetCommandLineA returns `.\GR12.exe`: the game takes everything up
  to the last backslash as its folder (g_game_dir = `.\`); with no backslash it would scan before the
  string (it did, with the earlier `GR12.exe`, giving the folder `GR12.exe`).

- Deterministic mode: virtual clock starts at 1 s; +1 µs per clock read, +10 µs per event poll, Sleep/SDL_Delay
  advance by their argument, each SDL_Flip by 16.667 ms; the mixer runs synchronously at each flip.
  Boot reaches the main menu after ~13.4 s of virtual time (frame ~300).
- Main menu (640×480): driver list rows x=430, y=96+16k (clicking adds the driver; the list shifts up);
  track list first entry 116,288; Start 560,464; Options 240,464; Exit 80,464. The race starts after a key
  press on the info box — it must be held across a poll (`down`/`up` events, a `key` event is too short);
  Player drives with the arrow keys (273 up, 274 down, 275 right, 276 left). Escape in a race opens the pause
  menu, which uses keyboard focus (arrows, Enter, Escape).
- Scenario files (`tests/scenarios/*.txt`): `script:` events `ms:kind:args` with kinds click, rclick, mdown,
  move, mup (x:y), key, down, up (SDL keysym), wheel (4 up / 5 down), snap:n (writes headless_n.bmp), quit;
  `copy: from to, ...` (or `from -> to` for names with spaces) copies game files in the scenario's copy of the install before the run;
  `fixture: file, ...` copies files from tests/fixtures into it, `remove: file, ...` deletes files from it;
  `patch: file hexoffset hexbytes, ...` then overwrites bytes of a file.
- Shadow checking compares the first GRX_SHADOW_N (default 30) calls of each function, then every 97th call
  up to 4×N. Calls that touch files or the window cannot be forked and are left to the whole-run trace
  ("not checkable"; a function is dropped from shadow checking after 200 such calls).
- Uninitialized reads: the original sometimes reads uninitialized stack. Known case: the key-binding
  callbacks of Driver options (0x48bb70 Accelerate, 0x48bc30/0x48bd00/0x48bdd0 the others) copy a 12-byte
  joystick-binding local into the driver record (+0x4c..+0x57) even when a key was bound, and the record is
  saved to the .drv file — whatever an earlier call left at that stack depth ends up on disk. A rewritten
  function leaves different leftovers, so the checker zeroes 16 KB of dead guest stack after every hooked
  call in both runs (`GRX_WIPE`); for that the reference run is build/gr with `GRX_ORIG=all` instead of
  build/gr_orig, and `--selftest` checks that the two are identical (without wiping).
- `tools/check.py --coverage` builds build/gr_cov (clang source coverage) and reports, per file of src/,
  which lines no scenario executed; `--coverage-show src/x.cpp` prints the annotated file. Unexecuted code
  is unverified: add a scenario that reaches it.
- The guest arena is at host 0x10000000000 (macOS refuses fixed mappings at some lower addresses).

## Progress log

- 2026-11-18: repository set up; recompiled original runs deterministically (boot, menu, 5-car race:
  identical traces and files across runs). Vec3 (10 functions) rewritten: identical traces over a 40 s race
  (~650k calls), shadow-checked; a planted bug is caught by both the trace and the shadow check.
  Menu add/remove driver (0x476400, 0x4764f0) rewritten; identical in race and menu_drivers. They draw to the
  screen, so the shadow check skips them; the per-frame trace catches a planted bug at the removal frame.
- 2026-11-18 (cont.): GUI toolkit rewritten (state, push/pop, widget inits, list operations, all mouse /
  keyboard / wheel input handlers, the four widget draw callbacks), video and palette functions (13),
  draw_text, the main menu widget drawing. New scenarios gui_input, menus, lists (scrolling a long driver
  list), race_skip to results (complete race, keyboard-driven pause menu, results). All scenarios identical, no shadow
  mismatches; 91% of the rewritten lines are executed by the scenarios.
  list_insert, widgets_free (verified by the new `exit` scenario: Exit → No → Exit → Yes). The new
  driver_options scenario exposed the uninitialized-stack read above (stack wiping added).
  Driver options screen (10 functions); scenario driver_joy (patched .drv files with joystick bindings,
  AI skill slider). Scenario files gained `copy:` and `patch:` setup lines.
  Menu page frame (draw_page, shared by all menu screens), Advanced options page, main menu page, folder
  dropdowns, track selection (toggle, random selection, Start, Exit); the game's file class (10 functions,
  used by every loader/saver); ghost car recording, hot-lap records, ghost/replay file packing. New
  scenarios main_folders, main_random, time_trial (two time trials: the second loads the saved ghost).
  Settings file parser (13 functions, scenario ini_values: patched gr.ini with a float, a padded string and
  enough keys/sections to grow both arrays), keyboard/joystick helpers, name entry open/close, message box
  callbacks, track-file record readers, small constructors and accessors.
  Race flow (resume, skip race, replay reset), Driver options name button, file dialog list click. New
  scenarios race_skip (pause on the info box; End race moving on to the next track; Resume/End race on the
  results), race_skip_solo (End race after one lap of a time trial), save_game (overwrite Autosave).
  Pause menu open/close (engine and ambient sounds are stopped while paused and restarted silent and
  looping), main menu car dropdown (widgets 0x4a5178; picking saves the driver via 0x4872b0). New scenario
  car_pick.
  Track info open and Reset (scenario track_info).
  Start-light countdown (0x416bc0: every 70 ticks of the start phase; start light 0x4a4040, beep sound
  handle 0x4a4dc4, pitched up for the last light), event poll loop, widget layout↔screen scaling, record
  date file I/O, the unclipped text drawer with drop shadow (offset 0x4a4ee8), the loading screen, the
  F1 about box and F11/F12 screenshots (new scenario hotkeys), control_value (key or joystick input,
  0..100), replay frame file I/O (src/game/replay.h: 18-byte records). Renamed: 0x445bc0 draws one race
  frame (it is not "finish the race").
  Not executed by any scenario yet: replay_frames_read (loading a saved game: after saving from the pause
  menu the scripted session cannot reopen the pause menu), the joystick paths of control_value (the
  harness has no joystick).
  File dialog open (Load from the main menu / Save from the pause menu; titles are language entries
  90/91), its file list (saves/*, sorted) and its box; replay setup (rate and ring-buffer size from
  gr.ini; when the size is unchanged the original allocates new buffers anyway and leaks the old ones —
  kept) and the replay writer (header 0x50520124, chunks "PLR1".."PLR6", marked range only); terrain
  height (bilinear over the 64-wide height grid 0x4a47a0, units of 0.05); surface helpers (alpha test,
  convert-and-scale); the track description box (click on the author in Track info; scenario
  track_description); the .lng entry lookup; key → typed character (letters a..y only are upper-cased
  with Shift — 'z' is not, a bug of the original, kept); case-insensitive string compare and sort
  predicate (0x403eb0 is cdecl). New scenario load_dialog.
- Results of rewritten functions in XMM0: the shadow check compares the whole register, so adapters
  also reproduce the original's upper lanes (terrain_height: zero).
  Track loading step at the start of a race (loading screen, ghost and replay setup, timed races:
  length unit minutes → limit = length × 60 s, the start saved as "Autosave"; new scenario timed_race),
  season end (players ranked by points 0x4a47a4, equal points share a place; each driver's season count
  and place counter updated and the .drv saved; new scenario season_end), driver files (drivers/<name>.drv:
  version 92 + record; Driver +0 seasons, +4 places[6]; a car index ≥ the car count 0x4a4c90 resets to 0
  on load), race options (0x4a4d48, 0x68 bytes, src/game/race_options.h; options.set = version 'J' +
  fields; the three 22-character strings are followed by 0 bytes that the reader skips with seeks).
  Driver record reader/writer (each binding's pad byte: written as 0, skipped on read), the driver
  list fill (one 0x8c-byte Driver per list item, item +0x20), list_directory 0x404020 (FindFirstFile
  over dir + "\*.ext"; mode bit 1 files / bit 2 folders; names lose ".ext" after a case-insensitive
  check; a name longer than max_len becomes its 8.3 short name; sorted with less_nocase).
  Program entry: win_main 0x471810 (WinMain, ret 16: game folder from the command line), game_main
  0x4723d0 (srand(ticks), race options, SDL_Init(0x230), caption, game_init 0x478d40, display modes,
  joysticks, event loop with SDL_Delay(racing ? 1 : 25), cleanup 0x47cf50, options saved, SDL_Quit),
  race_options_load 0x472050 (defaults when options.set is missing or not version 'J'; the screen size
  0x4a4d3c/0x4a4d1c comes from options +0x10/+0x14). Deleting: driver files (Driver options → Delete
  driver; the following records of g_drivers move down; new scenario driver_delete), track folders (the
  Delete next to the folder tab: exactly one folder ≥ index 2 marked; SHFileOperation FO_DELETE flags
  0x614; new scenario folder_delete), new scenario options_file (reads a saved options.set).
  Track folders: list_insert_sorted 0x472760 (the "binary search" only tests differs_nocase, so new
  texts end up appended unless an equal one is met — original behaviour kept), folder_lists_fill
  0x472e10 (subfolders of tracks + entries 128/129 on top; also fills the Move-to list), and
  track_list_refresh 0x4752f0 (folder marking with Shift ranges from anchor 0x4a4068 / Ctrl; the marked
  folders' names and indices go by value to 0x472830, which fills the track list; folder caption 130
  when several are marked; "Select n" slider range; "<n> selected" caption).
- track_list_fill 0x472830 (fastcall: ecx list widget, dl with_default; names and folder indices as two
  vectors by value on the stack, freed by the callee): lists tracks\*.trk (folder index 1) and
  tracks\<folder>\*.trk for each folder into a vector of {string, folder index} (0x1c bytes, push_back
  0x476f30, sort 0x477c50 with the case-insensitive less 0x403eb0, destroy 0x477da0, free 0x4776c0), then
  list_insert_sorted each and stores the folder index in the item. The install has no tracks directly in
  tracks/, so scenario folder_select copies one there (`copy:` accepts `from -> to` for names with spaces).
- replay_save_start 0x48d7e0: per player copies car state (0x194-byte records at 0x4a5210) +0x58 position,
  +0x7c orientation (3×3), +0xa0, and one float per wheel at +0xec + 0xc·(layout + k), layout = byte +0xbd
  of the car's model record (6 × 0x540 at 0x4a9050), to 0x4ab578 (0x44 bytes per car); keeps g_game_state
  in 0x4a51e0. replay_restore_start 0x48d8e0 copies it back and recomputes the wheels: world = position +
  axis0·x + axis1·y + axis2·z (single precision, left to right) from the model's wheel offsets at +0x504,
  then puts the saved y back.
- dopt_create_driver_clicked 0x48cb10 (callback of the Create driver button, also called by the Options
  button 0x4766e0 and by the dialog's OK 0x48cd20 after an empty name): hides Driver options widgets 0..19,
  shows 20..23 (20 name field, 21 human/computer toggle by id, 22/23 buttons), dialog box {20,19,60,41}
  with title 70 and captions 81/82 at x 0x19, y 0x1b/0x1f; clears the name, focuses it (gui_set_focus,
  inlined in the original), redraws everything.
- dopt_create_driver_done 0x48cd20 (OK id 1 / Cancel id 0 of the Create driver dialog): OK refuses an
  empty name (text 85) or one already in the dropdown (list_find, text 86) with a message titled text 0,
  then reopens the dialog. Closing: widgets 0..19 shown again unless their variant ≥ 2, 20..23 and the
  dropdown hidden, dialog area erased (colour 100), the R/G/B captions (0x49d82c/30/34) redrawn at x 37,
  rows 13/16/19/23/26/29. OK then list_add + a new g_drivers record: the array is reallocated (malloc
  n·0x8c, memcpy, free); seasons/places/unknown fields 0, car 0, colours 0xffffff / 0xff0000, keys 273 274
  276 275 (human) or 0 100 0 0 (computer, skill 100); the joystick bindings are left as malloc returned
  them. The item gets an operator-new copy; the new driver becomes selected. Scenario driver_create.
- src/orig/text.h removed: its draw_text_at wrapper was stale (0x484cf0 is rewritten).
- race_render 0x445bc0: 0x46c8d0(2,1), lock, the view function pointer at 0x4a4d20, 0x419840(0), per player
  0x41c460(p) and while racing the engine sound: volume -10000 - (int)(0x4a47f4·-0.93) millibel (the
  inlined sound_set_volume), rate = (float)(int)(pitch·k) / 22050 with pitch = (|speed|·4 + 50) / model
  +0xa8, speed = car +0xa0, k = 5000 when 0x4a4ecc > car +0x5c (y) else 10000; unlock; frame_present.
- pause_menu_draw 0x480dc0 (frame, title text 87, separator line); main_menu_show 0x480ff0 (race length unit
  caption in widget 18 from texts 1/2, 3/4, 165/166; page, track list; pops g_menu_gui or opens screen 1).
- Track files: track_records_write 0x496890 (cleared records into tracks\<name>.trk, cut after them),
  track_records_save 0x43ff50 (after a race into tracks<selected path>.trk, plus the ghost with one player,
  then track_checksum_write), car_models_write 0x442fa0 (field table of the 0x540-byte model record, two
  pad fields), track_checksum_write 0x4401d0 (marker 0x4752 ^ seek-ok at end-20, 16-byte checksum seeded
  from 0x4a4030: sum[i&3] += (byte+1)·(i+1) over [start, end); then marks the file to be cut after it,
  without resetting the stream state).
- set_player_driver 0x487020 (palette 192 + 8p: per colour 3/4 shade (signed /4), the colour, shade +
  0x404040, tint (r/2, g/2+64, b/2+128) via _ftol2); frame_present 0x46c9c0 (start lights: three discs
  each of radius 1.8/1.5/1.0/0.3 font units, colours 0xf1 + lit 0xf4/0xf3/0xf2 or unlit 1/10/0x19; then
  blit_surface(1, 0) and SDL_Flip); draw_disc 0x419560 (fastcall: cl colour, dl always, xmm1 radius; stack
  x, y, z; rows from the 100-entry half-width table at 0x4a4f08, depth test against the uint16 buffer at
  0x4a4784 with (int)(z / 0x4a402c); a row index outside 0..99 ends the disc).
- event_handle 0x471b80 (global keys: F12/F11 screenshot_take 0x471e50, Alt+F4 quit, Alt+Enter fullscreen
  toggle with 0x46b580; GUI dispatch when not racing). screenshot_take: screens\shotNNNN.bmp, the first
  number (counter 0x4ab730) whose file cannot be opened; the name comes from the original's sprintf
  0x46f320 (static buffer 0x4ab128). Scenario screenshot.
- main_menu_right_click 0x483790: driver list → name_entry_open (driver statistics); the name column is
  widget 10 (x 256, y 88, 6 rows of its height) → player p's statistics, an empty row ends the handler;
  track list → track_info_open("\name" / "\folder\name"). Scenario driver_stats.

### game_free, draw_dialog, message_box, options_open, dopt_open
- 0x47cf50 `game_free(everything)` (ecx = 0 means everything): drivers, track list, gui, the menus' widget
  arrays (0x4a4f0c x30, 0x4a5124 x17, 0x4a4c80 x21, 0x4a4c8c x24, 0x4a5178 x2, 0x4a4df4 x6, 0x4a4c70 x5,
  0x4a517c x3), then always the cars / font / disc widths / surfaces / key state / buffer 0x4a4cfc and the
  sound handles; everything also closes audio and frees sounds and surfaces a second time.
- 0x47d6f0 `draw_dialog(box, title, labels, n)` (ecx, edx, 2 stack, caller pops): frame in colour 0xf0
  border g_menu_border, fill 100, separator bars 0x5a 4 units from the bottom / top, title colour 0x73.
- 0x484120 `message_box(redraw, title, text, buttons)` (ecx = redraw fn, edx = text, stack title, buttons):
  wraps text into <= 20 lines of <= 40 chars (41-byte slots; break at '\n', else the last ' ' '.' ',' '-'
  or at '!'), PageLabels x 0x14, y 24-n+6+2k, colour 0xf0 (low byte only: the upper bytes keep heap
  garbage, which the heap trace sees), box {16, 24-n, 64, n+36}; buttons at y (n+32) units; runs its own
  events_poll loop until a button frees 0x4a4f1c / 0x4a4c48; returns widget 0's variant. 0x4a4c94 holds
  redraw while open.
- 0x481260 `options_open` (stdcall 1 arg, unused): first time saves the menu gui and loads g_race_options
  into the Options (0x4a5124) and Advanced (0x4a4c80) widgets; tyre/fuel/... toggles take id = value+2 and
  caption lang 5/6; points sliders = b02[0..6]; w1e/2.56 rounded = advanced slider 6.
- 0x481910 `dopt_open` (stdcall 1 arg): builds the driver dropdown and g_drivers the first time: players
  (or the saved 0x4a4c30 / names 0x4a5128 when 0x4a4048 >= 0) first, then the driver list.
  Scenario driver_options_players covers the players' branch; the saved-drivers branch is not covered yet
  (0x4a4048 is written through a pointer in 0x481d00).

### Checksums, constructors, race results, replay file, shading, objects, race exit
- 0x43d120 `track_file_checksum(name, sum[4])` (ecx, edx; returns bytes summed): sums bytes after the size
  field as track_checksum_write does but from {1,1,1,1}; 0 if unreadable or version > 0x124.
  0x43d070 `track_checksum_matches(cstr, expected[4])`.
- 0x4060b0 CarState constructor (static initializer 0x401000, six records), 0x478c50 car model constructor.
- 0x4196d0 `undraw_disc(x, y, z, radius in xmm1)`: like draw_disc, but restores the unshaded track image
  0x4a476c where the depth buffer is farther than (z - 1) / g_depth_scale; scale g_start_light_depth / z.
- 0x43ca30 `race_record_results`: Driver +0x1c points, +0x20 races, +0x24 race places[6], +0x3c fastest
  laps; CarState +0x10 race time, +0xd4 laps, +0xdc best lap; 0x4a47c0 finish order, 0x4a4770 fastest lap.
- 0x48f0f0 `replay_read(GameFile*)`: header version/"RP", rate (from 0x104), frame count, chunks
  "PLR1".."PLR6"; frames' b11 is stored decremented.
- 0x43bf20 `shade_tables_init`: g_shade_light 0x4a4748 / g_shade_dark 0x4a478c, 48 colour groups.
- 0x482fd0 `stats_reset_clicked` (Driver stats dialog: lang 125 title, 126 question).
- 0x445df0 `race_render_dimmed`: backdrop of in-race boxes; grey = 1 - trunc((0.6g + 0.3b + 0.1r) * -0.07).
- 0x43ae60 `object_collide_plane(i, p, n, thickness)` (ecx = index, 7 stack floats, caller pops):
  objects at 0x4a4744, 0x34 bytes: +4 position, +0x24 velocity, +0x30 radius.
- 0x43b7c0 `race_free`: frees ghost buffers, replay, stops sounds, frees 14 track data blocks, main menu,
  cursor on, 0x46b580.
- 0x476710 `track_new_folder_clicked` (stdcall button; ecx passed through to the input box 0x4846a0,
  which takes ecx, edx = text, stack = title, returns a C string or 0): lang 137 title, 138 prompt,
  139 failure. Scenario folder_create_twice covers the failure path.
- 0x43abf0 `object_collide_line(i, p, d, thickness)` and 0x43a910 `object_collide_sphere(out, i,
  other_mass in xmm2 (stack slot +8 unused), p, radius)` (returns the impulse on the sphere:
  100 * mass * n * closing * |other| / (mass + |other|), only for other_mass < 0). Object +0x20 = mass.
  The push-out of all three collisions uses the unnormalised offset times the radius.
  Not covered yet: the plane normal flip and the immovable sphere (other_mass >= 0) branches.
- 0x481650 `advanced_clicked(button)`: button id 3 = from the pause menu (saves the GUI in 0x4a5118,
  hides Advanced w1 and Driver options w1, w17, w18 with variant 2, loads the options incl. text[2]
  into a[20]); else gui_close. Shared loader adv_widgets_load with options_open.
- 0x43fc60 PitBox constructor (zeroes +0x4c..+0x180).
- src/game/fmath.h: guest_sqrtf (0x405e60).
- 0x485070 `settings_load`: gr.ini defaults then integer values (section/key strings at 0x49d5ec..);
  screenshot size capped at 2560 x 1920; ends with ini_clear.
- 0x441f50 `car_models_read(f, models, count)` (1 stack arg, caller pops): mirror of car_models_write;
  padding skipped with file_skip (each good skip counts 1), byte +0xc cleared.
- 0x474f40 `track_list_clicked(list)`: Ctrl = only the clicked track, Shift = range from anchor
  0x4a406c (else the list's previous selection), then "n selected" caption. Key state through the
  inlined keyboard_snapshot. SDL 1.2 key codes in input.h (SdlKey).
- 0x47db80 `options_draw_page`: title lang 35, captions lang 39..52 (rows 7..51, the 7th colour 0x73).
  (0x47e000 was already rewritten as adv_draw_labels: check every `REWRITE(` in src, not only src/hooks,
  before starting a function.)
- 0x43bbe0 `race_controls_init`: volumes from race options b20 (engine factor g_unknown_4a47f4 =
  log10(v*1e8+1)*1000) and b22 (start beep and sounds 0x4a4e00, 0x4a4db0 through sound_set_volume);
  copies each human driver's keys to car +0x48 and joystick bindings to car +0x18, an AI's skill to +0x4c.
  0x405e20 = float log10 (fmath.h guest_log10f). Scenario race_human covers the human branch.
- 0x471400 `lang_load(name)`: lang\<name>.lng; defaults from the pointer table 0x49b5a0 (169 entries).
- 0x416c50 `terrain_sample(p, normal)`: clamps p, sets p.y to the height and the normalised surface normal
  (slopes interpolated from neighbouring grid differences; reduced form in the last two columns/rows,
  not reached by any scenario).
- 0x41c460 `hud_draw_gauges(player)`: three pedal bars (car +0xde, +0xe2, +0xe0) in colours f7/f4/f5 on
  an f1 frame; memset via the CRT (0x497d10). race_human scenario: a track must be selected first or Start
  shows the "select players and tracks" message.
- 0x43bbe0 is not called at race start: only from the driver-options close handler (0x481d00) over a
  paused race, re-applying controls (race_finish and race_human reach it through pause > Options > Escape).
- 0x482940 `file_dialog_closed(button)`: replay dialog OK/Cancel. Main menu: load (0x440a70) if the name is
  listed, else lang 98+name+99 (title 100); load failure lang 101. Pause menu: save (0x440520), asking
  lang 95+name+96 (title 97) when it exists; returns to the pause menu, replay marks reset to -1.
  MsString::concat(out, a, b) for 0x404d80 (const&, const&). Scenario load_missing.
- 0x476ba0 `delete_tracks_clicked`: confirm (title lang 146; one track: 102 + "'" + name without its
  leading '\' + "'?", several: 148 " n " 149; none: 147), unlink tracks\<name>.trk, lang 150 on failure;
  clears the selection, refills the list. Scenario delete_tracks.
- 0x48f4c0 `file_dialog_delete_clicked`: replay Delete; question 102 + " '" (0x49d804, 2 chars) + name +
  "'?", title 103; unlinks saves\<name>.gam. Scenario save_delete.
- file_dialog_closed: the original frees the joined question string only after the message box; freeing
  it earlier changed the heap ("img" hash differs while the screen hash matches: think heap order).
- 0x443360 `car_states_write(file)`: per car, a field table (offset, size) of CarState with zero padding
  bytes; returns the total written. Deferred: 0x483bc0 (control capture, joystick polling), 0x44e910 and
  0x40ef10 (per-pixel renderers, float args in xmm registers).
- 0x440520 `game_save(name)` (replay.cpp) and 0x440a70 `game_load(name)` (game_load.cpp): .gam version
  0x124; load refuses > 0x124 and 0x120 (returns -1, the caller shows lang 101). Load keeps the options-
  screen fields of the race options (+0x0e, +0x10..0x18, +0x1a..0x1c, +0x1e..0x39), merges the drivers
  into the driver list, re-checks the current track's checksum (0x43cd50 searches for a copy; none:
  "\()" and no replay), then repeats race-start setup (buffers, random table, car palette colours via
  car_shades, engine sounds at attenuation 10^5, effect volumes). The original returns esi: 0 / -1.
  Scenario load_game (save, End season, Load).
- sound_set_attenuation(handle, e): volume 128 / 10^e; sound_set_volume(millibel) is e = mb / -2000.
- 0x442390 `car_states_read`: same field table as car_states_write, padding skipped (file_skip).
- 0x4883a0 `engine_sounds_load(set)` (race_flow.cpp): per player sounds\<set>\<car name>.wav (0x49db24),
  else sounds\<set>\engine.wav (0x49db2c), else sounds\default\engine.wav; same car as an earlier player:
  shared sample; fallback players share the first fallback's. Skipped without an audio device.
- 0x43cd50 `track_find_by_checksum(name, sum, out)` (ghost.cpp): 1 if tracks\<base> or the first
  tracks\<folder>\<base> has the checksum; only reached when a saved track file changed (no scenario).
- 0x475b00 `move_to_clicked`: closes the folder drop-down (widgets 27, 28 hidden again), moves the
  selected tracks' files to tracks\<folder>\ with MoveFileA (lang 145, title 144 on failure), then the
  main page is redrawn (main_menu_draw_page, inlined in the original). Scenario move_to.
- 0x4846a0 `input_box(text, title)` (message_box.cpp): like message_box with at most 16 text lines, an
  empty 20-character field focused (gui_set_focus(2)) and the main menu (0x47da90) repainted below
  afterwards. Returns the field's text (guest C string) for OK, 0 for Cancel. The original is fastcall
  with an unused ecx.
- 0x4853a0 `font_build()` (text.cpp): font size, menu border and text shadow from the resolution, then
  font.bmp (160 glyphs of 20x20, 1 bpp) box-filtered to the font size (a pixel is set at >= 1/4 coverage).
  Returns 0, -1 (no font.bmp) or -2 (image size not 16000); the startup code checks for -1 / -2.
- 0x40f2f0 `track_redraw_tile(tx, tz)` (render.cpp): repaints the disc of a track tile after its terrain
  type changed. Camera globals 0x4aaff8 (down), 0x4ab004 (forward), 0x4ab010 (eye), 0x4ab01c (right).
- 0x418fd0 `undraw_triangle(9 floats)` (render.cpp): restores the track image over a projected triangle
  (corners sorted by y with `>` tests, NaN cross product takes the cross >= 0 branch as in the original).
- 0x44f470 `water_pixel(n, q, x, y)` (render.cpp): per-pixel callback of the water plane (pointer at
  0x4a4738). Wave normal from a 16x16 patch of g_random_table (C `%` on possibly negative tile
  coordinates, as in the original); reflection pointing down darkens what is there (/5 + 0x78, or low
  bits 3 for water shades), otherwise sky shade 0xf8 - (int)(ry * -6.5 * ry) and the depth is written.
- 0x407520 `replay_controls()` (replay.cpp): the replay viewer's keys (Escape, C range marks + Save
  dialog, Page Up/Down hold, keypad +/- ratio, Left/Down/Right, Home/End), the engine sounds following
  the cars (muted by setting SoundRef::volume directly, as 128 / powf(10, 5) + 0.5 = 0) and the HUD texts
  (formats at 0x49cc98 / 0x49cca4 / 0x49ccb4 via 0x46f320). Scenario replay_keys.
- 0x43b080 `race_start()` (race_flow.cpp): the same set-up as game_load (engine sounds, volumes,
  alloc_race_buffers, now shared, shade tables), plus copying each player's car model and clearing the
  season points; ends with race_load_track (0x43ba10). The 0x4a4774 records are zeroed by a constructor
  loop and then memset in the original; only the memset is kept (same memory).
- `guest_powf` (fmath.h) wraps 0x48f860 (float pow through the CRT's double pow).
- 0x4473e0 `season_standings()` (race_flow.cpp): between races. No points yet: next track at once.
  Otherwise the dimmed race view gets the track, race length, lang 13-15 + "<race>/<races>", and the
  players with points (stable insertion sort; place boxes where the points drop; cars via 0x443cc0 with
  the view vectors passed as float bit patterns; names; points). Scenario standings (1-lap race to the
  end, Enter to the standings, Enter to the next race).
- 0x44e120 `triangle_for_each_pixel(color, a, b, c, wa, wb, wc)` (render.cpp): calls the function in
  0x4a4738 (g_pixel_fn: water_pixel, 0x44ec90, ...) for each pixel of a projected triangle with the
  unit normal, wa, x, y, color. The flag picking the long edge's side is whether (top, mid, bottom) is an
  odd permutation of the argument order.
- 0x44ec90 `terrain_pixel` and 0x44e910 `flat_pixel` (render.cpp): the other two per-pixel callbacks
  of triangle_for_each_pixel. Both cut the view ray with the triangle's plane and depth-test against
  the 16-bit depth buffer. terrain_pixel takes the slope from the 64x64 height map (with the original's
  one-sided border differences, including the odd low-z one) and the terrain type from the nearest track
  tiles (equal types pooled); flat_pixel shades the given colour (terrain colours like the track,
  0x98..0xa7 with noise, others up to 3 steps darker).
- 0x47f3a0 `track_info_draw` (track_info.cpp): the Track info dialog: 12 captions (lang 8, 118, 124,
  127, records "%-17s %2d.%02d", length "%d m"), the author button (widget 2, hidden without an author,
  width from the label), preview via blit_background, a car per set record (player -1 = record colours).
- 0x43c130 `race_grid_setup` (race_flow.cpp): laps from the race length (km: ceil via 1 - (int)(-1000
  len / metres)), grid order (random / player order / season points), and each car's start state:
  controls copied from the driver, the grid slot from track objects 0..5, orientation from the ground
  normal (terrain_sample) and slot heading, wheels put on the height map.
- 0x447c50 `season_end` (race_flow.cpp): after each race. Unless the season's last race scored points,
  race_free. Otherwise the final standings over a grey-shaded copy of the race (palette mapped by
  0.6 g + 0.3 b + 0.1 r), ranked as season_standings; palette entries 0..29 darkened, 30..34 fixed,
  the season-end sound played.
- car_physics.cpp: the car collision routines. The car is an ellipsoid with the model's half sizes
  (+0x10). 0x42d3d0 `car_sphere_collide` (returns the impulse) and 0x42de90 `car_cylinder_collide`
  (around a line; only the normal differs: the axis part is removed) share `contact_response` (their
  float operations are identical up to commutativity). 0x42ea10 `car_plane_collide` (slab of half
  thickness t; the normal is flipped to face the car) has its own operation order (e.g. energy f2*s*s,
  lift dot*(s*normal)) and, for the ground, spreads the lift over the wheel corners and lifts wheels
  below the plane. Hard hits (energy > 50) go to the impact list g_unknown_4a4774 (0x1c-byte records,
  ring of g_race_options.w1e) and add damage (b0b * energy over 5000). Callers: 0x420560 (track
  objects 6..), 0x407cd0 (ground and borders). Scenario crash drives into the objects.
- Scenarios added: standings, track_records (Track info with records), km_race, crash.
- 0x48e220 `replay_advance` (replay.cpp): decodes the four recorded frames around g_replay_pos
  (0x12 bytes each: position /150, two directions /120 normalised, their cross product, wheel heights
  /16, one signed byte for car +0xa0); the centre follows a cubic curve (tangents (p2 - p0) / 6), the
  orientation rows, wheel heights and +0xa0 blend linearly; wheels are placed by the pose with the
  recorded height. Then g_replay_pos += speed * step, clamped; returns hundredths shown (-1 - that at
  the end, -1 before the start). The rewind-past-start branch is not covered yet.
- 0x481d00 `adv_options_done` (advanced_options.cpp): OK (button variant 1) / Cancel of Advanced and
  Driver options. Applies the widgets to g_race_options; a new display mode or colour depth autosaves a
  running race, rescales every screen's widgets (layout <-> screen around the reload) and reloads
  everything; else a new language reloads everything, a new sound set reloads the effects and engine
  sounds, a new palette calls 0x46b580. Then writes Driver options' working copies back (on OK), saves
  each driver, re-matches the race's drivers by name (closing gaps) and returns the others to the main
  menu's list. Scenarios adv_apply (menu: sound set, language, 800x600) and pause_options (over the
  pause menu: sound set; language -> autosave/reload/load). The palette branch needs a second .pal.
- 0x47e440 `dopt_draw_page` (driver_options.cpp): captions (lang 73..79, 89; slider letters), the car
  preview, the "new driver" dialog (lang 70, 81, 82).
- 0x4875b0 `sound_effects_load` (race_flow.cpp): skid/damage/ding/yeah/fuel .wav from the set, else
  default. Globals renamed: g_skid_sound 0x4a4e04, g_damage_sound 0x4a4dbc, g_cheer_sound 0x4a4e00,
  g_fuel_sound 0x4a4db0 (g_start_sound 0x4a4dc4 is ding.wav).
- 0x443cc0 `car_preview_draw` (render.cpp): projects wheels and vertices, shades vertices by
  normal . view ray (only when facing: s*s*2.99), draws triangles by mean depth interleaved with the
  wheel discs far to near. Shades of vertices behind the camera are 0 here (stack garbage in the
  original; never reached by the menu views).
- 0x4180f0 `shaded_triangle` (render.cpp): back-face test, depth = mean z / (scale * 3), shade as a
  screen plane (x*A + B + y*C), rows split at the middle vertex; the vertex order comes from the
  original's comparison tree (ties decide which vertex is "top"), the short edges are on the left for
  the even orders. Edge x = to_int((row - y_start) * slope + x_start) + 1 from the edge's upper vertex.
- 0x46b580 `palette_reset` (video.cpp): palettes\<name>.pal (entries 0..191, 240..255) or the built-in
  palette (kRamps table, darker copies, fixed colours). Scenario palette_fallback (options.set naming a
  missing palette, then Default picked in Advanced options).
- 0x4170a0 `box_draw` (render.cpp): a box (centre +- h +- a +- b) projected through the race camera,
  ten triangles in colour 0xa8 (no -b face). Shadow-checked in crash (25.7k calls, 0 mismatches).
- 0x444ae0 `race_info_box` (race_flow.cpp): the box before the start; race_render's first half is now
  `race_draw_scene()` (shared), the grey backdrop table `grey_table()` (shared with race_render_dimmed).
  One player: lang 120/121 and the lap record (slot 1); more: lang 7/8, the distance and the race
  record (slot 0). Record holder's car in the holder's colours if racing, date by GetDateFormatA.
- 0x47fc80 `driver_stats_draw` (name_entry.cpp): the stats dialog; rows via MsStringStream (new helpers
  width/left/right/put_uint). Heap order matters: the stream isn't reset after the last row, and the
  title is passed to draw_dialog as a copy built from title.c_str() (an extra 32-byte allocation that a
  later click otherwise lands in differently). Debugging tip: dump the guest heap and the allocator's
  free-list heads at the first differing frame, then log gmalloc/gfree around the suspect call.
- 0x41f3c0 `car_car_collide` (car_physics.cpp): two ellipsoid cars; normal from both unit-sphere
  directions, contact points per car, push apart by mass share, impulse 5 - 1.5 vn with friction limited
  to sqrt(-1.5 vn); `record_impact` (shared by both cars) as contact_response. Called for every pair.
- 0x40dbe0 `lap_line_crossed` (lap_line.cpp): lap time/count, running order (order_at(k) reads
  g_finish_order[k - 1], the original's indexing), the lap counter and the player's HUD boxes, records
  (slot 0), finishing. Uncovered: the "F" fuel warning (needs fuel consumption on: +0xe0 is the fuel,
  drained while driving) and a finisher's new fastest lap redrawing another holder's time.
- Scenarios crash_damage and race_fuel_damage: fixture options with damage (b0b, options.set offset 0x0b)
  and b0a (offset 0x0a) raised; they cover the hard-hit damage lines of contact_response.
- 0x4061c0 `race_loop_step` (race_loop.cpp): the fixed-rate tick loop and game state machine (0 wait,
  1 start lights / false starts, 2-3 racing, 4 results, 5-7 standings and next track, 8-9 season-end
  fireworks, 10 replay, 11 leaving the replay), then one redraw. The race tick locks surface 2 (the
  background), the redraw blits it to the back buffer and locks surface 1: an early version locked 2
  there and every race's screen hash differed from the first race frame (image hash still equal).
  Scenarios replay_help (F1 key help in the replay) and race_timeout (an idle human: the race ends
  6000 ticks after the winner's crossing) cover the last branches; the info box needs its key after
  ~23 s when the track is picked late.
- 0x446010 `race_results_draw` (results.cpp): the results box. One player: the track's lap records with
  place markers (drawn straight into the pixels, memset rows), the holder's car in the player's colours
  when the names match (_stricmp), then the race's fastest lap. More players: the finishing order with
  the winner's average speed, the gap or laps behind, then the fastest lap. Two guest strings as in the
  original: a scratch string emptied after each use and the right-hand column string, which is only
  re-assigned (its buffer kept) and freed after the frame is shown. Uncovered: the solo fastest lap
  that is not a new record but places among the stored records (needs a records fixture with a faster
  lap already set). `race_length_text` (race_flow.cpp) is shared with the info box.
- 0x483bc0 `wait_for_control` (driver_options.cpp): the bind-a-control prompt and poll loop. The key path
  is covered (driver_options binds W); the joystick paths cannot run: the harness's SDL shim reports no
  joysticks (0x470e70 `joystick_poll` likewise only runs its no-joystick path).
- 0x48df20 `replay_record`: its countdown is a function-local static; `guest::static_init` reproduces
  MSVC's thread-safe first use (_Init_thread_header/footer, the guard against the TLS epoch).
- 0x40ef10 `wheel_spray` (wheel_effects.cpp): a wheel's water spray / dust shading over the frame. Not yet
  verified: no scenario drives a wheel through water (surface 3) or dust (surface 2); needs a scenario
  on a track with water or sand along the driven line.
- 0x407cd0 `car_step` (car_step.cpp): one car's physics step: mass and inertia from the fuel, wheel grip
  per surface (one player: the tile group's factor; more players: interpolated within the group), engine
  power reduced by tyre wear, controls (AI 0x40b890 still original, called with the skill in xmm3; the
  upside-down recovery; false-start throttle; human keys via control_value), fuel use, damage wobble and
  smoke, wheel spin, engine sound, the tyre friction-ellipse solve (bisection on k), steering torque
  (normal / arcade b1b), damping, per-wheel spray / dust / tile marks (tiles wear to 0x28..0x2f and
  heal), integration and the axis-angle rotation (sin/cos via 0x405e40/0x405e00), loose objects
  following the height map, air / water drag and buoyancy (splash particles), gravity, wheel points, then
  the ground and border planes, 0x420560 (objects, other cars; still original), the checkpoints (lap
  completion) and the pit gates (+0xd8). Shadow-checked on every call over the whole suite (88k calls,
  0 mismatches); one association slip found that way: the tyre impulse is vel + (C*slip + A*lat).
  Scenarios race_ohful / race_plot / race_rahli / race_surkit (AI races with a reversing human) add
  partial submersion, reversing and the flip recovery. Still uncovered: sand / water wheel surfaces
  (set only by 0x420560's sand and water objects), arcade steering, pit gates, AI skill > 100, an empty
  tank, worn-tile healing.
- `car_plane_collide` takes the surface kind (EDX: 0 wall, 1 ground, 2 sand, 3 water) and stores it per
  wheel; it was a bool written as 1, which would have lost sand and water once 0x420560 passes 2 / 3.
- 0x40b890 `ai_drive` (ai.cpp): the AI driver. Follows the racing line (aim point ahead by speed, offset
  sideways from traffic, steering at full lock towards it after compensating its own yaw rate), decides
  at the pit entry whether fuel or damage will last (or pits at random with fuel low), drives the pit
  lane, claims a free box (or its own with g_pit_shared), waits for the crew and leaves; when stuck it
  backs up (frames 140..199 of each 400) or drives on (200..399). The throttle compares the turn radius
  needed to reach the aim with the speed. Shadow-checked on every call (124k calls, 0 mismatches) before
  car_step was switched to call it directly. Scenarios pits_estio / _ohful / _plot / _rahli / _surkit
  (five AI drivers, high fuel use, two minutes) cover the pit lane, box claims and crew waits. Still
  uncovered: races in minutes (length unit 2), backing up towards a box that faces away, pitting for
  damage, the pit-lane aim wrapping past the racing line's end.
