// Menu font rendering (see text.h).
#include "game/text.h"

#include "game/file.h"
#include "game/gui.h"
#include "game/video.h"

// glyph column in the font bitmap
static int glyph_index(signed char ch) {
    if (ch >= '!') return ch - 0x20;
    if (ch < 0 && ch > -0x41) return ch + 0xa0;  // 192..255 -> 96..159
    return 0;                                     // controls, space, 128..191: blank
}

// Draws text into the locked surface at the top-left of `clip` (or centred), clipped to it.
void draw_text(const MsString *text, const Box *clip, uint8_t color, uint32_t flags) {
    int len = static_cast<int>(text->size);
    int s = g_font_size;
    int dx = (flags & TEXT_CENTER_X) ? ((clip->x2 - s * len) - clip->x1) / 2 : 0;
    int dy = (flags & TEXT_CENTER_Y) ? ((s * -2 - clip->y1) + clip->y2) / 2 : 0;
    int x = clip->x1;
    for (int i = 0; i < len; i++) {
        const uint8_t *glyph = &guest::at<uint8_t>(g_font + static_cast<uint32_t>(glyph_index(text->c_str()[i]) * s));
        int y = clip->y1 + dy;
        uint32_t row = g_pixels + static_cast<uint32_t>(y * g_pitch);
        for (int r = 0; r < s * 2; r++, y++, row += static_cast<uint32_t>(g_pitch), glyph += s * 160) {
            if (y < clip->y1 || y > clip->y2) continue;
            for (int k = 0, px = x + dx; k < s; k++, px++)
                if (px >= clip->x1 && px <= clip->x2 && glyph[k]) guest::at<uint8_t>(row + static_cast<uint32_t>(px)) = color;
        }
        x += s;
    }
}

// Draws text at (x, y) into the locked surface, unclipped, stopping at a NUL; with shadow != 0 every
// pixel also gets a shadow pixel g_text_shadow to the right and below.
void draw_text_at(const MsString *text, int x, int y, uint8_t color, uint8_t shadow) {
    int s = g_font_size;
    for (uint32_t i = 0; i < text->size; i++, x += s) {
        signed char ch = text->c_str()[i];
        if (!ch) return;
        const uint8_t *glyph = &guest::at<uint8_t>(g_font + static_cast<uint32_t>(glyph_index(ch) * s));
        uint32_t row = g_pixels + static_cast<uint32_t>(g_pitch * y) + static_cast<uint32_t>(x);
        uint32_t shadow_row = g_pixels + static_cast<uint32_t>((y + g_text_shadow) * g_pitch + g_text_shadow) +
                              static_cast<uint32_t>(x);
        for (int r = 0; r < s * 2; r++, glyph += s * 160, row += static_cast<uint32_t>(g_pitch),
                 shadow_row += static_cast<uint32_t>(g_pitch)) {
            for (int k = 0; k < s; k++) {
                if (!glyph[k]) continue;
                if (shadow) guest::at<uint8_t>(shadow_row + static_cast<uint32_t>(k)) = shadow;
                guest::at<uint8_t>(row + static_cast<uint32_t>(k)) = color;
            }
        }
    }
}

// Builds the menu font for the current resolution (0x4853a0): the font size (layout unit) is
// screen_w / 80, or screen_h / 60 if that is smaller; the widget border is a quarter of it and the text
// shadow half the border, rounded up. font.bmp holds 160 glyphs of 20x20 pixels side by side, 1 bit per
// pixel, bottom-up (3200x40, 16000 bytes of image). Each glyph pixel of the new size covers a square of
// 20/size source pixels; it is set when at least a quarter of that square is set (partial source pixels
// at the edges counted by their covered fraction). Returns 0, -1 if font.bmp cannot be opened, -2 if its
// image size is not 16000.
int font_build() {
    g_font_size = g_screen_w / 80;
    if (g_screen_h < g_font_size * 60) g_font_size = g_screen_h / 60;
    int s = g_font_size;
    g_menu_border = s / 4;
    g_text_shadow = (g_menu_border + 1) / 2;
    if (g_font) {
        guest::call_cdecl(0x496bd7, g_font);  // free
        g_font = 0;
    }
    g_font = guest::call_cdecl(0x496c14, static_cast<uint32_t>(s * s * 320));  // malloc: 2s rows of 160s
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> path;
    path->init();
    path->assign(&guest::at<char>(0x49daa4), 8);  // "font.bmp"
    bool opened = file_open(f.get(), path.get(), 0x21);
    path->destroy();
    int result;
    if (!opened) {
        result = -1;
    } else {
        guest::Local<uint32_t> data_offset, image_size;
        *data_offset = 0;
        *image_size = 0;
        file_seek(f.get(), 10, 0);  // BITMAPFILEHEADER.bfOffBits
        file_read(f.get(), data_offset.get(), 4);
        file_seek(f.get(), 0x22, 0);  // BITMAPINFOHEADER.biSizeImage
        file_read(f.get(), image_size.get(), 4);
        if (*image_size != 16000) {
            file_close(f.get());
            result = -2;
        } else {
            uint32_t raw = guest::call_cdecl(0x496c14, 16000u);
            file_seek(f.get(), static_cast<int>(*data_offset), 0);
            file_read(f.get(), &guest::at<uint8_t>(raw), *image_size);
            // unpack to one byte per pixel, top row first
            uint32_t pixels = guest::call_cdecl(0x496c14, *image_size << 3);
            const uint8_t *src = &guest::at<uint8_t>(raw);
            uint8_t *bits = &guest::at<uint8_t>(pixels);
            for (int row = 0; row < 40; row++) {
                uint8_t *dst = bits + (39 - row) * 3200;
                for (int i = 0; i < 400; i++, src++)
                    for (int b = 0; b < 8; b++) dst[i * 8 + b] = (*src >> (7 - b)) & 1;
            }
            guest::call_cdecl(0x496bd7, raw);  // free
            // scale down
            const float step = 20.0f / static_cast<float>(s);
            const float threshold = step * 0.25f * step;
            uint8_t *out = &guest::at<uint8_t>(g_font);
            int width = s * 160;
            for (int y = 0; y < 2 * s; y++, out += width) {
                float y0 = static_cast<float>(y) * step, y1 = y0 + step;
                int row_end = static_cast<int>(y1 + 0.5f);
                for (int x = 0; x < width; x++) {
                    float x0 = static_cast<float>(x) * step, x1 = x0 + step;
                    int col_end = static_cast<int>(x1 + 0.5f);
                    float covered = 0.0f;
                    for (int r = static_cast<int>(y0); r < row_end; r++) {
                        for (int c = static_cast<int>(x0); c < col_end; c++) {
                            if (!bits[r * 3200 + c]) continue;
                            float w = 1.0f;
                            if (r == static_cast<int>(y0)) w = static_cast<float>(r + 1) - y0;
                            else if (r == static_cast<int>(y1)) w = y1 - static_cast<float>(r);
                            if (c == static_cast<int>(x0)) w *= static_cast<float>(c + 1) - x0;
                            else if (c == static_cast<int>(x1)) w *= x1 - static_cast<float>(c);
                            covered += w;
                        }
                    }
                    out[x] = threshold <= covered;
                }
            }
            guest::call_cdecl(0x496bd7, pixels);  // free
            file_close(f.get());
            result = 0;
        }
    }
    file_destruct(f.get());
    return result;
}
