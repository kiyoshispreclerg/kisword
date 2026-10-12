/* versebox.c - one verse laid out in boxes (see versebox.h).
 *
 * A small flow layout, close to what MySword's WebView does with the same
 * verses: inline content (words, inline-block cells) fills lines and wraps
 * at spaces and between cells; a block takes a line of its own at full
 * width; a cell is as wide as its widest line (shrink to fit). Runs on a
 * line share a baseline, cells hang from the line's top. Right to left,
 * positions are mirrored. Run layouts are made once; changing the width
 * only redoes the arithmetic. */
#include "versebox.h"

#include "ref.h"

#include <string.h>

#define LINK_COLOR "#3b78c4"
#define MORPH_COLOR "#2e8b57"
#define RED_COLOR "#cc2222"
#define GLOSS_COLOR "#4a6fa5"
#define OTQUOTE_COLOR "#1f7a5c"
#define BORDER_PAD 4
#define CELL_GAP 12  /* between word columns */
#define LINE_GAP 1
#define UNBOUNDED (1 << 24)

typedef struct PBox PBox;

typedef struct {
    KsItemType type;
    const KsItem *item; /* RUN */
    PangoLayout *pl;    /* RUN */
    int w, h, asc;      /* RUN */
    int dir;            /* RUN: 1 left to right, -1 right to left, 0 neutral */
    PBox *box;          /* BOX */
    int x, y;           /* within the parent box */
    int line;
} PItem;

struct PBox {
    unsigned flags;
    gboolean rtl;
    int pad;
    GArray *items; /* PItem */
    int w, h;
};

typedef struct {
    int y, h, asc, lw;
} Line;

typedef struct {
    KsBox *box;
    KsItem num; /* the verse number, first thing on the first line */
    KsVerseBoxOptions opt;
    PangoFontDescription *font;
    PBox *root;
    int width, space_w;
    KsVerseBoxLinkCb cb;
    void *data;
    GdkCursor *hand;
    gboolean over_link;
} VB;

/* ---- preparing: one layout per run ---------------------------------------- */

static gboolean is_link(const KsItem *it)
{
    return it->kind == KS_SPAN_STRONG || it->kind == KS_SPAN_NOTE;
}

/* The direction of the first strong character: 1, -1, or 0 if none. */
static int text_dir(const char *s)
{
    for (; *s; s = g_utf8_next_char(s)) {
        gunichar c = g_utf8_get_char(s);

        if (c == 0x200E) /* LRM, RLM: the marks are there to set it */
            return 1;
        if (c == 0x200F)
            return -1;
        switch (g_unichar_get_script(c)) {
        case G_UNICODE_SCRIPT_HEBREW:
        case G_UNICODE_SCRIPT_ARABIC:
        case G_UNICODE_SCRIPT_SYRIAC:
        case G_UNICODE_SCRIPT_THAANA:
        case G_UNICODE_SCRIPT_NKO:
            return -1;
        case G_UNICODE_SCRIPT_COMMON:
        case G_UNICODE_SCRIPT_INHERITED:
            break;
        default:
            if (g_unichar_isalpha(c))
                return 1;
        }
    }
    return 0;
}

static PangoLayout *run_layout(VB *vb, PangoContext *ctx, const KsItem *it, PItem *p)
{
    PangoFontDescription *fd = pango_font_description_copy(vb->font);
    PangoAttrList *attrs = pango_attr_list_new();
    PangoLayout *pl = pango_layout_new(ctx);
    unsigned st = it->style;
    double scale = 1.0;
    char *text = NULL;
    int size = pango_font_description_get_size(fd);

    if (st & (KS_ST_SMALL | KS_ST_SUP | KS_ST_SUB))
        scale *= 0.8;
    if (it->kind == KS_SPAN_STRONG || it->kind == KS_SPAN_MORPH)
        scale *= 0.9;
    if (it->kind == KS_SPAN_NOTE || it->kind == KS_SPAN_VERSENUM || it->kind == KS_SPAN_XREF)
        scale *= 0.8;
    if (st & KS_ST_TITLE)
        scale *= 1.15;
    if (st & (KS_ST_ITALIC | KS_ST_TRANSLIT))
        pango_font_description_set_style(fd, PANGO_STYLE_ITALIC);
    if (st & (KS_ST_BOLD | KS_ST_TITLE) || it->kind == KS_SPAN_VERSENUM || it->kind == KS_SPAN_NOTE)
        pango_font_description_set_weight(fd, PANGO_WEIGHT_BOLD);
    if (size > 0) {
        if (pango_font_description_get_size_is_absolute(fd))
            pango_font_description_set_absolute_size(fd, size * scale);
        else
            pango_font_description_set_size(fd, (int)(size * scale));
    }
    if (st & KS_ST_UNDERLINE || it->kind == KS_SPAN_STRONG || it->kind == KS_SPAN_MORPH)
        pango_attr_list_insert(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
    if (st & KS_ST_STRIKE)
        pango_attr_list_insert(attrs, pango_attr_strikethrough_new(TRUE));

    if (it->kind == KS_SPAN_XREF) {
        int b, c, v1, v2 = 0;
        int got = sscanf(it->text, "%d.%d.%d-%d", &b, &c, &v1, &v2);

        if (got >= 3 && b >= 1)
            text = ks_ref_format(b, c, v1, got == 4 ? v2 : 0);
    }
    pango_layout_set_font_description(pl, fd);
    pango_layout_set_attributes(pl, attrs);
    pango_layout_set_text(pl, text ? text : it->text, -1);
    pango_layout_get_pixel_size(pl, &p->w, &p->h);
    p->dir = text_dir(pango_layout_get_text(pl));
    p->asc = pango_layout_get_baseline(pl) / PANGO_SCALE;
    if (st & KS_ST_SUP)
        p->asc += p->h / 3;
    g_free(text);
    pango_attr_list_unref(attrs);
    pango_font_description_free(fd);
    return pl;
}

static gboolean shown(VB *vb, const KsItem *it)
{
    if (it->kind == KS_SPAN_STRONG)
        return vb->opt.show_strong;
    if (it->kind == KS_SPAN_NOTE)
        return vb->opt.show_notes;
    return it->text && *it->text;
}

static void add_item(PBox *p, KsItemType type)
{
    PItem pi = { 0 };
    PItem *last = p->items->len ? &g_array_index(p->items, PItem, p->items->len - 1) : NULL;

    /* Breaks with nothing between them (a hidden layer) collapse. */
    if (type == KS_ITEM_BREAK) {
        while (last && last->type == KS_ITEM_SPACE) {
            g_array_set_size(p->items, p->items->len - 1);
            last = p->items->len ? &g_array_index(p->items, PItem, p->items->len - 1) : NULL;
        }
        if (!last || last->type == KS_ITEM_BREAK)
            return;
    }
    pi.type = type;
    g_array_append_val(p->items, pi);
}

static PBox *prepare(VB *vb, PangoContext *ctx, const KsBox *b, gboolean rtl, gboolean root)
{
    PBox *p = g_new0(PBox, 1);

    p->flags = b->flags;
    p->rtl = b->flags & KS_BOX_RTL ? TRUE : b->flags & KS_BOX_LTR ? FALSE : rtl;
    p->pad = b->flags & KS_BOX_BORDER ? BORDER_PAD : 0;
    p->items = g_array_new(FALSE, TRUE, sizeof(PItem));
    if (root) {
        PItem pi = { 0 };

        pi.type = KS_ITEM_RUN;
        pi.item = &vb->num;
        pi.pl = run_layout(vb, ctx, &vb->num, &pi);
        g_array_append_val(p->items, pi);
        add_item(p, KS_ITEM_SPACE);
    }
    for (guint i = 0; i < b->items->len; i++) {
        const KsItem *it = &g_array_index(b->items, KsItem, i);
        PItem pi = { 0 };

        switch (it->type) {
        case KS_ITEM_RUN:
            if (!shown(vb, it))
                break;
            pi.type = KS_ITEM_RUN;
            pi.item = it;
            pi.pl = run_layout(vb, ctx, it, &pi);
            g_array_append_val(p->items, pi);
            break;
        case KS_ITEM_BOX:
            pi.type = KS_ITEM_BOX;
            pi.box = prepare(vb, ctx, it->box, p->rtl, FALSE);
            g_array_append_val(p->items, pi);
            break;
        default:
            add_item(p, it->type);
        }
    }
    return p;
}

static void pbox_free(PBox *p)
{
    if (!p)
        return;
    for (guint i = 0; i < p->items->len; i++) {
        PItem *pi = &g_array_index(p->items, PItem, i);

        if (pi->pl)
            g_object_unref(pi->pl);
        pbox_free(pi->box);
    }
    g_array_free(p->items, TRUE);
    g_free(p);
}

/* ---- flow ----------------------------------------------------------------- */

typedef struct {
    GArray *lines; /* Line */
    int x, y;
    gboolean empty, space, prev_cell;
} Flow;

static void end_line(PBox *b, Flow *f)
{
    Line l = { f->y, 0, 0, f->x };
    int desc = 0;
    int n = f->lines->len;

    if (f->empty)
        return;
    for (guint i = 0; i < b->items->len; i++) {
        PItem *pi = &g_array_index(b->items, PItem, i);

        if (pi->line != n || pi->type == KS_ITEM_SPACE || pi->type == KS_ITEM_BREAK)
            continue;
        if (pi->type == KS_ITEM_RUN) {
            l.asc = MAX(l.asc, pi->asc);
            desc = MAX(desc, pi->h - pi->asc);
        } else {
            l.h = MAX(l.h, pi->box->h);
        }
    }
    l.h = MAX(l.h, l.asc + desc);
    g_array_append_val(f->lines, l);
    f->y += l.h + LINE_GAP;
    f->x = 0;
    f->empty = TRUE;
    f->space = FALSE;
    f->prev_cell = FALSE;
}

/* Puts items [i0, i1) on the current line, wrapping first if they don't fit. */
static void place(PBox *b, Flow *f, guint i0, guint i1, int width, int gap, int inner)
{
    if (!f->empty && f->x + gap + width > inner) {
        end_line(b, f);
        gap = 0;
    }
    f->x += gap;
    for (guint i = i0; i < i1; i++) {
        PItem *pi = &g_array_index(b->items, PItem, i);

        pi->line = f->lines->len;
        pi->x = f->x;
        f->x += pi->type == KS_ITEM_RUN ? pi->w : pi->box->w;
    }
    f->empty = FALSE;
    f->space = FALSE;
}

static int item_w(const PItem *pi)
{
    return pi->type == KS_ITEM_RUN ? pi->w : pi->box->w;
}

static int own_dir(const PBox *b, const PItem *pi)
{
    return pi->type == KS_ITEM_RUN ? pi->dir : b->rtl ? -1 : 1;
}

/* Direction of an item: runs by their text, cells go with the box. A
 * neutral one ("|", "·") goes with the next item on its line that has a
 * direction -- so "| in" in a Hebrew cell reads as MySword shows it. */
static int item_dir(const PBox *b, const PItem *pi)
{
    int d = own_dir(b, pi);
    const PItem *end = &g_array_index(b->items, PItem, b->items->len);

    for (const PItem *q = pi + 1; !d && q < end; q++) {
        if (q->type == KS_ITEM_BREAK || (q->type != KS_ITEM_SPACE && q->line != pi->line))
            break;
        if (q->type != KS_ITEM_SPACE)
            d = own_dir(b, q);
    }
    return d ? d : b->rtl ? -1 : 1;
}

/* From logical x (from the line's start) to the box's coordinates. A
 * minimal bidi: in a right-to-left box, words are placed from the right,
 * except that a stretch of left-to-right words ("the beginning") keeps its
 * order, as a browser does -- and the reverse in a left-to-right box. */
static void position(PBox *b, const Flow *f, int cw)
{
    int bdir = b->rtl ? -1 : 1;

    for (guint i = 0; i < b->items->len;) {
        PItem *pi = &g_array_index(b->items, PItem, i);
        const Line *l;
        int off = 0, x0, x1;
        guint j, k;

        if (pi->line < 0 || pi->type == KS_ITEM_SPACE || pi->type == KS_ITEM_BREAK) {
            i++;
            continue;
        }
        l = &g_array_index(f->lines, Line, pi->line);
        if (b->flags & KS_BOX_CENTER)
            off = (cw - l->lw) / 2;
        else if (b->flags & KS_BOX_END)
            off = cw - l->lw;

        /* The stretch [i, j) of items on this line going against the box,
         * neutral ones in between included. */
        j = i + 1;
        if (item_dir(b, pi) == -bdir) {
            for (k = i + 1; k < b->items->len; k++) {
                const PItem *q = &g_array_index(b->items, PItem, k);

                if (q->type == KS_ITEM_SPACE)
                    continue;
                if (q->line != pi->line || q->type == KS_ITEM_BREAK || item_dir(b, q) == bdir)
                    break;
                if (item_dir(b, q) == -bdir)
                    j = k + 1;
            }
        }
        x0 = pi->x;
        {
            const PItem *last = &g_array_index(b->items, PItem, j - 1);

            x1 = last->x + item_w(last);
        }
        for (k = i; k < j; k++) {
            PItem *q = &g_array_index(b->items, PItem, k);
            int w;

            if (q->line < 0 || q->type == KS_ITEM_SPACE || q->type == KS_ITEM_BREAK)
                continue;
            w = item_w(q);
            if (j == i + 1) /* a single item */
                q->x = b->rtl ? cw - (off + q->x) - w : off + q->x;
            else if (b->rtl) /* left-to-right stretch, kept in order */
                q->x = cw - off - x1 + (q->x - x0);
            else /* right-to-left stretch, reversed */
                q->x = off + x0 + (x1 - (q->x + w));
            q->y = q->type == KS_ITEM_RUN ? l->y + l->asc - q->asc : l->y;
            q->x += b->pad;
            q->y += b->pad;
        }
        i = j;
    }
}

static void flow(VB *vb, PBox *b, int avail)
{
    int inner = MAX(avail - 2 * b->pad, 1), cw = 0, total;
    Flow f = { g_array_new(FALSE, FALSE, sizeof(Line)), 0, 0, TRUE, FALSE, FALSE };
    gboolean cell = (b->flags & KS_BOX_CELL) != 0;

    for (guint i = 0; i < b->items->len; i++)
        g_array_index(b->items, PItem, i).line = -1;
    for (guint i = 0; i < b->items->len;) {
        PItem *pi = &g_array_index(b->items, PItem, i);

        if (pi->type == KS_ITEM_SPACE) {
            f.space = !f.empty;
            i++;
        } else if (pi->type == KS_ITEM_BREAK) {
            end_line(b, &f);
            i++;
        } else if (pi->type == KS_ITEM_BOX && !(pi->box->flags & KS_BOX_CELL)) {
            end_line(b, &f); /* a block: a line of its own, full width */
            flow(vb, pi->box, inner);
            pi->box->w = inner;
            place(b, &f, i, i + 1, 0, 0, inner);
            end_line(b, &f);
            i++;
        } else if (pi->type == KS_ITEM_BOX) {
            int gap = f.space || f.prev_cell ? MAX(CELL_GAP, vb->space_w) : 0;

            flow(vb, pi->box, UNBOUNDED);
            if (pi->box->w > inner)
                flow(vb, pi->box, inner);
            place(b, &f, i, i + 1, pi->box->w, f.empty ? 0 : gap, inner);
            f.prev_cell = TRUE;
            i++;
        } else {
            /* Runs with no space between them wrap as one word. */
            guint j = i;
            int w = 0, gap = f.space ? vb->space_w : 0;

            while (j < b->items->len && g_array_index(b->items, PItem, j).type == KS_ITEM_RUN)
                w += g_array_index(b->items, PItem, j++).w;
            place(b, &f, i, j, w, f.empty ? 0 : gap, inner);
            f.prev_cell = FALSE;
            i = j;
        }
    }
    end_line(b, &f);

    for (guint l = 0; l < f.lines->len; l++)
        cw = MAX(cw, g_array_index(f.lines, Line, l).lw);
    if (!cell)
        cw = inner;
    total = f.lines->len ? f.y - LINE_GAP : 0;

    position(b, &f, cw);
    b->w = cw + 2 * b->pad;
    b->h = total + 2 * b->pad;
    g_array_free(f.lines, TRUE);
}

/* ---- drawing and clicks ------------------------------------------------------- */

static void run_color(VB *vb, const KsItem *it, GdkColor *c)
{
    const KsPalette *pal = vb->opt.palette;
    const char *spec = NULL;

    if (it->color)
        spec = it->color;
    else if (it->kind == KS_SPAN_STRONG || it->kind == KS_SPAN_NOTE || it->kind == KS_SPAN_XREF)
        spec = LINK_COLOR;
    else if (it->kind == KS_SPAN_MORPH)
        spec = MORPH_COLOR;
    else if (it->kind == KS_SPAN_VERSENUM || it->style & KS_ST_TRANSLIT) {
        *c = pal->dim;
        return;
    } else if (it->style & KS_ST_RED && vb->opt.red_letters)
        spec = RED_COLOR;
    else if (it->style & KS_ST_GLOSS)
        spec = GLOSS_COLOR;
    else if (it->style & KS_ST_OTQUOTE)
        spec = OTQUOTE_COLOR;
    if (!spec || !gdk_color_parse(spec, c))
        *c = pal->text;
}

static void draw(VB *vb, cairo_t *cr, const PBox *b, int ox, int oy)
{
    if (b->flags & KS_BOX_BORDER) {
        gdk_cairo_set_source_color(cr, &vb->opt.palette->dim);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, ox + 0.5, oy + 0.5, b->w - 1, b->h - 1);
        cairo_stroke(cr);
    }
    for (guint i = 0; i < b->items->len; i++) {
        const PItem *pi = &g_array_index(b->items, PItem, i);

        if (pi->line < 0)
            continue;
        if (pi->type == KS_ITEM_RUN) {
            GdkColor c;

            run_color(vb, pi->item, &c);
            gdk_cairo_set_source_color(cr, &c);
            cairo_move_to(cr, ox + pi->x, oy + pi->y);
            pango_cairo_show_layout(cr, pi->pl);
        } else if (pi->type == KS_ITEM_BOX) {
            draw(vb, cr, pi->box, ox + pi->x, oy + pi->y);
        }
    }
}

static gboolean expose_cb(GtkWidget *w, GdkEventExpose *e, VB *vb)
{
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));

    gdk_cairo_region(cr, e->region);
    cairo_clip(cr);
    gdk_cairo_set_source_color(cr, vb->opt.highlight ? &vb->opt.palette->hl : &vb->opt.palette->base);
    cairo_paint(cr);
    if (vb->root)
        draw(vb, cr, vb->root, 0, 0);
    cairo_destroy(cr);
    return TRUE;
}

static const PItem *link_at(const PBox *b, int x, int y)
{
    for (guint i = 0; i < b->items->len; i++) {
        const PItem *pi = &g_array_index(b->items, PItem, i);

        if (pi->line < 0)
            continue;
        if (pi->type == KS_ITEM_RUN && is_link(pi->item) && x >= pi->x - 2 && x < pi->x + pi->w + 2 &&
            y >= pi->y && y < pi->y + pi->h)
            return pi;
        if (pi->type == KS_ITEM_BOX && x >= pi->x && x < pi->x + pi->box->w && y >= pi->y &&
            y < pi->y + pi->box->h) {
            const PItem *in = link_at(pi->box, x - pi->x, y - pi->y);

            if (in)
                return in;
        }
    }
    return NULL;
}

static gboolean release_cb(GtkWidget *w, GdkEventButton *e, VB *vb)
{
    const PItem *pi;

    if (e->button != 1 || !vb->root || !(pi = link_at(vb->root, (int)e->x, (int)e->y)))
        return FALSE;
    if (vb->cb)
        vb->cb(w, pi->item, (int)e->x_root, (int)e->y_root, vb->data);
    return TRUE;
}

static gboolean motion_cb(GtkWidget *w, GdkEventMotion *e, VB *vb)
{
    gboolean link = vb->root && link_at(vb->root, (int)e->x, (int)e->y);

    if (link != vb->over_link) {
        if (!vb->hand)
            vb->hand = gdk_cursor_new_for_display(gtk_widget_get_display(w), GDK_HAND2);
        vb->over_link = link;
        gdk_window_set_cursor(gtk_widget_get_window(w), link ? vb->hand : NULL);
    }
    return FALSE;
}

static void vb_free(VB *vb)
{
    pbox_free(vb->root);
    ks_box_free(vb->box);
    g_free(vb->num.text);
    pango_font_description_free(vb->font);
    if (vb->hand)
        gdk_cursor_unref(vb->hand);
    g_free(vb);
}

GtkWidget *ks_verse_box_new(KsBox *box, const KsVerseBoxOptions *opt, KsVerseBoxLinkCb cb, void *data)
{
    GtkWidget *w = gtk_drawing_area_new();
    VB *vb = g_new0(VB, 1);

    vb->box = box;
    vb->opt = *opt;
    vb->opt.label = NULL;
    vb->font = pango_font_description_copy(opt->font);
    vb->cb = cb;
    vb->data = data;
    vb->num.type = KS_ITEM_RUN;
    vb->num.kind = KS_SPAN_VERSENUM;
    vb->num.text = opt->label ? g_strdup_printf("%s %d", opt->label, opt->verse) : g_strdup_printf("%d", opt->verse);
    gtk_widget_add_events(w, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_object_set_data_full(G_OBJECT(w), "ks-verse-box", vb, (GDestroyNotify)vb_free);
    g_signal_connect(w, "expose-event", G_CALLBACK(expose_cb), vb);
    g_signal_connect(w, "button-release-event", G_CALLBACK(release_cb), vb);
    g_signal_connect(w, "motion-notify-event", G_CALLBACK(motion_cb), vb);
    return w;
}

void ks_verse_box_set_width(GtkWidget *w, int width)
{
    VB *vb = g_object_get_data(G_OBJECT(w), "ks-verse-box");

    if (!vb || width < 50 || width == vb->width)
        return;
    if (!vb->root) {
        PangoContext *ctx = gtk_widget_get_pango_context(w);
        PangoLayout *pl = pango_layout_new(ctx);

        pango_layout_set_font_description(pl, vb->font);
        pango_layout_set_text(pl, " ", 1);
        pango_layout_get_pixel_size(pl, &vb->space_w, NULL);
        g_object_unref(pl);
        vb->root = prepare(vb, ctx, vb->box, vb->opt.rtl, TRUE);
    }
    vb->width = width;
    flow(vb, vb->root, width);
    gtk_widget_set_size_request(w, width, MAX(vb->root->h, 1));
    gtk_widget_queue_draw(w);
}
