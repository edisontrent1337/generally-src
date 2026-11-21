#include "game/name_entry.h"

#include "game/driver_options.h"
#include "game/menu.h"
#include "game/lang.h"
#include "game/message_box.h"

#include "game/menu_page.h"
#include "game/video.h"
#include "game/gui.h"
#include "game/text.h"

#include <cstring>

static constexpr uint32_t NAME_SCALE = 0x49dc7c;  // float constant of the original

// Opens the dialog for d with name as the initial text. The screen below is saved: the main menu into
// g_menu_gui if nothing is saved there yet (opened from the main menu), else into g_name_saved_gui.
void name_entry_open(Driver *d, const MsString *name) {
    g_name_driver = d;
    if (name != &g_name_text) g_name_text.assign(*name);
    if (!g_menu_gui)
        g_menu_gui = gui_push();
    else
        g_name_saved_gui = gui_push();
    driver_stats_draw();
    gui_open(10, g_name_widgets.get(), 2, guest::at<float>(NAME_SCALE), 0, 0, 0, 0);
}

// Closes the dialog and returns to the screen below it.
void name_entry_close() {
    g_name_text.clear();
    g_name_driver = nullptr;
    if (g_name_saved_gui) {
        gui_close();
        gui_pop(&g_name_saved_gui);
        g_name_saved_gui = nullptr;
        dopt_draw_page();
        gui_redraw_all();
    } else {
        gui_close();
        main_menu_show();
    }
}

// Driver options: the rename button opens the dialog for the driver selected in the dropdown.
void dopt_rename_clicked(Widget *) {
    ListData *list = g_dopt_widgets.get()[DOPT_DRIVERS].list.get();
    int sel = list->selected;
    if (sel < 0 || sel >= list->items.size()) return;
    name_entry_open(&g_drivers.get()[sel], &list->items[sel].get()->text);
}

// Driver stats: the reset button asks (Yes / No, lang 125 title, 126 text) and on Yes clears the
// driver's statistics (seasons, season places, points, races, race places, fastest laps), saves the
// driver file and reopens the dialog.
void stats_reset_clicked(Widget *) {
    if (!g_name_driver || g_name_text.size == 0) return;
    guest::Local<MsString> name;
    name->init();
    name->assign(g_name_text);
    if (message_box(0x47fc80, &g_lang[125], &g_lang[126], 2)) {
        Driver *d = g_name_driver.get();
        d->seasons = 0;
        for (int &p : d->places) p = 0;
        d->points = d->races = 0;
        for (int &p : d->race_places) p = 0;
        d->fastest_laps = 0;
        guest::Local<MsString> file;
        file->init();
        const char *s = g_name_text.c_str();
        file->assign(s, static_cast<uint32_t>(std::strlen(s)));
        driver_save(g_name_driver.get(), file.get());
        file->destroy();
        name_entry_close();
        name_entry_open(d, name.get());
    }
    name->destroy();
}

// The driver stats dialog (0x47fc80), for g_name_driver named g_name_text: title lang 115 " - " name,
// a table in a fixed font: the header (lang 111, 110), for 1st..6th place (lang 104..109) the season
// and race finishes, the totals (lang 112: seasons, races), then points (113) and fastest laps (114);
// two lines in colour 0x5a below the header and above the totals.
void driver_stats_draw() {
    const Driver *d = g_name_driver.get();
    guest::Local<MsString> name;
    name->init();
    name->assign(g_name_text);
    if (d && name->size != 0) {
        guest::Local<MsString> sep, t1, title;
        sep->init();
        sep->assign(&guest::at<char>(0x49d838), 3);  // " - "
        MsString::concat(t1.get(), &g_lang[115], sep.get());
        MsString::concat_move(title.get(), t1.get(), name.get());
        t1->destroy();
        sep->destroy();
        static constexpr int kY[10] = {0x11, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e, 0x21, 0x27, 0x29};
        guest::Local<PageLabel[10]> labels;
        for (int i = 0; i < 10; i++) {
            PageLabel &l = *page_label_construct(&(*labels)[i]);
            l.x = 0x17;
            l.y = kY[i];
            reinterpret_cast<uint8_t &>(l.color) = 0xf0;  // (low byte only, as the original)
        }
        guest::Local<MsStringStream> ss;
        ss->construct();
        guest::Local<MsString> tmp, empty;
        auto take = [&](int row) {
            ss->str(tmp.get());
            (*labels)[row].text.move_assign(tmp.get());
            tmp->destroy();
            if (row == 9) return;  // (the last row leaves the stream as is)
            empty->init();
            ss->reset(empty.get());
            empty->destroy();
        };
        const int32_t *v = reinterpret_cast<const int32_t *>(d);  // seasons, places[6], points, races, ...
        ss->width(20) << &g_lang[111];
        ss->width(14) << &g_lang[110];
        take(0);
        for (int k = 0; k < 7; k++) {  // places 1st..6th, then the totals
            const int lang = k < 6 ? 104 + k : 112;
            const int season = k < 6 ? 1 + k : 0, race = k < 6 ? 9 + k : 8;
            ss->width(12).left() << &g_lang[lang];
            ss->width(8).right().put_uint(static_cast<uint32_t>(v[season]));
            ss->width(14).put_uint(static_cast<uint32_t>(v[race]));
            take(1 + k);
        }
        ss->width(20).left() << &g_lang[113];
        ss->width(14).right().put_uint(static_cast<uint32_t>(d->points));
        take(8);
        ss->width(20).left() << &g_lang[114];
        ss->width(14).right().put_uint(static_cast<uint32_t>(d->fastest_laps));
        take(9);
        static constexpr int32_t kBox[4] = {14, 10, 66, 50};
        guest::Local<int32_t[4]> box;
        std::memcpy(box.get(), kBox, sizeof kBox);
        guest::Local<MsString> shown;  // (the original passes a copy made from title.c_str())
        shown->construct(guest::addr(title->c_str()));
        draw_dialog(*box, shown.get(), *labels, 10);
        shown->destroy();
        const int u = g_font_size, x0 = layout_x0(), y0 = layout_y0(), b = g_menu_border;
        fill_rect(0x5a, static_cast<int16_t>(u * 0x16 + x0), static_cast<int16_t>(u * 0x20 + y0 + b),
                  static_cast<uint16_t>(u * 0x24), static_cast<uint16_t>(b));
        fill_rect(0x5a, static_cast<int16_t>(u * 0x16 + x0), static_cast<int16_t>(u * 0x13 + y0 + b),
                  static_cast<uint16_t>(u * 0x24), static_cast<uint16_t>(b));
        ss->destruct();
        guest::call_stdcall(0x4970c6, labels.get(), static_cast<uint32_t>(sizeof(PageLabel)), 10u, 0x47db40u);
        title->destroy();
    }
    name->destroy();
}
