#include "game/driver_options.h"

#include <cstring>

#include "game/input.h"
#include "game/menu.h"
#include "game/menu_page.h"
#include "game/text.h"
#include "game/video.h"
#include "bridge/imports.h"
#include "orig/menu.h"
#include "orig/render.h"

static Widget &widget(int i) { return g_dopt_widgets.get()[i]; }

static void show(int i, bool on) {
    if (on) widget(i).flags &= ~WF_HIDDEN;
    else widget(i).flags |= WF_HIDDEN;
}

// The driver selected in the dropdown, or null.
static Driver *selected_driver() {
    ListData *d = widget(DOPT_DRIVERS).list;
    int i = d->selected;
    if (i < 0 || i >= d->items.size()) return nullptr;
    return &g_drivers.get()[i];
}


static void erase(int x, int y, int w, int h) {
    if (!g_back) return;
    guest::Local<SdlRect> r;
    *r = rect(x, y, w, h);
    sdl::FillRect(g_back, r.get(), 100);
}

// "Joy #1: Axis #2+", "Joy #1: Button #3", "Joy #1: Hat #0"
void joy_binding_name(const JoyBinding *b, MsString *out) {
    guest::Local<MsStringStream> ss;
    guest::Local<MsString> tmp;
    ss->construct();
    ss->put(0x49db3c /* "Joy #" */) << b->joy;
    switch (b->kind) {
    case 0:
        ss->put(0x49db38 /* ": " */).put(0x49db44 /* "Axis #" */) << b->index;
        ss->put(b->negative ? STR_MINUS : STR_PLUS);
        break;
    case 1: ss->put(0x49db38 /* ": " */).put(0x49db4c /* "Button #" */) << b->index; break;
    case 2: ss->put(0x49db38 /* ": " */).put(0x49db58 /* "Hat #" */) << b->index; break;
    default:
        tmp->init();
        ss->reset(tmp.get());
        tmp->destroy();
        *ss << &g_lang[L_UNASSIGNED];
    }
    ss->str(tmp.get());
    out->destroy();
    out->move_assign(tmp.get());
    tmp->destroy();
    ss->destruct();
}

// Closes the driver dropdown: shows the screen's widgets again (only those of the human or the AI page)
// and erases the dropdown.
void dopt_close_dropdown() {
    for (int i = 0; i < DOPT_COUNT; i++)
        if (static_cast<uint16_t>(widget(i).variant) < 2) show(i, true);
    show(DOPT_DRIVERS, false);
    Driver *d = selected_driver();
    if (!d || d->human()) {
        show(DOPT_AI_SKILL, false);
    } else {
        for (int c = 0; c < CTRL_COUNT; c++) show(DOPT_CONTROL + c, false);
    }
    if (g_back) {
        Widget &w = widget(DOPT_DRIVERS);
        int16_t b = static_cast<int16_t>(g_menu_border);
        guest::Local<SdlRect> r;
        r->x = static_cast<int16_t>(static_cast<int16_t>(guest::to_int(w.x)) - b);
        r->y = static_cast<int16_t>(static_cast<int16_t>(guest::to_int(w.y)) - b);
        r->w = static_cast<uint16_t>(static_cast<int16_t>(guest::to_int(w.w)) + b * 2);
        r->h = static_cast<uint16_t>(static_cast<int16_t>(guest::to_int(w.h)) + b * 2);
        sdl::FillRect(g_back, r.get(), 100);
    }
    gui_redraw_all();
}

// The lower part of the screen: the control bindings of a human driver, or the skill of an AI driver.
void dopt_show_controls(bool human) {
    Driver *d = selected_driver();
    if (!d) return;
    int f = g_font_size, x0 = layout_x0(), y0 = layout_y0();
    erase(f * 13 + x0, f * 33 + y0, f * 54, f * 17);
    lock_surface(SURF_BACK);
    int x = x0 + f * 14;
    if (!human) {
        for (int c = 0; c < CTRL_COUNT; c++) show(DOPT_CONTROL + c, false);
        show(DOPT_AI_SKILL, true);
        widget(DOPT_AI_SKILL).pos = d->ai_skill();
        draw_text_at(&g_lang[L_AI_SKILL], x, f * 37 + y0, 0xf0, false);
    } else {
        for (int c = 0; c < CTRL_COUNT; c++) show(DOPT_CONTROL + c, true);
        show(DOPT_AI_SKILL, false);
        for (int c = 0; c < CTRL_COUNT; c++) {
            MsString *label = &widget(DOPT_CONTROL + c).label;
            if (d->key[c] < 0) joy_binding_name(&d->joy[c], label);
            else key_name(static_cast<uint32_t>(d->key[c]), label);
        }
        draw_text_at(&g_lang[L_CONTROLS], x, y0 + f * 34, 0x73, false);
        for (int c = 0; c < CTRL_COUNT; c++) draw_text_at(&g_lang[L_ACCELERATE + c], x, f * (37 + 3 * c) + y0, 0xf0, false);
    }
    unlock_surface();
    gui_redraw_all();
}

// 0x00RRGGBB from three slider widgets
static uint32_t slider_color(int first) {
    return (static_cast<uint32_t>(widget(first).pos) * 0x100 + widget(first + 1).pos) * 0x100 + widget(first + 2).pos;
}

// Palette entries for a car colour c (set from the three sliders starting at widget `first`): a darker
// shade, the colour, the shade lightened, and a pale tint.
static void car_color_shades(uint32_t *pal, int first, uint32_t c) {
    auto ch = [](int32_t v) { return static_cast<uint32_t>(v * 3) >> 2; };
    uint32_t dark = (ch(widget(first).pos) * 0x100 + ch(widget(first + 1).pos)) * 0x100 + ch(widget(first + 2).pos);
    uint32_t r = c >> 16 & 0xff, g = c >> 8 & 0xff, b = c & 0xff;
    pal[0] = dark;
    pal[1] = c;
    pal[2] = dark + 0x404040;
    uint32_t tr = static_cast<uint32_t>(guest::to_int64(static_cast<float>(r) * 0.5f));
    uint32_t tg = static_cast<uint32_t>(guest::to_int64(static_cast<float>(g) * 0.5f + 64.0f));
    uint32_t tb = static_cast<uint32_t>(guest::to_int64(static_cast<float>(b) * 0.5f + 128.0f));
    pal[3] = (tr * 0x100 + tg) * 0x100 + tb;
}

// Stores the slider colours in the selected driver and updates the preview colours (palette 192..199).
void dopt_update_colors() {
    Driver *d = &g_drivers.get()[widget(DOPT_DRIVERS).list->selected];
    guest::Local<uint32_t[256]> pal;
    d->color[0] = slider_color(DOPT_COLOR);
    palette_store(*pal);
    car_color_shades(*pal + 192, DOPT_COLOR, d->color[0]);
    d->color[1] = slider_color(DOPT_COLOR + 3);
    car_color_shades(*pal + 196, DOPT_COLOR + 3, d->color[1]);
    palette_load(*pal);
}

// The dropdown's callback: a driver was picked (or the list was rebuilt).
void dopt_driver_selected(Widget *list) {
    ListData *ld = list->list;
    if (ld->items.size() == 0) {  // no drivers: only the "no drivers" note
        dopt_draw_page();
        show(DOPT_AI_SKILL, false);
        show(DOPT_DRIVERS, false);
        for (int i = DOPT_CONTROL; i < DOPT_COUNT; i++) show(i, false);
        if (static_cast<uint16_t>(widget(DOPT_EMPTY).variant) < 2) show(DOPT_EMPTY, true);
        gui_redraw_all();
        return;
    }
    for (int i = DOPT_CONTROL; i < DOPT_COUNT; i++)
        if (static_cast<uint16_t>(widget(i).variant) < 2) show(i, true);
    show(DOPT_AI_SKILL, false);
    int i = ld->selected;
    if (i < 0) return;
    if (i >= ld->items.size()) {
        ld->selected = 0;
        i = 0;
    }
    list->list->items[i]->marked = 0;
    dopt_close_dropdown();
    MsString *name = &list->list->items[i]->text;
    if (&widget(DOPT_NAME).label != name) widget(DOPT_NAME).label.assign(*name);
    Driver *d = &g_drivers.get()[i];
    dopt_show_controls(d->human());
    for (int k = 0; k < 2; k++) {
        uint32_t c = d->color[k];
        widget(DOPT_COLOR + 3 * k).pos = static_cast<int32_t>(c >> 16 & 0xff);
        widget(DOPT_COLOR + 3 * k + 1).pos = static_cast<int32_t>(c >> 8 & 0xff);
        widget(DOPT_COLOR + 3 * k + 2).pos = static_cast<int32_t>(c & 0xff);
    }
    dopt_update_colors();

    int f = g_font_size, x0 = layout_x0(), y0 = layout_y0();
    erase(f * 14 + x0, f * 16 + y0, f * 18, f * 16);
    SdlSurfaceRef back = g_back;  // (lock_surface, inlined)
    if (sdl::LockSurface(back) == 0) {
        g_pixels = sdl::surface(back)->pixels;
        g_pitch = sdl::surface(back)->pitch;
        g_locked = back;
    } else {
        back = g_locked;
    }
    draw_car_preview(0, &g_cars.get()[d->car], f * 21 + x0, f * 23 + y0, 40.0f);
    if (back) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(back);
        g_locked = SdlSurfaceRef{};
    }
    gui_redraw_all();
}

void dopt_ai_skill_changed(Widget *slider) {
    if (Driver *d = selected_driver()) d->ai_skill() = slider->pos;
}

// A control's button: waits for a key or joystick input (0x483bc0, showing the control's name) and binds it.
void dopt_bind_control(int c, Widget *button) {
    Driver *d = selected_driver();
    if (!d) return;
    // (as in the original, the binding is copied even when a key was pressed and it stayed unset)
    guest::Local<JoyBinding> joy;
    int key = wait_for_control(&g_lang[L_ACCELERATE + c], joy.get());
    if (key >= 0) {
        d->joy[c] = *joy;
        d->key[c] = key;
        key_name(static_cast<uint32_t>(key), &button->label);
    }
    if (key == -2) {  // joystick
        d->joy[c] = *joy;
        d->key[c] = -1;
        joy_binding_name(joy.get(), &button->label);
    }
    gui_redraw_all();
}

// The name button: opens the driver dropdown (only the dropdown and the button stay visible), or closes it
// when it is open.
void dopt_name_clicked(Widget *) {
    if (!(widget(DOPT_DRIVERS).flags & WF_HIDDEN)) {
        dopt_close_dropdown();
        return;
    }
    gui_set_focus(DOPT_DRIVERS);
    for (int i = 0; i < DOPT_COUNT; i++) show(i, false);
    show(DOPT_DRIVERS, true);
    show(DOPT_NAME, true);
    list_draw(&widget(DOPT_DRIVERS));
}

// "Delete driver": after a Yes in the confirmation (question = text 102 + "'" + name + "'?"), frees the
// selected driver's record, deletes its .drv file, removes it from the list and moves the following
// drivers of g_drivers down by one; then shows whatever driver is selected now.
void dopt_delete_driver_clicked(Widget *button) {
    Widget *w = g_dopt_widgets.get();
    ListData *list = w[DOPT_DRIVERS].list.get();
    int sel = list->selected;
    if (sel < 0 || sel >= list->items.size()) return;
    guest::Local<MsString> part1, part2, question;
    MsString::concat(part1.get(), &g_lang[L_REALLY_DELETE], 0x49d804u);    // + "'"
    MsString::concat_move(part2.get(), part1.get(), &w[DOPT_NAME].label);
    MsString::concat_move(question.get(), part2.get(), 0x49d800u);         // + "'?"
    part2->destroy();
    part1->destroy();
    if (message_box(0x47e440, &g_lang[L_DELETE_DRIVER], question.get(), 2)) {
        Widget *ws = g_dopt_widgets.get();
        ListItem *item = ws[DOPT_DRIVERS].list->items[sel].get();
        if (item->data) guest::op_delete(&guest::at<Driver>(item->data), sizeof(Driver));
        guest::Local<MsString> name;
        name->construct(guest::addr(ws[DOPT_DRIVERS].list->items[sel]->text.c_str()));
        driver_delete(name.get());
        guest::call_thiscall(0x4027a0, name.get());  // ~string
        list_remove(&ws[DOPT_DRIVERS], sel);
        int count = ws[DOPT_DRIVERS].list->items.size();
        if (sel + 1 <= count) {
            uint32_t *dst = reinterpret_cast<uint32_t *>(&g_drivers.get()[sel]);
            const uint32_t *src = dst + sizeof(Driver) / 4;
            for (uint32_t n = static_cast<uint32_t>((count - sel) * sizeof(Driver)) / 4; n; n--) *dst++ = *src++;
        }
    }
    dopt_driver_selected(&g_dopt_widgets.get()[DOPT_DRIVERS]);
    question->destroy();
}

// "Create driver" (also when Options opens, and again after an empty name was refused): hides the
// screen's widgets and shows the dialog (name field, human/computer toggle, OK, Cancel) with an empty name
// and the keyboard in the name field.
void dopt_create_driver_clicked(Widget *button) {
    if (button) button->state &= ~WS_FOCUS;
    for (int i = 0; i < DOPT_DIALOG_END; i++) show(i, i >= DOPT_COUNT);
    Widget &kind = widget(DOPT_NEW_KIND);
    const MsString *caption = &g_lang[kind.id == 2 ? L_HUMAN : L_COMPUTER];
    if (&kind.label != caption) kind.label.assign(*caption);

    guest::Local<int32_t[4]> box;
    static const int32_t kBox[4] = {20, 19, 60, 41};
    for (int i = 0; i < 4; i++) (*box)[i] = kBox[i];
    guest::Local<PageLabel[2]> labels;
    static const struct { int y, text; } rows[] = {{0x1b, L_NEW_DRIVER_NAME}, {0x1f, L_NEW_DRIVER_KIND}};
    for (int i = 0; i < 2; i++) {
        PageLabel &l = (*labels)[i];
        l.x = 0x19;
        l.y = rows[i].y;
        l.color = 0xf0;
        l.text.init();
        l.text.assign(g_lang[rows[i].text]);
    }
    draw_dialog(*box, &g_lang[L_CREATE_DRIVER], *labels, 2);

    Widget &name = widget(DOPT_NEW_NAME);
    name.label.clear();
    name.pos = 0;
    gui_set_focus(DOPT_NEW_NAME);
    gui_redraw_all();
    guest::call_stdcall(0x4970c6, labels.get(), static_cast<uint32_t>(sizeof(PageLabel)), 2u, 0x47db40u);
}

// The Create driver dialog's OK (id 1) and Cancel (id 0). OK with an empty or existing name shows a
// message and reopens the dialog. Otherwise the dialog is closed (the screen's widgets come back, the
// dropdown stays closed, the dialog area is erased and the R G B captions redrawn) and on OK the driver is
// added: to the dropdown (selected), and as a new record at the end of g_drivers (no seasons, car 0,
// white and red, arrow keys for a human, skill 100 for a computer driver); the list item gets its own copy
// of the record. Then the selected driver is shown.
void dopt_create_driver_done(Widget *button) {
    Widget &list = widget(DOPT_DRIVERS);
    MsString *name = &widget(DOPT_NEW_NAME).label;
    if (button->id != 0) {
        int refused = -1;
        if (static_cast<int32_t>(name->size) < 1) refused = L_DRIVER_NAME_EMPTY;
        else if (list.type == WIDGET_LIST && static_cast<int32_t>(list_find(list.list, name)) >= 0)
            refused = L_DRIVER_NAME_EXISTS;
        if (refused >= 0) {
            message_box(0x47e440, &g_lang[0], &g_lang[refused], 1);
            dopt_driver_selected(&list);
            dopt_create_driver_clicked(button);
            return;
        }
    }
    button->state &= ~WS_FOCUS;
    for (int i = 0; i < DOPT_DIALOG_END; i++) show(i, i < DOPT_COUNT && static_cast<uint16_t>(widget(i).variant) < 2);
    show(DOPT_DRIVERS, false);

    int f = g_font_size, x0 = layout_x0(), y0 = layout_y0();
    if (g_back) {
        guest::Local<SdlRect> r;
        r->x = static_cast<int16_t>(f * 20 - g_menu_border + x0);
        r->y = static_cast<int16_t>(f * 19 - g_menu_border + y0);
        r->w = static_cast<uint16_t>((f * 20 + g_menu_border) * 2);
        r->h = static_cast<uint16_t>((f * 11 + g_menu_border) * 2);
        sdl::FillRect(g_back, r.get(), 100);
    }
    SdlSurfaceRef back = g_back;  // (lock_surface, inlined)
    if (sdl::LockSurface(back) == 0) {
        g_pixels = sdl::surface(back)->pixels;
        g_pitch = sdl::surface(back)->pitch;
        g_locked = back;
    }
    static const uint32_t kRgb[3] = {0x49d82c, 0x49d830, 0x49d834};  // "R", "G", "B"
    static const int kRows[6] = {13, 16, 19, 23, 26, 29};          // colour 1, colour 2
    for (int i = 0; i < 6; i++) {
        guest::Local<MsString> s;
        s->init();
        s->assign(&guest::at<char>(kRgb[i % 3]), 1);
        draw_text_at(s.get(), f * 37 + x0, f * kRows[i] + y0, 0xf0, 0);
        s->destroy();
    }
    if (g_locked) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(g_locked);
        g_locked = SdlSurfaceRef{};
    }

    if (button->id != 0) {
        list_add(&list, name);
        uint32_t n = static_cast<uint32_t>(list.list->items.size());
        // g_drivers grows by one record (malloc, copy, free)
        uint64_t bytes = static_cast<uint64_t>(n) * sizeof(Driver);
        uint32_t fresh = guest::call_cdecl(0x496c14, (bytes >> 32) ? 0xffffffffu : static_cast<uint32_t>(bytes));
        if (g_drivers) {
            std::memcpy(&guest::at<Driver>(fresh), g_drivers.get(), (n - 1) * sizeof(Driver));
            guest::call_cdecl(0x496bd7, g_drivers);  // free
        }
        g_drivers = ptr32<Driver>::from_addr(fresh);
        Driver &d = g_drivers.get()[n - 1];
        d.seasons = 0;
        for (int &p : d.places) p = 0;
        d.points = d.races = 0;
        for (int &u : d.race_places) u = 0;
        d.fastest_laps = 0;
        d.car = 0;
        d.color[0] = 0xffffff;
        d.color[1] = 0xff0000;
        // (the joystick bindings stay as allocated)
        static const int32_t kHuman[4] = {273, 274, 276, 275};  // arrow keys up, down, left, right
        static const int32_t kComputer[4] = {0, 100, 0, 0};    // skill 100
        const int32_t *keys = widget(DOPT_NEW_KIND).id == 2 ? kHuman : kComputer;
        for (int k = 0; k < CTRL_COUNT; k++) d.key[k] = keys[k];
        uint32_t copy = guest::call_cdecl(0x496bdc, static_cast<uint32_t>(sizeof(Driver)));  // operator new
        std::memcpy(&guest::at<Driver>(copy), &d, sizeof(Driver));
        list.list->items[static_cast<int>(n - 1)]->data = copy;
        list.list->previous = list.list->selected;
        list.list->selected = static_cast<int32_t>(n - 1);
    }
    dopt_driver_selected(&list);
}


// Opens the Driver options screen. The first time (empty dropdown) the dropdown and g_drivers are built:
// the race's drivers first (the players', or the saved ones when g_saved_driver_count >= 0), then those
// of the main menu's driver list; their items keep pointing at the original records. (g_drivers gets room
// for the players plus the list, whichever drivers come first.)
void dopt_open() {
    Widget &list = widget(DOPT_DRIVERS);
    if (list.list->items.size() == 0) {
        uint32_t total = static_cast<uint32_t>(g_driver_list->items.size() + g_player_count);
        int players = g_player_count;
        if (g_drivers) guest::call_cdecl(0x496bd7, g_drivers);  // free
        if (static_cast<int32_t>(total) < 1) {
            g_drivers = nullptr;
        } else {
            uint64_t bytes = static_cast<uint64_t>(total) * sizeof(Driver);
            g_drivers = ptr32<Driver>::from_addr(
                guest::call_cdecl(0x496c14, (bytes >> 32) ? 0xffffffffu : static_cast<uint32_t>(bytes)));
        }
        auto add = [&](int i, Driver *d, const char *name) {
            std::memcpy(&g_drivers.get()[i], d, sizeof(Driver));
            guest::Local<MsString> s;
            s->init();
            s->assign(name, static_cast<uint32_t>(std::strlen(name)));
            list_add(&list, s.get());
            s->destroy();
            list.list->items[i]->data = guest::addr(d);
        };
        if (g_saved_driver_count < 0) {
            for (int i = 0; i < g_player_count; i++) add(i, g_player_driver[i].get(), g_player_name[i]);
        } else {
            for (int i = 0; i < g_saved_driver_count; i++) add(i, g_saved_drivers[i].get(), g_saved_driver_names[i]);
        }
        for (uint32_t k = 0; k < static_cast<uint32_t>(g_driver_list->items.size()); k++) {
            ListItem *src = g_driver_list->items[static_cast<int>(k)].get();
            std::memcpy(&g_drivers.get()[players + static_cast<int>(k)], &guest::at<Driver>(src->data), sizeof(Driver));
            list_add(&list, &src->text);
            ListItem *dst = list.list->items[players + static_cast<int>(k)].get();
            dst->folder = g_driver_list->items[static_cast<int>(k)]->folder;
            dst->data = g_driver_list->items[static_cast<int>(k)]->data;
        }
    }
    gui_close();
    guest::call_regs(0x47e440, 0, 0);  // draws the page (not rewritten yet)
    gui_open(6, g_dopt_widgets.get(), 0x18, guest::at<float>(0x49dc7c) /* 1.0 */, 0x473930, 0x473b00, 0x473ee0, 0);
    list.list->selected = 0;
    list.flags &= ~WF_HIDDEN;
    dopt_driver_selected(&list);
    gui_redraw_all();
}

// Draws the Driver options page (0x47e440): title lang 37, the captions (lang 73; lang 74 and 75 as headings
// in colour 0x73, the slider letters at 0x49d82c / 0x49d830 / 0x49d834 for both colours), then lang 89
// for a computer driver's skill slider or lang 76..79 for a human's four controls. The selected driver's
// car is drawn as a preview, and while the "new driver" dialog is open (widget 21 shown) the dialog too
// (box {20, 19, 60, 41}, title lang 70, captions lang 81, 82).
void dopt_draw_page() {
    const bool computer = !(widget(DOPT_AI_SKILL).flags & WF_HIDDEN);
    struct L {
        int x, y, color;
        int text;       // lang index, or -1: a letter
        uint32_t letter;
    };
    static const L kCommon[9] = {
        {0x0e, 0x07, 0xf0, 73, 0},        {0x0e, 0x0d, 0x73, 74, 0},        {0x25, 0x0d, 0xf0, -1, 0x49d82c},
        {0x25, 0x10, 0xf0, -1, 0x49d830}, {0x25, 0x13, 0xf0, -1, 0x49d834}, {0x25, 0x17, 0xf0, -1, 0x49d82c},
        {0x25, 0x1a, 0xf0, -1, 0x49d830}, {0x25, 0x1d, 0xf0, -1, 0x49d834}, {0x0e, 0x22, 0x73, 75, 0},
    };
    const int n = computer ? 10 : 13;
    guest::Local<PageLabel[13]> labels;
    guest::Local<MsString> s;
    auto make = [&](int i, int x, int y, int color, int text, uint32_t letter) {
        s->init();
        if (text >= 0) s->assign(g_lang[text]);
        else s->assign(&guest::at<char>(letter), 1);
        page_label_construct(&(*labels)[i], x, y, static_cast<uint8_t>(color), s.get());
        s->destroy();
    };
    for (int i = 0; i < 9; i++) make(i, kCommon[i].x, kCommon[i].y, kCommon[i].color, kCommon[i].text, kCommon[i].letter);
    if (computer) {
        make(9, 0x0e, 0x25, 0xf0, 89, 0);
    } else {
        for (int k = 0; k < 4; k++) make(9 + k, 0x0e, 0x25 + 3 * k, 0xf0, 76 + k, 0);
    }
    draw_page(&g_lang[37], *labels, n);
    guest::call_stdcall(0x4970c6, labels.get(), static_cast<uint32_t>(sizeof(PageLabel)), static_cast<uint32_t>(n), 0x47db40u);

    // the selected driver's car
    ListData *list = widget(DOPT_DRIVERS).list.get();
    int sel = list->selected;
    if (sel >= 0 && sel < list->items.size()) {
        const int f = g_font_size;
        int x0 = layout_x0(), y0 = layout_y0();
        const Driver &d = g_drivers.get()[sel];
        lock_surface(SURF_BACK);
        draw_car_preview(0u, &g_cars.get()[d.car], f * 0x15 + x0, f * 0x17 + y0, 40.0f);
        unlock_surface();
    }

    if (!(widget(21).flags & WF_HIDDEN)) {
        guest::Local<int32_t[4]> box;
        (*box)[0] = 20;
        (*box)[1] = 19;
        (*box)[2] = 60;
        (*box)[3] = 41;
        guest::Local<PageLabel[2]> dl;
        page_label_construct(&(*dl)[0], 0x19, 0x1b, 0xf0, &g_lang[81]);
        page_label_construct(&(*dl)[1], 0x19, 0x1f, 0xf0, &g_lang[82]);
        draw_dialog(*box, &g_lang[70], *dl, 2);
        guest::call_stdcall(0x4970c6, dl.get(), static_cast<uint32_t>(sizeof(PageLabel)), 2u, 0x47db40u);
    }
}

namespace {
void fill_screen(int x1, int y1, int x2, int y2, uint32_t colour) {
    const uint32_t s = guest::call_import(IMP_SDL_GetVideoSurface);
    if (!s) return;
    guest::Local<SdlRect> r;
    r->x = static_cast<int16_t>(x1);
    r->y = static_cast<int16_t>(y1);
    r->w = static_cast<uint16_t>(x2 - x1);
    r->h = static_cast<uint16_t>(y2 - y1);
    guest::call_import(IMP_SDL_FillRect, s, guest::addr(r.get()), colour);
}

int sdl_delay_10() { return static_cast<int>(guest::call_import(IMP_SDL_Delay, 10u)); }
}  // namespace

// Waits for a control to bind (0x483bc0): a 40 x 20 unit box in the middle of the screen (colour 0xf0, a
// border b inside it in colour 100) with the prompt (lang string at 0x4a63e0) and the control's name,
// shown at once. Then, every 10 ms, the keyboard and each joystick are polled:
//   a key held (0..322): wait until it is released, empty the event queue and return it; key 0, Escape
//   and Enter return -1 (nothing bound)
//   an axis beyond +-16000, a button or a hat: *joy gets the joystick, the input's number, its kind and
//   (axis) direction or (hat) value; returns -2
// (Each joystick's axis count is read once before, as the original does, without using it.)
int wait_for_control(const MsString *name, JoyBinding *joy) {
    const int u = g_font_size;
    const int x0 = static_cast<int>(guest::to_int64(static_cast<float>(g_screen_w - u * 80) * 0.5f + 0.5f));
    const int y0 = static_cast<int>(guest::to_int64(static_cast<float>(g_screen_h - u * 60) * 0.5f + 0.5f));
    int x1 = x0 + u * 20, x2 = x0 + u * 60, y1 = y0 + u * 20, y2 = y0 + u * 40;
    fill_screen(x1, y1, x2, y2, 0xf0);
    const int b = g_menu_border;
    x1 += b;
    y2 -= b;
    x2 -= b;
    y1 += b;
    fill_screen(x1, y1, x2, y2, 100);
    SdlSurfaceRef screen = sdl::GetVideoSurface();  // (lock_surface, inlined)
    if (sdl::LockSurface(screen) == 0) {
        g_pixels = sdl::surface(screen)->pixels;
        g_pitch = sdl::surface(screen)->pitch;
        g_locked = screen;
    } else {
        screen = g_locked;
    }
    guest::Local<Box> box;
    *box = Box{x1, y1 + u * 5, x2, y2 - u * 10};
    draw_text(&guest::at<MsString>(0x4a63e0), box.get(), 0xf0, 3);
    box->y1 += u * 5;
    box->y2 += u * 5;
    draw_text(name, box.get(), 0x5a, 3);
    if (screen) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(screen);
        g_locked = SdlSurfaceRef{};
    }
    guest::call_import(IMP_SDL_Flip, guest::call_import(IMP_SDL_GetVideoSurface));
    for (uint32_t i = 0; i < guest::call_import(IMP_SDL_NumJoysticks); i++)
        guest::call_import(IMP_SDL_JoystickNumAxes, guest::call_import(IMP_SDL_JoystickOpen, i));

    MsVector<JoyInfo> &js = g_joysticks;
    for (;;) {
        keyboard_snapshot();
        for (int k = 0; k < 0x143; k++) {
            if (!(g_keys.get()[k] & 1)) continue;
            do {
                sdl_delay_10();
                keyboard_snapshot();
            } while (g_keys.get()[k] & 1);
            guest::Local<SdlEvent> ev;
            while (guest::call_import(IMP_SDL_PollEvent, guest::addr(ev.get()))) {
            }
            return k == 0 || k == 0x1b || k == 0xd ? -1 : k;
        }
        for (uint32_t j = 0; j < static_cast<uint32_t>(js.size()); j++) {
            joy->joy = -1;
            for (uint32_t a = 0; a < static_cast<uint32_t>(js[j].axes); a++) {
                joy->kind = 0;
                const int16_t v = static_cast<int16_t>(guest::call_import(IMP_SDL_JoystickGetAxis, js[j].handle, a));
                if (v < -16000 || static_cast<int16_t>(guest::call_import(IMP_SDL_JoystickGetAxis, js[j].handle, a)) > 16000) {
                    const int16_t d = static_cast<int16_t>(guest::call_import(IMP_SDL_JoystickGetAxis, js[j].handle, a));
                    joy->joy = static_cast<int32_t>(j);
                    joy->index = static_cast<int32_t>(a);
                    joy->negative = d < 0;
                    return -2;
                }
            }
            for (uint32_t n = 0; n < static_cast<uint32_t>(js[j].buttons); n++) {
                joy->kind = 1;
                if (static_cast<uint8_t>(guest::call_import(IMP_SDL_JoystickGetButton, js[j].handle, n))) {
                    joy->joy = static_cast<int32_t>(j);
                    joy->index = static_cast<int32_t>(n);
                    return -2;
                }
            }
            for (uint32_t h = 0; h < static_cast<uint32_t>(js[j].hats); h++) {
                joy->kind = 2;
                if (static_cast<uint8_t>(guest::call_import(IMP_SDL_JoystickGetHat, js[j].handle, h))) {
                    joy->joy = static_cast<int32_t>(j);
                    joy->index = static_cast<int32_t>(h);
                    joy->unknown8 = static_cast<uint8_t>(guest::call_import(IMP_SDL_JoystickGetHat, js[j].handle, h));
                    return -2;
                }
            }
        }
        sdl_delay_10();
    }
}
