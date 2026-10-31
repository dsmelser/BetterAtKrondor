#include <ctype.h>

#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/SCREENS/BOOKVIEW.H"
#include "SRC/UI/TEXTWRAP.H"
#include "SRC/GFX/FONT/FONT.H"

struct textline {
    s16 start;
    s16 end;
};

static s16 far textwrap_is_break_point(s32 text, s16 line_start, s16 pos) {
    char ch;
    s16 i;

    ch = toupper(*((char far *)((char huge *)text + pos)));
    if (ch == ' ')
        return 1;
    if (isalpha(ch) == 0)
        goto fail;
    if (pos - line_start <= 3)
        goto fail;
    i = 0;
    do {
        if (*((char far *)((char huge *)text + (pos + i - 3))) != '.')
            goto fail;
        i++;
    } while (i < 3);
    return 1;
fail:
    return 0;
}

s16 far textwrap_compute_lines(s32 text, s16 max_width, s16 *out_lines, s16 max_lines) {
    s16 pos;
    s16 line_start;
    char no_break;
    s16 line_width;
    s16 count;
    u16 width;
    u16 height;

#define CH(i) (*((char far *)((char huge *)text + (i))))

    line_start = 0;
    count = 0;
    while (count < max_lines && CH(line_start) != '\0') {
        pos = line_start;
        line_width = 0;
        no_break = 1;
        while (CH(pos) != '\0' && CH(pos) != '\n') {
            font_glyph_metrics(CH(pos), &width, &height);
            if (max_width < (s16)(line_width + width))
                break;
            line_width += (s16)width;
            if (textwrap_is_break_point(text, line_start, pos))
                no_break = 0;
            pos++;
        }
        if (CH(pos) != '\0' && CH(pos) != '\n' && !no_break) {
            while (pos != line_start && textwrap_is_break_point(text, line_start, pos) == 0)
                pos--;
            do {
                if (pos == line_start)
                    break;
            } while (textwrap_is_break_point(text, line_start, --pos) != 0);
            pos++;
        }
        if (out_lines != 0) {
            ((struct textline *)out_lines)[count].start = line_start;
            ((struct textline *)out_lines)[count].end = pos;
        }
        count++;
        line_start = pos;
        if (CH(line_start) == '\n') {
            line_start = line_start + 1;
        } else {
            while (CH(line_start) != '\0' && CH(line_start) == ' ')
                line_start++;
        }
    }
    if (count < max_lines && out_lines != 0) {
        ((struct textline *)out_lines)[count].start = -1;
        ((struct textline *)out_lines)[count].end = -1;
    }
    return count;
}

#undef CH

#pragma option -Od

void textwrap_draw_aligned(s32 text, s16 x, s16 y, s16 max_width, s16 max_height, s16 line_spacing,
                           u16 flags, s16 first_line) {
    struct textline lines[90];
    s16 line_height;
    s16 count;
    s16 hoff;
    s16 voff;
    char saved;
    register u16 f = flags;
    register s16 fl = first_line;

    line_height = g_graphics_context.pFont_height[0];
    hoff = 0;
    voff = 0;
    if ((f & 0) != 0)

        count = textwrap_compute_lines(text, 999, (s16 *)lines, 0x5a);
    else
        count = textwrap_compute_lines(text, max_width, (s16 *)lines, 0x5a);

    for (g_wTextWrapLinesRemaining = 0;
         max_height <
         (s16)((line_height + line_spacing) * ((count - fl) - g_wTextWrapLinesRemaining) -
               line_spacing);
         g_wTextWrapLinesRemaining++) {
    }
    if (g_wTextWrapLinesRemaining == 1) {
        g_wTextWrapLinesRemaining++;
    }
    g_wTextWrapXAccum = 0;
    g_wTextWrapLinesDrawn = (count - g_wTextWrapLinesRemaining) - fl;

    if (((f & 0x20) != 0) || ((f & 0x10) != 0)) {
        voff = max_height - (line_height + line_spacing) * g_wTextWrapLinesDrawn;
        if ((f & 0x10) != 0) {
            voff = voff / 2;
        }
    }

    while (fl < (s16)(count - g_wTextWrapLinesRemaining)) {
        if ((voff + line_height) > max_height)
            break;
        saved = *((char huge *)text + lines[fl].end);
        *((char huge *)text + lines[fl].end) = 0;
        if (((f & 2) != 0) || ((f & 4) != 0)) {
            hoff = max_width - font_text_pixel_width((char far *)text + lines[fl].start);
            if ((f & 2) != 0) {
                hoff = hoff / 2;
            }
        }
        g_wTextWrapXAccum += (lines[fl].end - lines[fl].start);
        font_draw_text_far((char far *)text + lines[fl].start, x + hoff, y + voff);
        *((char huge *)text + lines[fl].end) = saved;
        voff += line_height + line_spacing;
        fl++;
    }
}
