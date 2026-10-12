/* htmlview.c - module HTML in a GtkTextView (see htmlview.h).
 *
 * Like the chapter view, a page is built into a fresh buffer sharing one
 * tag table and only then attached. A paragraph break ("\n\n" from html.c)
 * becomes one line break plus space above the next line, which reads
 * better than blank lines in narrow dictionary entries. Every line starts
 * with an LRM (RLM in right-to-left modules): Pango takes a paragraph's
 * direction from its first letter, and a line opening with a Hebrew word
 * would otherwise turn right-aligned, unlike in a browser. Copying leaves
 * those marks out. */
#include "htmlview.h"

#include "html.h"

#include <gdk/gdkkeysyms.h>
#include <stdarg.h>
#include <string.h>

#define MARGIN 10
#define MAX_INDENT 6
#define LINK_COLOR "#3b78c4"
#define LRM "\xe2\x80\x8e"
#define RLM "\xe2\x80\x8f"

typedef struct {
    int start, end;
    char *href;
} Link;

typedef struct {
    int offset;
    char *src;
} Image;

struct KsHtmlView {
    GtkWidget *scroll, *text, *popup;
    GtkTextTagTable *table;
    GtkTextTag *t_bold, *t_italic, *t_underline, *t_sup, *t_sub, *t_small, *t_big, *t_strike, *t_h[4], *t_link,
        *t_indent[MAX_INDENT + 1], *t_para, *t_head, *t_note, *t_center, *t_right;
    GdkCursor *hand, *ibeam;
    gboolean over_link;
    guint click_idle;
    GArray *links;  /* Link, in buffer order */
    GArray *images; /* Image */

    KsHtmlLinkCb link_cb;
    KsHtmlImageCb image_cb;
    void *cb_data;

    /* While a page is being built. */
    GtkTextBuffer *b;
    GtkTextIter it;
    gboolean rtl, line_start, para;
    int img_width;
};

/* ---- building ---------------------------------------------------------- */

static void clear_page(KsHtmlView *hv)
{
    for (guint i = 0; i < hv->links->len; i++)
        g_free(g_array_index(hv->links, Link, i).href);
    for (guint i = 0; i < hv->images->len; i++)
        g_free(g_array_index(hv->images, Image, i).src);
    g_array_set_size(hv->links, 0);
    g_array_set_size(hv->images, 0);
}

static GtkTextTag *color_tag(KsHtmlView *hv, const char *color)
{
    char name[24];
    GtkTextTag *t;

    g_snprintf(name, sizeof name, "fg%s", color);
    t = gtk_text_tag_table_lookup(hv->table, name);
    if (!t) {
        t = gtk_text_tag_new(name);
        g_object_set(t, "foreground", color, NULL);
        gtk_text_tag_table_add(hv->table, t);
        g_object_unref(t);
    }
    return t;
}

/* Text at the end of the page, with tags. */
static void put(KsHtmlView *hv, const char *t, int len, GtkTextTag **tags, int ntags)
{
    int off = gtk_text_iter_get_offset(&hv->it);

    gtk_text_buffer_insert(hv->b, &hv->it, t, len);
    if (ntags) {
        GtkTextIter s;

        gtk_text_buffer_get_iter_at_offset(hv->b, &s, off);
        for (int i = 0; i < ntags; i++)
            gtk_text_buffer_apply_tag(hv->b, tags[i], &s, &hv->it);
        gtk_text_buffer_get_end_iter(hv->b, &hv->it);
    }
}

static void newline(KsHtmlView *hv)
{
    if (!hv->line_start)
        put(hv, "\n", 1, NULL, 0);
    hv->line_start = TRUE;
}

static int run_tags(KsHtmlView *hv, const KsHtmlRun *r, GtkTextTag **out)
{
    static const struct { unsigned bit; size_t field; } map[] = {
        { KS_HTML_BOLD, G_STRUCT_OFFSET(KsHtmlView, t_bold) },
        { KS_HTML_ITALIC, G_STRUCT_OFFSET(KsHtmlView, t_italic) },
        { KS_HTML_UNDERLINE, G_STRUCT_OFFSET(KsHtmlView, t_underline) },
        { KS_HTML_SUP, G_STRUCT_OFFSET(KsHtmlView, t_sup) },
        { KS_HTML_SUB, G_STRUCT_OFFSET(KsHtmlView, t_sub) },
        { KS_HTML_SMALL, G_STRUCT_OFFSET(KsHtmlView, t_small) },
        { KS_HTML_BIG, G_STRUCT_OFFSET(KsHtmlView, t_big) },
        { KS_HTML_STRIKE, G_STRUCT_OFFSET(KsHtmlView, t_strike) },
    };
    int n = 0;

    for (size_t i = 0; i < G_N_ELEMENTS(map); i++)
        if (r->style & map[i].bit)
            out[n++] = G_STRUCT_MEMBER(GtkTextTag *, hv, map[i].field);
    if (r->heading)
        out[n++] = hv->t_h[MIN(r->heading, 4) - 1];
    if (r->indent)
        out[n++] = hv->t_indent[MIN(r->indent, MAX_INDENT)];
    if (r->align)
        out[n++] = r->align == KS_HTML_ALIGN_CENTER ? hv->t_center : hv->t_right;
    if (r->color)
        out[n++] = color_tag(hv, r->color);
    if (r->href)
        out[n++] = hv->t_link;
    return n;
}

static void add_link(KsHtmlView *hv, int start, int end, const char *href)
{
    Link l;

    if (hv->links->len) {
        Link *last = &g_array_index(hv->links, Link, hv->links->len - 1);

        if (last->end == start && strcmp(last->href, href) == 0) {
            last->end = end;
            return;
        }
    }
    l.start = start;
    l.end = end;
    l.href = g_strdup(href);
    g_array_append_val(hv->links, l);
}

typedef struct {
    int max_w, max_h;
} Fit;

static void size_prepared(GdkPixbufLoader *l, int w, int h, Fit *f)
{
    double s = MIN((double)f->max_w / w, (double)f->max_h / h);

    if (s < 1.0)
        gdk_pixbuf_loader_set_size(l, MAX(1, (int)(w * s)), MAX(1, (int)(h * s)));
}

/* Decoded at most max_w x max_h, keeping the proportions: the maps of some
 * books are 10000 pixels wide. */
static GdkPixbuf *load_pixbuf(GBytes *bytes, int max_w, int max_h)
{
    GdkPixbufLoader *l = gdk_pixbuf_loader_new();
    Fit f = { max_w, max_h };
    GdkPixbuf *pb = NULL;
    gsize len;
    const guchar *data = g_bytes_get_data(bytes, &len);
    gboolean ok;

    g_signal_connect(l, "size-prepared", G_CALLBACK(size_prepared), &f);
    ok = gdk_pixbuf_loader_write(l, data, len, NULL);
    ok = gdk_pixbuf_loader_close(l, NULL) && ok;
    if (ok && (pb = gdk_pixbuf_loader_get_pixbuf(l)))
        g_object_ref(pb);
    g_object_unref(l);
    return pb;
}

static void put_image(KsHtmlView *hv, const KsHtmlRun *r)
{
    GBytes *bytes = hv->image_cb ? hv->image_cb(hv, r->image, hv->cb_data) : NULL;
    GdkPixbuf *pb = bytes ? load_pixbuf(bytes, hv->img_width, hv->img_width * 2) : NULL;
    Image img;

    if (bytes)
        g_bytes_unref(bytes);
    if (!pb) {
        GtkTextTag *tags[1] = { hv->t_note };

        put(hv, "[img]", 5, tags, 1);
        hv->line_start = FALSE;
        return;
    }
    img.offset = gtk_text_iter_get_offset(&hv->it);
    img.src = g_strdup(r->image);
    g_array_append_val(hv->images, img);
    gtk_text_buffer_insert_pixbuf(hv->b, &hv->it, pb);
    if (r->href)
        add_link(hv, img.offset, img.offset + 1, r->href);
    g_object_unref(pb);
    hv->line_start = FALSE;
}

static void run_cb(const KsHtmlRun *r, void *data)
{
    KsHtmlView *hv = data;
    GtkTextTag *tags[24];
    int ntags;

    if (r->anchor) {
        char *name = g_strconcat("a:", r->anchor, NULL);

        if (!gtk_text_buffer_get_mark(hv->b, name))
            gtk_text_buffer_create_mark(hv->b, name, &hv->it, TRUE);
        g_free(name);
        return;
    }
    if (r->image) {
        put_image(hv, r);
        return;
    }
    ntags = run_tags(hv, r, tags);
    for (int i = 0; i < r->len;) {
        int j = i;

        if (r->text[i] == '\n') {
            while (j < r->len && r->text[j] == '\n')
                j++;
            newline(hv);
            if (j - i >= 2)
                hv->para = TRUE;
            i = j;
            continue;
        }
        while (j < r->len && r->text[j] != '\n')
            j++;
        {
            int n = ntags, start;

            if (hv->para)
                tags[n++] = hv->t_para;
            hv->para = FALSE;
            if (hv->line_start)
                put(hv, hv->rtl ? RLM : LRM, 3, tags, n);
            hv->line_start = FALSE;
            start = gtk_text_iter_get_offset(&hv->it);
            put(hv, r->text + i, j - i, tags, n);
            if (r->href)
                add_link(hv, start, gtk_text_iter_get_offset(&hv->it), r->href);
        }
        i = j;
    }
}

void ks_html_view_begin(KsHtmlView *hv, gboolean rtl)
{
    GtkAllocation al;

    if (hv->b)
        g_object_unref(hv->b);
    clear_page(hv);
    hv->b = gtk_text_buffer_new(hv->table);
    gtk_text_buffer_get_end_iter(hv->b, &hv->it);
    hv->rtl = rtl;
    hv->line_start = TRUE;
    hv->para = FALSE;
    gtk_widget_get_allocation(hv->text, &al);
    /* Room for a vertical scroll bar that the page may bring. */
    hv->img_width = al.width > 100 ? al.width - 2 * MARGIN - 28 : 400;
    gtk_widget_set_direction(hv->text, rtl ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
}

void ks_html_view_heading(KsHtmlView *hv, const char *text, const char *mark)
{
    GtkTextTag *tags[1] = { hv->t_head };

    newline(hv);
    hv->para = FALSE;
    if (mark && !gtk_text_buffer_get_mark(hv->b, mark))
        gtk_text_buffer_create_mark(hv->b, mark, &hv->it, TRUE);
    put(hv, hv->rtl ? RLM : LRM, 3, tags, 1);
    put(hv, text, strlen(text), tags, 1);
    hv->line_start = FALSE;
    newline(hv);
}

void ks_html_view_html(KsHtmlView *hv, const char *html, const KsStyleSheet *css)
{
    newline(hv);
    ks_html_render(html, css, run_cb, hv);
    newline(hv);
}

void ks_html_view_note(KsHtmlView *hv, const char *text)
{
    GtkTextTag *tags[1] = { hv->t_note };

    newline(hv);
    put(hv, text, strlen(text), tags, 1);
    hv->line_start = FALSE;
    newline(hv);
}

gboolean ks_html_view_scroll_to(KsHtmlView *hv, const char *mark)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(hv->text));
    GtkTextMark *m = mark ? gtk_text_buffer_get_mark(b, mark) : NULL;

    if (!m)
        return FALSE;
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(hv->text), m, 0.0, TRUE, 0.0, 0.0);
    return TRUE;
}

gboolean ks_html_view_scroll_to_anchor(KsHtmlView *hv, const char *anchor)
{
    char *name = g_strconcat("a:", anchor, NULL);
    gboolean ok = ks_html_view_scroll_to(hv, name);

    g_free(name);
    return ok;
}

/* GTK2 drops the old buffer before cancelling a scroll still pending in
 * it, whose mark then is gone already (Gtk-CRITICAL): the old buffer is
 * kept alive across the change. */
static void set_buffer(GtkWidget *text, GtkTextBuffer *b)
{
    GtkTextBuffer *old = g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)));

    gtk_text_view_set_buffer(GTK_TEXT_VIEW(text), b);
    g_object_unref(old);
}

void ks_html_view_end(KsHtmlView *hv, const char *scroll_mark)
{
    GtkTextIter start;

    if (!hv->b)
        return;
    gtk_text_buffer_get_start_iter(hv->b, &start);
    gtk_text_buffer_place_cursor(hv->b, &start);
    set_buffer(hv->text, hv->b);
    g_object_unref(hv->b);
    hv->b = NULL;
    if (!ks_html_view_scroll_to(hv, scroll_mark))
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(hv->text), &start, 0.0, TRUE, 0.0, 0.0);
}

void ks_html_view_message(KsHtmlView *hv, const char *text)
{
    ks_html_view_begin(hv, FALSE);
    ks_html_view_note(hv, text);
    ks_html_view_end(hv, NULL);
}

/* ---- popup ------------------------------------------------------------- */

static void popup_close(KsHtmlView *hv)
{
    if (!hv->popup)
        return;
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
    gtk_grab_remove(hv->popup);
    gtk_widget_destroy(hv->popup);
    hv->popup = NULL;
}

static gboolean popup_dismiss_cb(GtkWidget *w, GdkEvent *e, KsHtmlView *hv)
{
    (void)w;
    (void)e;
    popup_close(hv);
    return TRUE;
}

void ks_html_view_popup(KsHtmlView *hv, const char *text, int x, int y)
{
    GtkWidget *win, *frame, *label;
    GtkRequisition req;
    GdkScreen *screen = gtk_widget_get_screen(hv->text);
    GdkRectangle mon;
    GdkWindow *gw;
    int px, py;

    popup_close(hv);
    win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_name(win, "gtk-tooltip");
    gtk_window_set_screen(GTK_WINDOW(win), screen);
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
    label = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_selectable(GTK_LABEL(label), FALSE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_misc_set_padding(GTK_MISC(label), 8, 6);
    gtk_container_add(GTK_CONTAINER(frame), label);
    gtk_container_add(GTK_CONTAINER(win), frame);
    gtk_widget_add_events(win, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(win, "button-press-event", G_CALLBACK(popup_dismiss_cb), hv);
    g_signal_connect(win, "key-press-event", G_CALLBACK(popup_dismiss_cb), hv);
    g_signal_connect(win, "grab-broken-event", G_CALLBACK(popup_dismiss_cb), hv);

    gtk_widget_show_all(frame);
    gtk_widget_size_request(win, &req);
    gdk_screen_get_monitor_geometry(screen, gdk_screen_get_monitor_at_point(screen, x, y), &mon);
    px = CLAMP(x - 8, mon.x, MAX(mon.x, mon.x + mon.width - req.width));
    py = y + 16;
    if (py + req.height > mon.y + mon.height)
        py = MAX(mon.y, y - 8 - req.height);
    gtk_window_move(GTK_WINDOW(win), px, py);
    gtk_widget_show(win);

    hv->popup = win;
    gtk_grab_add(win);
    gw = gtk_widget_get_window(win);
    gdk_pointer_grab(gw, TRUE, GDK_BUTTON_PRESS_MASK, NULL, NULL, GDK_CURRENT_TIME);
    gdk_keyboard_grab(gw, TRUE, GDK_CURRENT_TIME);
}

/* ---- images in a window ------------------------------------------------- */

static gboolean image_key_cb(GtkWidget *w, GdkEventKey *e, gpointer data)
{
    (void)data;
    if (e->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(w);
        return TRUE;
    }
    return FALSE;
}

void ks_image_window(GtkWidget *parent, GBytes *bytes, const char *title)
{
    GdkScreen *screen = gtk_widget_get_screen(parent);
    GdkPixbuf *pb = load_pixbuf(bytes, 4096, 4096);
    GtkWidget *win, *sw, *img;
    int w, h;

    if (!pb)
        return;
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(gtk_widget_get_toplevel(parent)));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
    w = MIN(gdk_pixbuf_get_width(pb) + 30, gdk_screen_get_width(screen) * 9 / 10);
    h = MIN(gdk_pixbuf_get_height(pb) + 30, gdk_screen_get_height(screen) * 9 / 10);
    gtk_window_set_default_size(GTK_WINDOW(win), w, h);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    img = gtk_image_new_from_pixbuf(pb);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sw), img);
    gtk_container_add(GTK_CONTAINER(win), sw);
    g_signal_connect(win, "key-press-event", G_CALLBACK(image_key_cb), NULL);
    g_object_unref(pb);
    gtk_widget_show_all(win);
}

/* ---- events ------------------------------------------------------------ */

/* The character under the pointer, only if it is really there (not the
 * empty space after a line's end). GTK2 may give the position after the
 * character hit -- always after an image -- so the one before counts too. */
static gboolean char_at(KsHtmlView *hv, double x, double y, GtkTextIter *out)
{
    GtkTextView *tv = GTK_TEXT_VIEW(hv->text);
    GtkTextIter cand[2];
    int bx, by, line_y, line_h;

    gtk_text_view_window_to_buffer_coords(tv, GTK_TEXT_WINDOW_TEXT, (int)x, (int)y, &bx, &by);
    gtk_text_view_get_iter_at_location(tv, &cand[0], bx, by);
    /* The line's height, not the character's: an image's is wrong. */
    gtk_text_view_get_line_yrange(tv, &cand[0], &line_y, &line_h);
    if (by < line_y || by >= line_y + line_h)
        return FALSE;
    cand[1] = cand[0];
    for (int i = 0; i < 2; i++) {
        GdkRectangle loc;

        if (i == 1 && (gtk_text_iter_starts_line(&cand[1]) || !gtk_text_iter_backward_char(&cand[1])))
            break;
        if (gtk_text_iter_ends_line(&cand[i]))
            continue;
        gtk_text_view_get_iter_location(tv, &cand[i], &loc);
        if (bx >= loc.x - 1 && bx <= loc.x + loc.width + 1) {
            *out = cand[i];
            return TRUE;
        }
    }
    return FALSE;
}

static const Link *link_at(KsHtmlView *hv, int off)
{
    guint lo = 0, hi = hv->links->len;

    while (lo < hi) {
        guint mid = (lo + hi) / 2;
        const Link *l = &g_array_index(hv->links, Link, mid);

        if (off < l->start)
            hi = mid;
        else if (off >= l->end)
            lo = mid + 1;
        else
            return l;
    }
    return NULL;
}

static const Image *image_at(KsHtmlView *hv, int off)
{
    for (guint i = 0; i < hv->images->len; i++)
        if (g_array_index(hv->images, Image, i).offset == off)
            return &g_array_index(hv->images, Image, i);
    return NULL;
}

/* What was clicked, handled once the click is over: following a link
 * may replace the page, and GTK2 still has a mark of the old one to clear
 * after this button release (a Gtk-CRITICAL otherwise). */
typedef struct {
    KsHtmlView *hv;
    char *href, *image;
    int x, y;
} Click;

static gboolean click_idle(gpointer data)
{
    Click *c = data;
    KsHtmlView *hv = c->hv;

    hv->click_idle = 0;
    if (c->href && hv->link_cb) {
        hv->link_cb(hv, c->href, c->x, c->y, hv->cb_data);
    } else if (c->image && hv->image_cb) {
        GBytes *bytes = hv->image_cb(hv, c->image, hv->cb_data);

        if (bytes) {
            ks_image_window(hv->text, bytes, c->image);
            g_bytes_unref(bytes);
        }
    }
    return G_SOURCE_REMOVE;
}

static void click_free(gpointer data)
{
    Click *c = data;

    g_free(c->href);
    g_free(c->image);
    g_free(c);
}

static gboolean release_cb(GtkWidget *w, GdkEventButton *e, KsHtmlView *hv)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    GtkTextIter it, s, z;
    const Link *l;
    const Image *img;
    Click *c;
    int off;

    if (e->button != 1 || gtk_text_buffer_get_selection_bounds(b, &s, &z) || !char_at(hv, e->x, e->y, &it) ||
        hv->click_idle)
        return FALSE;
    off = gtk_text_iter_get_offset(&it);
    l = link_at(hv, off);
    img = l ? NULL : image_at(hv, off);
    if (!l && !img)
        return FALSE;
    c = g_new0(Click, 1);
    c->hv = hv;
    c->href = l ? g_strdup(l->href) : NULL;
    c->image = img ? g_strdup(img->src) : NULL;
    c->x = (int)e->x_root;
    c->y = (int)e->y_root;
    hv->click_idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, click_idle, c, click_free);
    return FALSE;
}

static gboolean motion_cb(GtkWidget *w, GdkEventMotion *e, KsHtmlView *hv)
{
    GtkTextIter it;
    gboolean link = FALSE;

    if (char_at(hv, e->x, e->y, &it)) {
        int off = gtk_text_iter_get_offset(&it);

        link = link_at(hv, off) || image_at(hv, off);
    }
    if (link != hv->over_link) {
        if (!hv->hand) {
            hv->hand = gdk_cursor_new_for_display(gtk_widget_get_display(w), GDK_HAND2);
            hv->ibeam = gdk_cursor_new_for_display(gtk_widget_get_display(w), GDK_XTERM);
        }
        hv->over_link = link;
        gdk_window_set_cursor(gtk_text_view_get_window(GTK_TEXT_VIEW(w), GTK_TEXT_WINDOW_TEXT),
                              link ? hv->hand : hv->ibeam);
    }
    return FALSE;
}

/* Plain text without the direction marks. */
static void copy_cb(GtkTextView *tv, KsHtmlView *hv)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(tv);
    GtkTextIter s, e;
    char *text;
    GString *g;

    (void)hv;
    if (!gtk_text_buffer_get_selection_bounds(b, &s, &e))
        return;
    text = gtk_text_buffer_get_text(b, &s, &e, FALSE);
    g = g_string_sized_new(strlen(text));
    for (const char *p = text; *p;) {
        if (strncmp(p, LRM, 3) == 0 || strncmp(p, RLM, 3) == 0) {
            p += 3;
            continue;
        }
        g_string_append_c(g, *p++);
    }
    gtk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(tv), GDK_SELECTION_CLIPBOARD), g->str, g->len);
    g_string_free(g, TRUE);
    g_free(text);
    g_signal_stop_emission_by_name(tv, "copy-clipboard");
}

static GdkColor mix(GdkColor a, GdkColor b, double t)
{
    GdkColor c;

    c.pixel = 0;
    c.red = (guint16)(a.red * t + b.red * (1 - t));
    c.green = (guint16)(a.green * t + b.green * (1 - t));
    c.blue = (guint16)(a.blue * t + b.blue * (1 - t));
    return c;
}

static void style_set_cb(GtkWidget *w, GtkStyle *prev, KsHtmlView *hv)
{
    GtkStyle *st = gtk_widget_get_style(w);
    GdkColor dim = mix(st->text[GTK_STATE_NORMAL], st->base[GTK_STATE_NORMAL], 0.55);

    (void)prev;
    g_object_set(hv->t_head, "foreground-gdk", &dim, NULL);
    g_object_set(hv->t_note, "foreground-gdk", &dim, NULL);
}

static void destroy_cb(GtkWidget *w, KsHtmlView *hv)
{
    (void)w;
    popup_close(hv);
    if (hv->click_idle)
        g_source_remove(hv->click_idle);
    clear_page(hv);
    g_array_free(hv->links, TRUE);
    g_array_free(hv->images, TRUE);
    if (hv->b)
        g_object_unref(hv->b);
    g_object_unref(hv->table);
    if (hv->hand) {
        gdk_cursor_unref(hv->hand);
        gdk_cursor_unref(hv->ibeam);
    }
    g_free(hv);
}

/* ---- setup -------------------------------------------------------------- */

static GtkTextTag *new_tag(KsHtmlView *hv, const char *first, ...) G_GNUC_NULL_TERMINATED;

static GtkTextTag *new_tag(KsHtmlView *hv, const char *first, ...)
{
    GtkTextTag *t = gtk_text_tag_new(NULL);
    va_list ap;

    va_start(ap, first);
    g_object_set_valist(G_OBJECT(t), first, ap);
    va_end(ap);
    gtk_text_tag_table_add(hv->table, t);
    g_object_unref(t);
    return t;
}

KsHtmlView *ks_html_view_new(void)
{
    KsHtmlView *hv = g_new0(KsHtmlView, 1);
    GtkTextBuffer *b;

    hv->links = g_array_new(FALSE, FALSE, sizeof(Link));
    hv->images = g_array_new(FALSE, FALSE, sizeof(Image));
    hv->table = gtk_text_tag_table_new();

    /* Priority follows creation order: later tags win. */
    for (int i = 1; i <= MAX_INDENT; i++)
        hv->t_indent[i] = new_tag(hv, "left-margin", MARGIN + 22 * i, NULL);
    hv->t_para = new_tag(hv, "pixels-above-lines", 8, NULL);
    hv->t_center = new_tag(hv, "justification", GTK_JUSTIFY_CENTER, NULL);
    hv->t_right = new_tag(hv, "justification", GTK_JUSTIFY_RIGHT, NULL);
    hv->t_h[0] = new_tag(hv, "scale", PANGO_SCALE_X_LARGE, "pixels-above-lines", 10, NULL);
    hv->t_h[1] = new_tag(hv, "scale", PANGO_SCALE_LARGE, "pixels-above-lines", 8, NULL);
    hv->t_h[2] = new_tag(hv, "scale", 1.1, "pixels-above-lines", 6, NULL);
    hv->t_h[3] = new_tag(hv, "pixels-above-lines", 6, NULL);
    hv->t_bold = new_tag(hv, "weight", PANGO_WEIGHT_BOLD, NULL);
    hv->t_italic = new_tag(hv, "style", PANGO_STYLE_ITALIC, NULL);
    hv->t_underline = new_tag(hv, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    hv->t_strike = new_tag(hv, "strikethrough", TRUE, NULL);
    hv->t_small = new_tag(hv, "scale", PANGO_SCALE_SMALL, NULL);
    hv->t_big = new_tag(hv, "scale", PANGO_SCALE_LARGE, NULL);
    hv->t_sup = new_tag(hv, "rise", 5 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    hv->t_sub = new_tag(hv, "rise", -3 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    hv->t_link = new_tag(hv, "foreground", LINK_COLOR, NULL);
    hv->t_head = new_tag(hv, "weight", PANGO_WEIGHT_BOLD, "pixels-above-lines", 12, "pixels-below-lines", 2, NULL);
    hv->t_note = new_tag(hv, "style", PANGO_STYLE_ITALIC, NULL);

    b = gtk_text_buffer_new(hv->table);
    hv->text = gtk_text_view_new_with_buffer(b);
    g_object_unref(b);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(hv->text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(hv->text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(hv->text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(hv->text), MARGIN);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(hv->text), MARGIN);
    gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(hv->text), 1);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(hv->text), 1);

    hv->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(hv->scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(hv->scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(hv->scroll), hv->text);

    g_signal_connect(hv->text, "button-release-event", G_CALLBACK(release_cb), hv);
    g_signal_connect(hv->text, "motion-notify-event", G_CALLBACK(motion_cb), hv);
    g_signal_connect(hv->text, "copy-clipboard", G_CALLBACK(copy_cb), hv);
    g_signal_connect(hv->text, "style-set", G_CALLBACK(style_set_cb), hv);
    g_signal_connect(hv->scroll, "destroy", G_CALLBACK(destroy_cb), hv);
    return hv;
}

GtkWidget *ks_html_view_widget(KsHtmlView *hv)
{
    return hv->scroll;
}

GtkWidget *ks_html_view_text(KsHtmlView *hv)
{
    return hv->text;
}

void ks_html_view_set_callbacks(KsHtmlView *hv, KsHtmlLinkCb link, KsHtmlImageCb image, void *data)
{
    hv->link_cb = link;
    hv->image_cb = image;
    hv->cb_data = data;
}

void ks_html_view_set_font(KsHtmlView *hv, const PangoFontDescription *font)
{
    gtk_widget_modify_font(hv->text, (PangoFontDescription *)font);
}
