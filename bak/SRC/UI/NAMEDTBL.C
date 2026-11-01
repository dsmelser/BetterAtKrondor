#include "SRC/GEN/GFXCTX.H"
#include "structs.h"
#include "SRC/UI/NAMEDTBL.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/MEM.H"
#include "SRC/GFX/FONT/FONT.H"

/*
 * A label file: the count, then 10-byte records (the label's 16-bit offset in the string
 * pool or FFFFh for none, x, y, flags, the colour and the shadow colour), then the pool's
 * size and the pool. The original keeps the records as they are and rebases each offset in
 * place into a near pointer (none: 0, which draws as an empty string), and finds the pool
 * again for freeing as the lowest of them. Natively the handle holds the pool and the
 * labels with pointers; callers only pass it back to these functions.
 */
typedef struct {
    char *text;
    s16 x, y, flags;
    unsigned char colour, shadow;
} NamedLabel;

typedef struct {
    s16 count;
    NamedLabel *labels;
    char *pool;
} NamedTable;

static s16 word_at(const unsigned char *p) {
    return (s16)(p[0] | p[1] << 8);
}

s16 *namedtbl_load(char *filename) {
    NamedTable *t;
    ResFile *file;
    unsigned char *raw;
    u16 poolSize;
    s16 i;

    t = galloc_safe_zcalloc(sizeof *t);
    file = res_fopen(filename, "rb");
    res_fread(&t->count, 2, 1, file);
    raw = galloc_safe_zcalloc(t->count * 10);
    res_fread(raw, 10, t->count, file);
    res_fread(&poolSize, 2, 1, file);
    if (poolSize != 0) {
        t->pool = galloc_safe_zcalloc(poolSize);
        res_fread(t->pool, 1, poolSize, file);
    }
    res_fclose(file);
    t->labels = galloc_safe_zcalloc(t->count * sizeof(NamedLabel));
    for (i = 0; i < t->count; i++) {
        const unsigned char *r = raw + i * 10;
        u16 at = (u16)word_at(r);
        if (at != 0xffff && t->pool == NULL)
            BAK_UNIMPLEMENTED("namedtbl_load: a label in a file without strings (the original "
                              "points into DGROUP)");
        t->labels[i].text = at == 0xffff ? NULL : t->pool + at;
        t->labels[i].x = word_at(r + 2);
        t->labels[i].y = word_at(r + 4);
        t->labels[i].flags = word_at(r + 6);
        t->labels[i].colour = r[8];
        t->labels[i].shadow = r[9];
    }
    galloc_zfree(raw);
    return (s16 *)t;
}

s16 namedtbl_free(s16 *table) {
    NamedTable *t = (NamedTable *)table;
    if (t == NULL)
        return 0;
    if (t->pool != NULL)
        galloc_zfree(t->pool);
    galloc_zfree(t->labels);
    galloc_zfree(t);
    return 1;
}

void far namedtbl_labels_draw_all(s16 *table) {
    NamedTable *t = (NamedTable *)table;
    s16 i;

    if (t != NULL) {
        for (i = 0; i < t->count; i++)
            namedtbl_label_draw((s16 *)&t->labels[i]);
    }
}

/* Centred (flags bit 1) or at x; with a shadow one row down in its own colour (bit 0). */
void far namedtbl_label_draw(s16 *label) {
    const NamedLabel *l = (const NamedLabel *)label;
    char *text = NEAR_STR(l->text);
    s16 x;

    g_graphics_context.clip.xmin = 0;
    g_graphics_context.clip.ymin = 0;
    g_graphics_context.clip.xmax = 0x13f;
    g_graphics_context.clip.ymax = 199;
    g_graphics_context.bClip_enabled = '\x01';
    g_graphics_context.bText_style_flags = '\x01';
    if (l->flags & 2U)
        x = 0xa0 - (font_text_width_ds(text) >> 1);
    else
        x = l->x;
    if ((l->flags & 1U) != 0) {
        g_graphics_context.bText_fg_color = l->shadow;
        font_draw_text_ds(text, x, l->y + 1);
    }
    g_graphics_context.bText_fg_color = l->colour;
    font_draw_text_ds(text, x, l->y);
}
