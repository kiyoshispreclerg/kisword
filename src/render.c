/* render.c - the chapter view (see render.h).
 *
 * A chapter is rendered into a fresh GtkTextBuffer (not yet attached to the
 * view, so inserting costs no relayout) that shares one tag table; changing
 * an option just re-renders from the verse texts kept in memory, without
 * touching the database. Each verse gets a mark "v<N>" for scrolling. */
#include "render.h"

#include "gbf.h"
#include "ks_i18n.h"
#include "ref.h"

#include <stdarg.h>
#include <string.h>

#define KS_MAX_INDENT 4
#define MARGIN 12
#define HIT_SLACK 3
#define LINK_COLOR "#3b78c4"
#define RED_COLOR "#cc2222"
#define OTQUOTE_COLOR "#1f7a5c"

typedef struct {
    int verse;
    int mod;    /* index in KsView.mods */
    char *text; /* verse text from the module (VerseRules applied) */
    int start, end;
} KsVerse;

typedef struct {
    int start, end; /* marker range in the buffer */
    int verse;
    char *raw;      /* note content, may contain HTML */
} KsNote;

struct KsView {
    GtkWidget *scroll, *text;
    GtkTextTagTable *table;
    GtkTextTag *t_hl, *t_indent[KS_MAX_INDENT + 1], *t_verse, *t_italic, *t_bold, *t_underline,
        *t_red, *t_otquote, *t_sup, *t_sub, *t_title, *t_strong, *t_note, *t_label, *t_group, *t_rtl,
        *t_ltr, *t_absent;
    GdkCursor *hand, *ibeam;
    gboolean over_link;
    KsViewOptions opt;

    KsModule *mod;  /* mods[0], NULL when nothing is shown */
    KsModule *mods[KS_VIEW_MAX_MODS];
    int nmods;      /* more than one: interleaved, verse by verse */
    int book, chapter, hl_from, hl_to;
    GArray *verses; /* KsVerse, in verse order, then module order */
    GArray *notes;  /* KsNote */
    GtkWidget *popup;
};

/* ---- rendering ---------------------------------------------------------- */

typedef struct {
    KsView *v;
    GtkTextBuffer *b;
    GtkTextIter it;     /* always the end of the buffer */
    gboolean para;      /* paragraph layout */
    gboolean line_start;
    gboolean num_pending;
    gboolean in_title;
    gboolean prev_strong;
    int verse;
    int note_seq;
} Render;

static void put(Render *r, const char *t, int len, GtkTextTag **tags, int ntags)
{
    int off;

    if (len <= 0)
        return;
    off = gtk_text_iter_get_offset(&r->it);
    gtk_text_buffer_insert(r->b, &r->it, t, len);
    if (ntags) {
        GtkTextIter s;

        gtk_text_buffer_get_iter_at_offset(r->b, &s, off);
        for (int i = 0; i < ntags; i++)
            gtk_text_buffer_apply_tag(r->b, tags[i], &s, &r->it);
        gtk_text_buffer_get_end_iter(r->b, &r->it);
    }
    r->line_start = t[len - 1] == '\n';
    r->prev_strong = FALSE;
}

static void newline(Render *r)
{
    if (!r->line_start)
        put(r, "\n", 1, NULL, 0);
}

/* Foreground colors from classes and <font>: one tag per color, made the
 * first time it is seen. */
static GtkTextTag *color_tag(KsView *v, const char *color)
{
    char name[24];
    GtkTextTag *t;

    g_snprintf(name, sizeof name, "fg%s", color);
    t = gtk_text_tag_table_lookup(v->table, name);
    if (!t) {
        t = gtk_text_tag_new(name);
        g_object_set(t, "foreground", color, NULL);
        gtk_text_tag_table_add(v->table, t);
        g_object_unref(t);
    }
    return t;
}

static int style_tags(Render *r, const KsSpan *sp, GtkTextTag **out)
{
    KsView *v = r->v;
    unsigned st = sp->style;
    int indent = sp->indent;
    int n = 0;

    if (indent > 0)
        out[n++] = v->t_indent[MIN(indent, KS_MAX_INDENT)];
    if (st & KS_ST_ITALIC)
        out[n++] = v->t_italic;
    if (st & KS_ST_BOLD)
        out[n++] = v->t_bold;
    if (st & KS_ST_UNDERLINE)
        out[n++] = v->t_underline;
    if ((st & KS_ST_RED) && v->opt.red_letters)
        out[n++] = v->t_red;
    if (st & KS_ST_OTQUOTE)
        out[n++] = v->t_otquote;
    if (st & KS_ST_SUP)
        out[n++] = v->t_sup;
    if (st & KS_ST_SUB)
        out[n++] = v->t_sub;
    if (st & KS_ST_TITLE)
        out[n++] = v->t_title;
    if (sp->color)
        out[n++] = color_tag(v, sp->color);
    return n;
}

/* The verse number goes in front of the first real content, so a section
 * title (<TS>) at the start of the verse ends up above it. */
static void ensure_num(Render *r)
{
    GtkTextTag *tags[2] = { r->v->t_verse, r->v->t_sup };
    char buf[16];
    int n;

    if (!r->num_pending)
        return;
    r->num_pending = FALSE;
    n = g_snprintf(buf, sizeof buf, "%d ", r->verse);
    put(r, buf, n, tags, r->para ? 2 : 1);
}

static void title_edge(Render *r, gboolean title)
{
    if (title == r->in_title)
        return;
    newline(r);
    r->in_title = title;
}

static char *note_marker(int seq)
{
    if (seq < 26)
        return g_strdup_printf("%c", 'a' + seq);
    return g_strdup_printf("%c%c", 'a' + (seq / 26 - 1) % 26, 'a' + seq % 26);
}

static void span_cb(const KsSpan *sp, void *data)
{
    Render *r = data;
    KsView *v = r->v;
    GtkTextTag *tags[12];
    gboolean title = (sp->style & KS_ST_TITLE) != 0;

    if (sp->style & KS_ST_HIDDEN)
        return;
    switch (sp->kind) {
    case KS_SPAN_TEXT: {
        const char *t = sp->text;
        int len = sp->len;

        title_edge(r, title);
        if (r->line_start || (!title && r->num_pending)) {
            while (len > 0 && (*t == ' ' || *t == '\t')) {
                t++;
                len--;
            }
        }
        if (len <= 0)
            break;
        if (!title)
            ensure_num(r);
        put(r, t, len, tags, style_tags(r, sp, tags));
        break;
    }
    case KS_SPAN_STRONG:
        if (!v->opt.show_strong)
            break;
        if (!r->in_title)
            ensure_num(r);
        /* A word with several numbers ("created<WH1254><WH853>"): a thin
         * space keeps them apart as separate clickable ranges. */
        if (r->prev_strong)
            put(r, "\xe2\x80\x89", 3, NULL, 0);
        tags[0] = v->t_strong;
        put(r, sp->text, sp->len, tags, 1);
        r->prev_strong = TRUE;
        break;
    case KS_SPAN_NOTE: {
        KsNote note;
        char *marker;

        if (!v->opt.show_notes)
            break;
        if (!r->in_title)
            ensure_num(r);
        marker = sp->mark ? g_strndup(sp->mark, sp->mark_len) : note_marker(r->note_seq++);
        note.start = gtk_text_iter_get_offset(&r->it);
        tags[0] = v->t_note;
        put(r, marker, strlen(marker), tags, 1);
        note.end = gtk_text_iter_get_offset(&r->it);
        note.verse = r->verse;
        note.raw = g_strndup(sp->text, sp->len);
        g_array_append_val(v->notes, note);
        g_free(marker);
        break;
    }
    case KS_SPAN_VERSENUM:
        title_edge(r, FALSE);
        ensure_num(r);
        tags[0] = v->t_verse;
        put(r, sp->text, sp->len, tags, 1);
        break;
    case KS_SPAN_PARA:
        if (r->para)
            newline(r);
        break;
    case KS_SPAN_LINE:
        newline(r);
        break;
    case KS_SPAN_MORPH:
    case KS_SPAN_UNKNOWN:
        break;
    }
}

static void clear_notes(KsView *v)
{
    for (guint i = 0; i < v->notes->len; i++)
        g_free(g_array_index(v->notes, KsNote, i).raw);
    g_array_set_size(v->notes, 0);
}

static void clear_chapter(KsView *v)
{
    clear_notes(v);
    for (guint i = 0; i < v->verses->len; i++)
        g_free(g_array_index(v->verses, KsVerse, i).text);
    g_array_set_size(v->verses, 0);
}

static void render(KsView *v, int scroll_verse)
{
    GtkTextBuffer *b = gtk_text_buffer_new(v->table);
    GtkTextMark *mark = NULL;
    GtkTextIter start;
    Render r = { 0 };

    clear_notes(v);
    r.v = v;
    r.b = b;
    r.line_start = TRUE;
    /* Interleaved modules are always a verse per line. */
    for (guint i = 0; v->nmods == 1 && v->opt.paragraphs && i < v->verses->len && !r.para; i++)
        r.para = strstr(g_array_index(v->verses, KsVerse, i).text, "<CM>") != NULL;
    gtk_text_buffer_get_end_iter(b, &r.it);

    for (guint i = 0; i < v->verses->len; i++) {
        KsVerse *kv = &g_array_index(v->verses, KsVerse, i);
        const KsModule *m = v->mods[kv->mod];
        gboolean group = i == 0 || g_array_index(v->verses, KsVerse, i - 1).verse != kv->verse;
        char name[24];

        g_snprintf(name, sizeof name, "v%d", kv->verse);
        kv->start = gtk_text_iter_get_offset(&r.it);
        if (!gtk_text_buffer_get_mark(b, name))
            gtk_text_buffer_create_mark(b, name, &r.it, TRUE);
        if (v->nmods > 1) {
            GtkTextTag *tags[1] = { v->t_label };

            /* An RLM first: Pango takes a paragraph's direction from its
             * first strong character, and the label is Latin. */
            if (m->rtl)
                put(&r, "\xe2\x80\x8f", 3, NULL, 0);
            put(&r, ks_module_label(m), strlen(ks_module_label(m)), tags, 1);
            put(&r, " ", 1, NULL, 0);
        }
        r.verse = kv->verse;
        r.num_pending = TRUE;
        r.in_title = FALSE;
        r.prev_strong = FALSE;
        ks_gbf_parse(kv->text, m->css, span_cb, &r);
        title_edge(&r, FALSE);
        ensure_num(&r);
        kv->end = gtk_text_iter_get_offset(&r.it);
        if (!r.line_start)
            put(&r, r.para ? " " : "\n", 1, NULL, 0);
        if (v->nmods > 1) {
            /* Each module's lines get its own direction; a blank above
             * every verse keeps the groups apart. */
            GtkTextIter a, z;

            gtk_text_buffer_get_iter_at_offset(b, &a, kv->start);
            gtk_text_buffer_apply_tag(b, m->rtl ? v->t_rtl : v->t_ltr, &a, &r.it);
            if (group && i > 0) {
                z = a;
                gtk_text_iter_forward_to_line_end(&z);
                gtk_text_buffer_apply_tag(b, v->t_group, &a, &z);
            }
        }
    }
    if (!v->verses->len) {
        GtkTextTag *tags[1] = { v->t_absent };
        const char *msg = _("This module does not have this chapter.");

        put(&r, msg, strlen(msg), tags, 1);
    }

    if (v->hl_from) {
        int to = MAX(v->hl_to, v->hl_from), s = -1, e = -1;

        for (guint i = 0; i < v->verses->len; i++) {
            KsVerse *kv = &g_array_index(v->verses, KsVerse, i);

            if (kv->verse >= v->hl_from && kv->verse <= to) {
                if (s < 0)
                    s = kv->start;
                e = kv->end;
            }
        }
        if (s >= 0) {
            GtkTextIter a, z;

            gtk_text_buffer_get_iter_at_offset(b, &a, s);
            gtk_text_buffer_get_iter_at_offset(b, &z, e);
            gtk_text_buffer_apply_tag(b, v->t_hl, &a, &z);
        }
    }

    gtk_text_buffer_get_start_iter(b, &start);
    gtk_text_buffer_place_cursor(b, &start);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(v->text), r.para ? 8 : 4);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(v->text), b);
    g_object_unref(b);

    if (scroll_verse) {
        char name[24];

        g_snprintf(name, sizeof name, "v%d", scroll_verse);
        mark = gtk_text_buffer_get_mark(b, name);
    }
    if (!mark)
        mark = gtk_text_buffer_get_insert(b);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(v->text), mark, 0.0, TRUE, 0.0, 0.0);
}

/* ---- popups (Strong's numbers, footnotes) -------------------------------- */

static void popup_close(KsView *v)
{
    if (!v->popup)
        return;
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
    gtk_grab_remove(v->popup);
    gtk_widget_destroy(v->popup);
    v->popup = NULL;
}

static gboolean popup_dismiss_cb(GtkWidget *w, GdkEvent *e, KsView *v)
{
    (void)w;
    (void)e;
    popup_close(v);
    return TRUE;
}

/* A tooltip-styled window with a pointer/keyboard grab: any click or key
 * closes it, inside or outside the application. */
static void popup_show(KsView *v, const char *markup, int x, int y)
{
    GtkWidget *win, *frame, *label;
    GtkRequisition req;
    GdkScreen *screen = gtk_widget_get_screen(v->text);
    GdkRectangle mon;
    GdkWindow *gw;
    int px, py;

    popup_close(v);
    win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_name(win, "gtk-tooltip");
    gtk_window_set_screen(GTK_WINDOW(win), screen);
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_misc_set_padding(GTK_MISC(label), 8, 6);
    gtk_container_add(GTK_CONTAINER(frame), label);
    gtk_container_add(GTK_CONTAINER(win), frame);
    gtk_widget_add_events(win, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(win, "button-press-event", G_CALLBACK(popup_dismiss_cb), v);
    g_signal_connect(win, "key-press-event", G_CALLBACK(popup_dismiss_cb), v);
    g_signal_connect(win, "grab-broken-event", G_CALLBACK(popup_dismiss_cb), v);

    gtk_widget_show_all(frame);
    gtk_widget_size_request(win, &req);
    gdk_screen_get_monitor_geometry(screen, gdk_screen_get_monitor_at_point(screen, x, y), &mon);
    px = x - 8;
    py = y + 16;
    if (px + req.width > mon.x + mon.width)
        px = mon.x + mon.width - req.width;
    if (px < mon.x)
        px = mon.x;
    if (py + req.height > mon.y + mon.height)
        py = y - 8 - req.height;
    if (py < mon.y)
        py = mon.y;
    gtk_window_move(GTK_WINDOW(win), px, py);
    gtk_widget_show(win);

    v->popup = win;
    gtk_grab_add(win);
    gw = gtk_widget_get_window(win);
    gdk_pointer_grab(gw, TRUE, GDK_BUTTON_PRESS_MASK, NULL, NULL, GDK_CURRENT_TIME);
    gdk_keyboard_grab(gw, TRUE, GDK_CURRENT_TIME);
}

static gboolean is_link(KsView *v, const GtkTextIter *it)
{
    return gtk_text_iter_has_tag(it, v->t_strong) || gtk_text_iter_has_tag(it, v->t_note);
}

/* The Strong's number or note marker under the pointer, if any. The targets
 * are small superscripts only a few pixels wide, so a click just beside one
 * (HIT_SLACK pixels, on either neighbouring character) still counts. */
static gboolean link_at_pointer(KsView *v, double x, double y, GtkTextIter *out)
{
    GtkTextView *tv = GTK_TEXT_VIEW(v->text);
    GtkTextIter cand[3];
    int bx, by, line_y, line_h;

    gtk_text_view_window_to_buffer_coords(tv, GTK_TEXT_WINDOW_TEXT, (int)x, (int)y, &bx, &by);
    gtk_text_view_get_iter_at_location(tv, &cand[0], bx, by);
    gtk_text_view_get_line_yrange(tv, &cand[0], &line_y, &line_h);
    if (by < line_y || by >= line_y + line_h)
        return FALSE;
    cand[1] = cand[2] = cand[0];
    gtk_text_iter_backward_char(&cand[1]);
    gtk_text_iter_forward_char(&cand[2]);

    for (int i = 0; i < 3; i++) {
        GdkRectangle loc;
        int cy, ch;

        if (!is_link(v, &cand[i]))
            continue;
        gtk_text_view_get_line_yrange(tv, &cand[i], &cy, &ch);
        gtk_text_view_get_iter_location(tv, &cand[i], &loc);
        if (cy == line_y && bx >= loc.x - HIT_SLACK && bx < loc.x + loc.width + HIT_SLACK) {
            *out = cand[i];
            return TRUE;
        }
    }
    return FALSE;
}

static const KsNote *note_at(KsView *v, int off)
{
    for (guint i = 0; i < v->notes->len; i++) {
        const KsNote *n = &g_array_index(v->notes, KsNote, i);

        if (off >= n->start && off < n->end)
            return n;
    }
    return NULL;
}

static gboolean release_cb(GtkWidget *w, GdkEventButton *e, KsView *v)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    GtkTextIter it, s, z;
    char *markup = NULL;

    if (e->button != 1 || gtk_text_buffer_get_selection_bounds(b, &s, &z))
        return FALSE;
    if (!link_at_pointer(v, e->x, e->y, &it))
        return FALSE;

    if (gtk_text_iter_has_tag(&it, v->t_strong)) {
        char *num;

        s = z = it;
        if (!gtk_text_iter_begins_tag(&s, v->t_strong))
            gtk_text_iter_backward_to_tag_toggle(&s, v->t_strong);
        gtk_text_iter_forward_to_tag_toggle(&z, v->t_strong);
        num = gtk_text_buffer_get_text(b, &s, &z, FALSE);
        markup = g_markup_printf_escaped("<b>Strong %s</b>\n<small>%s</small>", num,
                                         _("Dictionaries are not supported yet."));
        g_free(num);
    } else if (gtk_text_iter_has_tag(&it, v->t_note)) {
        const KsNote *n = note_at(v, gtk_text_iter_get_offset(&it));

        if (n) {
            char *ref = ks_ref_format(v->book, v->chapter, n->verse, 0);
            char *body = ks_gbf_markup(n->raw);
            char *head = g_markup_printf_escaped("<b>%s</b>\n", ref);

            markup = g_strconcat(head, body, NULL);
            g_free(head);
            g_free(body);
            g_free(ref);
        }
    }
    if (markup) {
        popup_show(v, markup, (int)e->x_root, (int)e->y_root);
        g_free(markup);
    }
    return FALSE;
}

static gboolean motion_cb(GtkWidget *w, GdkEventMotion *e, KsView *v)
{
    GtkTextIter it;
    gboolean link = link_at_pointer(v, e->x, e->y, &it);
    if (link != v->over_link) {
        /* Created on first use: loading the cursor theme costs tens of
         * milliseconds, too much to spend at startup. */
        if (!v->hand) {
            v->hand = gdk_cursor_new_for_display(gtk_widget_get_display(w), GDK_HAND2);
            v->ibeam = gdk_cursor_new_for_display(gtk_widget_get_display(w), GDK_XTERM);
        }
        v->over_link = link;
        gdk_window_set_cursor(gtk_text_view_get_window(GTK_TEXT_VIEW(w), GTK_TEXT_WINDOW_TEXT),
                              link ? v->hand : v->ibeam);
    }
    return FALSE;
}

/* ---- copy ----------------------------------------------------------------- */

static int verse_index_at(KsView *v, int off)
{
    int found = -1;

    for (guint i = 0; i < v->verses->len; i++) {
        if (g_array_index(v->verses, KsVerse, i).start > off)
            break;
        found = (int)i;
    }
    return found;
}

/* Copies the verses touched by the selection, whole, as plain text with the
 * reference on top: "Jo 3:16-17 (KJV)\n16 ...\n17 ...". */
static void copy_cb(GtkTextView *tv, KsView *v)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(tv);
    GtkTextIter s, e;
    int i0, i1;
    GString *g;
    char *ref;

    if (!v->mod || !v->verses->len || !gtk_text_buffer_get_selection_bounds(b, &s, &e))
        return;
    i0 = MAX(verse_index_at(v, gtk_text_iter_get_offset(&s)), 0);
    i1 = MAX(verse_index_at(v, gtk_text_iter_get_offset(&e) - 1), i0);

    ref = ks_ref_format(v->book, v->chapter, g_array_index(v->verses, KsVerse, i0).verse,
                        g_array_index(v->verses, KsVerse, i1).verse > g_array_index(v->verses, KsVerse, i0).verse
                            ? g_array_index(v->verses, KsVerse, i1).verse
                            : 0);
    g = g_string_new(NULL);
    if (v->nmods > 1)
        g_string_printf(g, "%s\n", ref);
    else
        g_string_printf(g, "%s (%s)\n", ref, ks_module_label(v->mod));
    for (int i = i0; i <= i1; i++) {
        const KsVerse *kv = &g_array_index(v->verses, KsVerse, i);
        char *plain = ks_gbf_plain(kv->text, v->mods[kv->mod]->css);

        /* Interleaved: "16 (KJV) For God so loved..." */
        if (v->nmods > 1)
            g_string_append_printf(g, "%d (%s) %s\n", kv->verse, ks_module_label(v->mods[kv->mod]), plain);
        else if (i1 > i0)
            g_string_append_printf(g, "%d %s\n", kv->verse, plain);
        else
            g_string_append(g, plain);
        g_free(plain);
    }
    while (g->len && g->str[g->len - 1] == '\n')
        g_string_truncate(g, g->len - 1);

    gtk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(tv), GDK_SELECTION_CLIPBOARD), g->str, g->len);
    g_string_free(g, TRUE);
    g_free(ref);
    g_signal_stop_emission_by_name(tv, "copy-clipboard");
}

/* ---- setup ---------------------------------------------------------------- */

static GdkColor mix(GdkColor a, GdkColor b, double t)
{
    GdkColor c;

    c.pixel = 0;
    c.red = (guint16)(a.red * t + b.red * (1 - t));
    c.green = (guint16)(a.green * t + b.green * (1 - t));
    c.blue = (guint16)(a.blue * t + b.blue * (1 - t));
    return c;
}

/* Verse numbers and the highlight follow the theme (light or dark). */
static void style_set_cb(GtkWidget *w, GtkStyle *prev, KsView *v)
{
    GtkStyle *st = gtk_widget_get_style(w);
    GdkColor dim = mix(st->text[GTK_STATE_NORMAL], st->base[GTK_STATE_NORMAL], 0.5);
    GdkColor hl = mix(st->base[GTK_STATE_SELECTED], st->base[GTK_STATE_NORMAL], 0.25);

    (void)prev;
    g_object_set(v->t_verse, "foreground-gdk", &dim, NULL);
    g_object_set(v->t_label, "foreground-gdk", &dim, NULL);
    g_object_set(v->t_absent, "foreground-gdk", &dim, NULL);
    g_object_set(v->t_hl, "background-gdk", &hl, NULL);
}

static GtkTextTag *new_tag(KsView *v, const char *name, const char *first, ...) G_GNUC_NULL_TERMINATED;

static GtkTextTag *new_tag(KsView *v, const char *name, const char *first, ...)
{
    GtkTextTag *t = gtk_text_tag_new(name);
    va_list ap;

    va_start(ap, first);
    g_object_set_valist(G_OBJECT(t), first, ap);
    va_end(ap);
    gtk_text_tag_table_add(v->table, t);
    g_object_unref(t);
    return t;
}

KsView *ks_view_new(const KsViewOptions *opt)
{
    KsView *v = g_new0(KsView, 1);
    GtkTextBuffer *b;

    v->opt = *opt;
    v->verses = g_array_new(FALSE, FALSE, sizeof(KsVerse));
    v->notes = g_array_new(FALSE, FALSE, sizeof(KsNote));
    v->table = gtk_text_tag_table_new();

    /* Priority follows creation order: later tags win on conflicts. */
    v->t_hl = new_tag(v, "hl", "background", "#fff3b0", NULL);
    for (int i = 1; i <= KS_MAX_INDENT; i++) {
        char name[16];

        g_snprintf(name, sizeof name, "indent%d", i);
        v->t_indent[i] = new_tag(v, name, "left-margin", MARGIN + 24 * i, NULL);
    }
    v->t_verse = new_tag(v, "verse", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_SMALL, NULL);
    v->t_italic = new_tag(v, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    v->t_bold = new_tag(v, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    v->t_underline = new_tag(v, "underline", "underline", PANGO_UNDERLINE_SINGLE, NULL);
    v->t_red = new_tag(v, "red", "foreground", RED_COLOR, NULL);
    v->t_otquote = new_tag(v, "otquote", "foreground", OTQUOTE_COLOR, NULL);
    v->t_sup = new_tag(v, "sup", "rise", 6 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    v->t_sub = new_tag(v, "sub", "rise", -3 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    v->t_title = new_tag(v, "title", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_LARGE,
                         "pixels-above-lines", 10, "pixels-below-lines", 4, NULL);
    v->t_strong = new_tag(v, "strong", "foreground", LINK_COLOR, "scale", PANGO_SCALE_X_SMALL,
                          "rise", 6 * PANGO_SCALE, NULL);
    v->t_note = new_tag(v, "note", "foreground", LINK_COLOR, "scale", PANGO_SCALE_X_SMALL,
                        "rise", 6 * PANGO_SCALE, "weight", PANGO_WEIGHT_BOLD, NULL);
    v->t_label = new_tag(v, "label", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_X_SMALL, NULL);
    v->t_group = new_tag(v, "group", "pixels-above-lines", 10, NULL);
    v->t_rtl = new_tag(v, "rtl", "direction", GTK_TEXT_DIR_RTL, NULL);
    v->t_ltr = new_tag(v, "ltr", "direction", GTK_TEXT_DIR_LTR, NULL);
    v->t_absent = new_tag(v, "absent", "style", PANGO_STYLE_ITALIC, NULL);

    b = gtk_text_buffer_new(v->table);
    v->text = gtk_text_view_new_with_buffer(b);
    g_object_unref(b);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(v->text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(v->text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v->text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(v->text), MARGIN);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(v->text), MARGIN);
    gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(v->text), 2);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(v->text), 4);

    v->scroll = gtk_scrolled_window_new(NULL, NULL);
    /* Not NEVER: a wrapping GtkTextView asks for the width it last had, and
     * NEVER would pass that on, growing the window when a side panel opens.
     * Wrapping keeps the horizontal bar from ever showing. */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(v->scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(v->scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(v->scroll), v->text);

    g_signal_connect(v->text, "button-release-event", G_CALLBACK(release_cb), v);
    g_signal_connect(v->text, "motion-notify-event", G_CALLBACK(motion_cb), v);
    g_signal_connect(v->text, "copy-clipboard", G_CALLBACK(copy_cb), v);
    g_signal_connect(v->text, "style-set", G_CALLBACK(style_set_cb), v);
    return v;
}

GtkWidget *ks_view_widget(KsView *v)
{
    return v->scroll;
}

GtkWidget *ks_view_text(KsView *v)
{
    return v->text;
}

void ks_view_set_options(KsView *v, const KsViewOptions *opt)
{
    int top = ks_view_top_verse(v);

    v->opt = *opt;
    if (v->mod)
        render(v, top);
}

void ks_view_set_font(KsView *v, const PangoFontDescription *font)
{
    gtk_widget_modify_font(v->text, (PangoFontDescription *)font);
}

typedef struct {
    KsView *v;
    int mod;
} Collect;

static void collect_cb(int verse, const char *text, void *data)
{
    Collect *c = data;
    KsVerse kv = { verse, c->mod, g_strdup(text), 0, 0 };

    g_array_append_val(c->v->verses, kv);
}

static gint by_verse(gconstpointer a, gconstpointer b)
{
    const KsVerse *x = a, *y = b;

    return x->verse != y->verse ? (x->verse > y->verse) - (x->verse < y->verse) : x->mod - y->mod;
}

void ks_view_show(KsView *v, KsModule *const *mods, int nmods, int book, int chapter, int scroll_verse,
                  int hl_from, int hl_to)
{
    popup_close(v);
    clear_chapter(v);
    v->nmods = CLAMP(nmods, 1, KS_VIEW_MAX_MODS);
    for (int i = 0; i < v->nmods; i++)
        v->mods[i] = mods[i];
    v->mod = mods[0];
    v->book = book;
    v->chapter = chapter;
    v->hl_from = hl_from;
    v->hl_to = hl_to;
    /* Interleaved: the direction goes per line, with tags. */
    gtk_widget_set_direction(v->text, v->nmods == 1 && v->mod->rtl ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
    for (int i = 0; i < v->nmods; i++) {
        Collect c = { v, i };

        if (book <= KS_MAX_BOOK && v->mods[i]->nchapters[book])
            ks_module_chapter(v->mods[i], book, chapter, collect_cb, &c);
    }
    if (v->nmods > 1)
        g_array_sort(v->verses, by_verse);
    render(v, scroll_verse ? scroll_verse : hl_from);
}

void ks_view_message(KsView *v, const char *text)
{
    GtkTextBuffer *b = gtk_text_buffer_new(v->table);

    popup_close(v);
    clear_chapter(v);
    v->mod = NULL;
    v->nmods = 0;
    gtk_text_buffer_set_text(b, text, -1);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(v->text), b);
    g_object_unref(b);
}

int ks_view_top_verse(KsView *v)
{
    GtkTextView *tv = GTK_TEXT_VIEW(v->text);
    GdkRectangle r;
    GtkTextIter it;
    int i;

    if (!v->mod || !v->verses->len)
        return 0;
    gtk_text_view_get_visible_rect(tv, &r);
    gtk_text_view_get_iter_at_location(tv, &it, r.x, r.y);
    i = verse_index_at(v, gtk_text_iter_get_offset(&it));
    return g_array_index(v->verses, KsVerse, MAX(i, 0)).verse;
}

/* Verses kv[i0..] with the same number as kv[i0]: the buffer y where that
 * verse starts and where the next one does. */
static void group_yrange(KsView *v, guint i0, int *y0, int *y1)
{
    GtkTextView *tv = GTK_TEXT_VIEW(v->text);
    GtkTextIter it;
    int verse = g_array_index(v->verses, KsVerse, i0).verse, h;
    guint i = i0;

    gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(tv), &it,
                                       g_array_index(v->verses, KsVerse, i0).start);
    gtk_text_view_get_line_yrange(tv, &it, y0, &h);
    while (i < v->verses->len && g_array_index(v->verses, KsVerse, i).verse == verse)
        i++;
    if (i < v->verses->len) {
        gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(tv), &it,
                                           g_array_index(v->verses, KsVerse, i).start);
        gtk_text_view_get_line_yrange(tv, &it, y1, &h);
    } else {
        gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(tv), &it);
        gtk_text_view_get_line_yrange(tv, &it, y1, &h);
        *y1 += h;
    }
    if (*y1 <= *y0)
        *y1 = *y0 + 1;
}

gboolean ks_view_get_anchor(KsView *v, int *verse, double *frac)
{
    GtkTextView *tv = GTK_TEXT_VIEW(v->text);
    GdkRectangle r;
    GtkTextIter it;
    int i, y0, y1;

    if (!v->mod || !v->verses->len)
        return FALSE;
    gtk_text_view_get_visible_rect(tv, &r);
    gtk_text_view_get_iter_at_location(tv, &it, r.x, r.y);
    i = MAX(verse_index_at(v, gtk_text_iter_get_offset(&it)), 0);
    *verse = g_array_index(v->verses, KsVerse, i).verse;
    while (i > 0 && g_array_index(v->verses, KsVerse, i - 1).verse == *verse)
        i--;
    group_yrange(v, i, &y0, &y1);
    *frac = CLAMP((double)(r.y - y0) / (y1 - y0), 0.0, 1.0);
    return TRUE;
}

void ks_view_set_anchor(KsView *v, int verse, double frac)
{
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(v->scroll));
    guint i;
    int y0, y1;
    double y;

    if (!v->mod || !v->verses->len)
        return;
    /* That verse, or the next one this module has. */
    for (i = 0; i < v->verses->len && g_array_index(v->verses, KsVerse, i).verse < verse; i++)
        ;
    if (i == v->verses->len) {
        y = gtk_adjustment_get_upper(adj);
    } else {
        group_yrange(v, i, &y0, &y1);
        y = g_array_index(v->verses, KsVerse, i).verse == verse ? y0 + frac * (y1 - y0) : y0;
    }
    gtk_adjustment_set_value(adj, CLAMP(y, gtk_adjustment_get_lower(adj),
                                        gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj)));
}

GtkAdjustment *ks_view_vadjustment(KsView *v)
{
    return gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(v->scroll));
}

void ks_view_free(KsView *v)
{
    if (!v)
        return;
    popup_close(v);
    clear_chapter(v);
    gtk_widget_destroy(v->scroll);
    g_array_free(v->verses, TRUE);
    g_array_free(v->notes, TRUE);
    g_object_unref(v->table);
    if (v->hand) {
        gdk_cursor_unref(v->hand);
        gdk_cursor_unref(v->ibeam);
    }
    g_free(v);
}
