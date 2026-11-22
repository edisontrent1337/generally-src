#include "game/track_info.h"

#include <cstring>

#include "game/ghost.h"
#include "game/menu.h"
#include "game/lang.h"
#include "game/menu_page.h"
#include "game/text.h"
#include "game/track.h"
#include "orig/render.h"
#include "game/video.h"
#include "orig/menu.h"

static constexpr uint32_t TRACK_INFO_DRAW = 0x47f3a0;  // track_info_draw, passed to message_box as the redraw

// Opens the dialog for track (a path below the tracks folder, starting with '\') if its preview loads.
void track_info_open(const char *track) {
    int ok = static_cast<int>(guest::call_regs(0x491870, guest::addr(track + 1), static_cast<uint32_t>(g_font_size * 20),
                                               static_cast<uint32_t>(g_font_size * 15)));  // load the preview
    if (ok < 0) return;
    if (g_info_track) {
        guest::call_cdecl(0x496bd7, g_info_track.a);  // free
        g_info_track = nullptr;
    }
    g_info_track = ptr32<char>(&guest::at<char>(guest::call_cdecl(0x496c14, 0x3cu)));  // operator new
    guest::call_import(IMP_strncpy, g_info_track.a, guest::addr(track), 0x3cu);
    g_menu_gui = gui_push();
    track_info_draw();
    gui_open(9, g_track_info_widgets.get(), 3, guest::at<float>(0x49dc7c) /* 1.0 */, 0, 0, 0, 0);
}

// Reset (after a Yes/No confirmation): the track's records become empty (times of 6000 frames), the ghost
// is reset and the track's record file deleted; the dialog is opened again.
void track_info_reset_clicked(Widget *) {
    if (!g_info_track) return;
    if (!message_box(TRACK_INFO_DRAW, &guest::at<MsString>(0x4a6758), &guest::at<MsString>(0x4a6770), 2)) return;
    guest::call_cdecl(0x497d10, guest::addr(g_record_date), 0u, static_cast<uint32_t>(sizeof(g_record_date)));
    guest::call_cdecl(0x497d10, guest::addr(g_record_car), 0u, static_cast<uint32_t>(sizeof(g_record_car)));
    guest::call_cdecl(0x497d10, guest::addr(g_record_name), 0u, static_cast<uint32_t>(sizeof(g_record_name)));
    for (uint16_t &t : g_record_time) t = kGhostFrames;
    ghost_init();
    guest::Local<MsString> path;
    path->init();
    path->assign(g_info_track.get(), static_cast<uint32_t>(std::strlen(g_info_track.get())));
    track_records_write(path.get());  // the cleared records
    path->destroy();
    if (g_ghost_buf_a) guest::call_cdecl(0x496bd7, g_ghost_buf_a.a);
    g_ghost_buf_a = nullptr;
    if (g_ghost_buf_b) guest::call_cdecl(0x496bd7, g_ghost_buf_b.a);
    g_ghost_buf_b = nullptr;
    ptr32<char> track = g_info_track;
    if (track) guest::call_cdecl(0x496bd7, track.a);
    g_info_track = nullptr;
    gui_close();
    main_menu_show();
    track_info_open(track.get());      // (the freed name: the original does the same)
}

// The track's description (read from the track file) in a message box titled with the track's path.
void track_info_description_clicked(Widget *) {
    if (!g_info_track) return;
    guest::Local<MsString> title, text;
    title->init();
    title->assign(g_info_track.get(), static_cast<uint32_t>(std::strlen(g_info_track.get())));
    text->init();
    const char *desc = g_track_description;
    text->assign(desc, static_cast<uint32_t>(std::strlen(desc)));
    message_box(TRACK_INFO_DRAW, title.get(), text.get(), 1);
    text->destroy();
    title->destroy();
}

// Draws the dialog (0x47f3a0): title "<lang 123> - <track name>", captions (lang 8, 118, 124, 127), the
// records that are set ("<name> <seconds>.<hundredths>"; entry 0 at the top, 1..6 in a list), the track
// length, the preview image and a small car for each record. Widget 2 shows the track's author if it
// has one (and is hidden otherwise).
void track_info_draw() {
    if (!g_info_track) return;
    guest::Local<MsString> tmp, title;
    MsString::concat(tmp.get(), &g_lang[123], 0x49d838u);  // + " - "
    // the name after the last '\\'; after Reset the dialog reopens with the freed name, which may have
    // none left: the original then passes address 1 (strrchr's null + 1), as here
    const char *slash = std::strrchr(g_info_track.get(), '\\');
    MsString::concat_move(title.get(), tmp.get(), (slash ? guest::addr(slash) : 0u) + 1);
    tmp->destroy();

    guest::Local<PageLabel[12]> labels;
    static const struct { int x, y, color, text; } kLabels[12] = {
        {0x28, 0x0d, 0x5a, 8},   {0x28, 0x15, 0x5a, 118}, {0x0f, 0x21, 0x5a, 124}, {0x0f, 0x29, 0x5a, 127},
        {0x28, 0x10, 0xf0, -1},  {0x28, 0x18, 0xf0, -1},  {0x28, 0x1b, 0xf0, -1},  {0x28, 0x1e, 0xf0, -1},
        {0x28, 0x21, 0xf0, -1},  {0x28, 0x24, 0xf0, -1},  {0x28, 0x27, 0xf0, -1},  {0x0f, 0x24, 0xf0, -1},
    };
    for (int i = 0; i < 12; i++) {
        PageLabel &l = (*labels)[i];
        l.x = kLabels[i].x;
        l.y = kLabels[i].y;
        reinterpret_cast<uint8_t &>(l.color) = static_cast<uint8_t>(kLabels[i].color);  // (low byte only)
        l.text.init();
        if (kLabels[i].text >= 0) l.text.assign(g_lang[kLabels[i].text]);
    }
    auto set_text = [](PageLabel &l, uint32_t s) { l.text.assign(&guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s)))); };
    for (int k = 0; k < kRecords; k++) {
        int time = g_record_time[k];
        if (time >= kGhostFrames) continue;
        set_text((*labels)[4 + k],
                 guest::call_cdecl(0x46f320, 0x49d83cu, guest::addr(g_record_name[k]), time / 100, time % 100));  // "%-17s %2d.%02d"
    }
    set_text((*labels)[11], guest::call_cdecl(0x46f320, 0x49d84cu, g_track_length));  // "%d m"

    Widget &author = g_track_info_widgets.get()[2];
    if (g_track_author[0] == 0) {
        author.flags |= WF_HIDDEN;
    } else {
        author.label.assign(g_track_author, static_cast<uint32_t>(std::strlen(g_track_author)));
        uint32_t w = (author.label.size + 2) * static_cast<uint32_t>(g_font_size);
        author.flags &= ~static_cast<uint32_t>(WF_HIDDEN);
        author.w = static_cast<float>(static_cast<double>(w));
    }
    guest::Local<int32_t[4]> box;
    static const int32_t kBox[4] = {10, 6, 70, 54};
    for (int i = 0; i < 4; i++) (*box)[i] = kBox[i];
    draw_dialog(*box, title.get(), *labels, 12);

    const int f = g_font_size;
    int x0 = layout_x0(), y0 = layout_y0();
    blit_background(static_cast<int16_t>(f * 20), static_cast<int16_t>(f * 15), static_cast<int16_t>(f * 13 + x0),
                    static_cast<int16_t>(f * 16 + y0));  // the preview
    lock_surface(SURF_BACK);
    auto car = [&](int k, int y) {
        draw_car_preview(0xffffffffu, g_record_car[k], g_font_size * 0x26 + x0, y, 120.0f);
    };
    if (g_record_time[0] < kGhostFrames) car(0, f * 17 + y0);
    int y = g_font_size * 25 + y0;
    const int step = g_font_size * 3;
    for (int k = 1; k < kRecords; k++, y += step)
        if (g_record_time[k] < kGhostFrames) car(k, y);
    unlock_surface();
    guest::call_stdcall(0x4970c6, labels.get(), static_cast<uint32_t>(sizeof(PageLabel)), 12u, 0x47db40u);
    title->destroy();
}
