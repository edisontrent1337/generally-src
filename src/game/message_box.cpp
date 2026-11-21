#include "game/message_box.h"

#include <cstring>

#include "game/menu.h"
#include "game/menu_page.h"
#include "game/input.h"
#include "game/text.h"
#include "game/video.h"

static void free_text(ptr32<char> &p) {
    if (p) guest::call_cdecl(0x496bd7, p.a);  // free
    p = nullptr;
}

// A button of the message box: the result is bit 0 of the button's id (1 = OK / Yes); the dialog's
// copies of the message are freed.
void msgbox_button_clicked(Widget *button) {
    g_msgbox_widgets.get()[0].variant = static_cast<int16_t>(button->id & 1);
    free_text(g_msgbox_text);
    free_text(g_msgbox_title);
}

void inputbox_button_clicked(Widget *button) {
    g_inputbox_widgets.get()[0].variant = static_cast<int16_t>(button->id & 1);
    free_text(g_inputbox_text);
    free_text(g_inputbox_lines);
}

static uint32_t copy_cstr(const MsString *s) {
    uint32_t p = guest::call_cdecl(0x496c14, s->size + 1);  // malloc
    std::strcpy(&guest::at<char>(p), s->c_str());
    return p;
}

// Breaks text into lines of at most 40 characters (41-byte slots, at most 20 lines): at a newline, else
// after the last space, '.', ',' or '-' (or at a '!') within reach; returns the number of lines.
static int wrap_lines(const char *text, int n, char *lines, int max_lines) {
    if (n < 1) return 0;
    int count = 0, pos = 0, off = 0, next;
    do {
        int end = pos + 39 < n ? pos + 39 : n;
        lines[off] = 0;
        int len = -1;
        for (int i = pos; i < end; i++) {
            if (text[i] == '\n') {
                len = i - pos + 2;
                break;
            }
        }
        if (len < 0) {
            if (pos < end) {
                for (char c; (c = text[end]) != 0 && c != ' ' && c != '.' && c != ',' && c != '-';)
                    if (c == '!' || --end <= pos) break;
            }
            len = end + 2 - pos;
            if (len < 3) len = 0x28;
        }
        next = pos - 1 + len;
        std::strncpy(lines + off, text + pos, static_cast<size_t>(len));
        lines[off + len - 1] = 0;
        off += 41;
        count++;
    } while (off < 41 * max_lines && (pos = next) < n);
    return count;
}

// The modal message box: title, the text wrapped into lines, OK (buttons 1) or Yes/No (buttons 2).
// Runs its own event loop until a button frees the copies (a quit request ends it too, and is passed on);
// then redraw (a guest function, may be 0) repaints the screen below. Returns 1 for OK / Yes.
int message_box(uint32_t redraw, const MsString *title, const MsString *text, int buttons) {
    const char *s = text->c_str();
    int n = static_cast<int>(text->size);
    g_msgbox_text = ptr32<char>::from_addr(copy_cstr(text));
    g_msgbox_title = ptr32<char>::from_addr(copy_cstr(title));
    g_msgbox_redraw = redraw;
    guest::Local<char[41 * 20]> lines;
    int count = wrap_lines(s, n, *lines, 20);
    int top = 24 - count;

    // the lines as captions: new PageLabel[count]
    uint32_t block = guest::call_cdecl(0x496c14, static_cast<uint32_t>(count * static_cast<int>(sizeof(PageLabel)) + 4));
    PageLabel *labels = nullptr;
    if (block) {
        guest::at<uint32_t>(block) = static_cast<uint32_t>(count);
        labels = &guest::at<PageLabel>(block + 4);
        guest::call_stdcall(0x4971ad, guest::addr(labels), static_cast<uint32_t>(sizeof(PageLabel)), static_cast<uint32_t>(count),
                            0x478bd0u /* page_label_construct */, 0x47db40u /* ~PageLabel */);
    }
    for (int k = 0; k < count; k++) {
        PageLabel &l = labels[k];
        reinterpret_cast<uint8_t &>(l.color) = 0xf0;  // (only the low byte, as in the original)
        l.x = 0x14;
        l.y = top + 6 + 2 * k;
        const char *line = *lines + 41 * k;
        l.text.assign(line, static_cast<uint32_t>(std::strlen(line)));
    }
    guest::Local<int32_t[4]> box;
    (*box)[0] = 0x10;
    (*box)[1] = top;
    (*box)[2] = 0x40;
    (*box)[3] = count + 0x24;
    draw_dialog(*box, title, labels, count);
    if (block) {
        guest::call_stdcall(0x4970c6, guest::addr(labels), static_cast<uint32_t>(sizeof(PageLabel)), guest::at<uint32_t>(block),
                            0x47db40u);
        guest::call_cdecl(0x4970b8, block);  // operator delete[]
    }

    Widget *w = g_msgbox_widgets.get();
    float y = static_cast<float>((count + 0x20) * g_font_size) + static_cast<float>(layout_y0());
    w[0].y = w[1].y = w[2].y = y;
    ptr32<GuiState> saved = gui_push();
    ptr32<GuiState> below = g_menu_gui;
    g_menu_gui = saved;
    for (int i = 0; i < 3; i++) {  // OK, or Yes and No
        bool shown = buttons == 2 ? i != 0 : i == 0;
        if (shown) w[i].flags &= ~WF_HIDDEN;
        else w[i].flags |= WF_HIDDEN;
    }
    gui_open(0xb, g_msgbox_widgets.get(), 3, guest::at<float>(0x49dc7c) /* 1.0 */, 0, 0, 0, 0);
    gui_redraw_all();
    if (g_msgbox_text) {
        bool quit = false;
        while (g_msgbox_title) {
            if (!events_poll(1)) {
                quit = true;
                break;
            }
            if (!g_msgbox_text) break;
        }
        if (quit) {
            uint32_t ev = guest::call_cdecl(0x496bdc, 0x14u);  // operator new (never freed, as in the original)
            guest::at<uint8_t>(ev) = 0xc;                        // SDL_QUIT
            guest::call_import(IMP_SDL_PushEvent, ev);
        }
        if (g_msgbox_text) guest::call_cdecl(0x496bd7, g_msgbox_text);
    }
    g_msgbox_text = nullptr;
    if (g_msgbox_title) guest::call_cdecl(0x496bd7, g_msgbox_title);
    g_msgbox_title = nullptr;
    g_msgbox_redraw = 0;
    gui_close();
    g_menu_gui = below;
    if (redraw) guest::call_regs(redraw, 0, 0);
    gui_pop(&saved);
    gui_redraw_all();
    return static_cast<uint16_t>(g_msgbox_widgets.get()[0].variant);
}

// The modal text input box: title, the text wrapped into at most 16 lines, an empty text field (at most
// 20 characters, focused) and OK / Cancel. Runs its own event loop like message_box; afterwards the main
// menu is repainted below. Returns the entered text (a guest C string inside the field) for OK, else 0.
uint32_t input_box(const MsString *text, const MsString *title) {
    const char *s = text->c_str();
    int n = static_cast<int>(text->size);
    uint32_t text_copy = guest::call_cdecl(0x496c14, text->size + 1);    // malloc
    uint32_t title_copy = guest::call_cdecl(0x496c14, title->size + 1);  // malloc
    g_inputbox_text = ptr32<char>::from_addr(text_copy);
    g_inputbox_lines = ptr32<char>::from_addr(title_copy);
    std::strcpy(&guest::at<char>(text_copy), s);
    std::strcpy(&guest::at<char>(title_copy), title->c_str());
    guest::Local<char[41 * 16]> lines;
    int count = wrap_lines(s, n, *lines, 16);
    int top = 22 - count;

    uint32_t block = guest::call_cdecl(0x496c14, static_cast<uint32_t>(count * static_cast<int>(sizeof(PageLabel)) + 4));
    PageLabel *labels = nullptr;
    if (block) {
        guest::at<uint32_t>(block) = static_cast<uint32_t>(count);
        labels = &guest::at<PageLabel>(block + 4);
        guest::call_stdcall(0x4971ad, guest::addr(labels), static_cast<uint32_t>(sizeof(PageLabel)), static_cast<uint32_t>(count),
                            0x478bd0u /* page_label_construct */, 0x47db40u /* ~PageLabel */);
    }
    for (int k = 0; k < count; k++) {
        PageLabel &l = labels[k];
        reinterpret_cast<uint8_t &>(l.color) = 0xf0;
        l.x = 0x14;
        l.y = top + 6 + 2 * k;
        const char *line = *lines + 41 * k;
        l.text.assign(line, static_cast<uint32_t>(std::strlen(line)));
    }
    guest::Local<int32_t[4]> box;
    (*box)[0] = 0x10;
    (*box)[1] = top;
    (*box)[2] = 0x40;
    (*box)[3] = count + 0x27;
    draw_dialog(*box, title, labels, count);
    if (block) {
        guest::call_stdcall(0x4970c6, guest::addr(labels), static_cast<uint32_t>(sizeof(PageLabel)), guest::at<uint32_t>(block),
                            0x47db40u);
        guest::call_cdecl(0x4970b8, block);  // operator delete[]
    }

    // OK and Cancel below the text field, the field below the text
    Widget *w = g_inputbox_widgets.get();
    float y0 = static_cast<float>(layout_y0());
    w[0].y = w[1].y = static_cast<float>((count + 0x23) * g_font_size) + y0;
    w[2].y = static_cast<float>((count + 0x1e) * g_font_size) + y0;
    w[2].label.size = 0;
    const_cast<char *>(w[2].label.c_str())[0] = 0;
    w[2].max = 0x14;
    w[2].pos = 0;
    ptr32<GuiState> saved = gui_push();
    ptr32<GuiState> below = g_menu_gui;
    g_menu_gui = saved;
    gui_open(0xb, w, 3, guest::at<float>(0x49dc7c) /* 1.0 */, 0x473930 /* text field drawing */, 0, 0, 0);
    gui_set_focus(2);
    gui_redraw_all();
    bool quit = false;
    while (g_inputbox_text && g_inputbox_lines) {
        if (!events_poll(1)) {
            quit = true;
            break;
        }
    }
    if (quit) {
        uint32_t ev = guest::call_cdecl(0x496bdc, 0x14u);  // operator new (never freed, as in the original)
        guest::at<uint8_t>(ev) = 0xc;                        // SDL_QUIT
        guest::call_import(IMP_SDL_PushEvent, ev);
    }
    free_text(g_inputbox_text);
    free_text(g_inputbox_lines);
    gui_close();
    g_menu_gui = below;
    guest::call_regs(0x47da90, 0, 0);  // repaint the main menu
    gui_pop(&saved);
    gui_redraw_all();
    if (!g_inputbox_widgets.get()[0].variant) return 0;
    return guest::addr(g_inputbox_widgets.get()[2].label.c_str());
}
