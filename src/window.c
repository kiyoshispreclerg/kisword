/* window.c - kisword's main window (see window.h): module/book/chapter
 * selectors, the "go to" entry, menus carrying every shortcut,
 * back/forward history, and parallel Bibles -- interleaved in the main
 * view, or in columns of their own that scroll together. */
#include "window.h"

#include "books.h"
#include "html.h"
#include "ks_i18n.h"
#include "module.h"
#include "ref.h"
#include "render.h"
#include "searchpane.h"

#include <gdk/gdkkeysyms.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define HIST_MAX 100

typedef struct {
    int book, chapter, verse;
} KsPos;

enum { MOD_COL_LABEL, MOD_COL_INDEX, MOD_COL_SENSITIVE, MOD_N_COLS };
enum { BOOK_COL_NAME, BOOK_COL_BOOK, BOOK_N_COLS };

typedef struct {
    KsConfig *cfg;
    GPtrArray *mods;
    KsModule *mod;
    KsView *view;
    KsSearchPane *search;
    GPtrArray *par;       /* parallel modules (KsModule *, not owned), open */
    GPtrArray *par_views; /* KsView * per parallel, in columns mode */
    GPtrArray *par_cols;  /* their column widgets */
    GtkWidget *columns, *main_head;
    gboolean syncing;     /* scrolling the columns from code */
    PangoFontDescription *font;

    GtkWidget *win, *mod_combo, *book_combo, *chap_spin, *entry, *status;
    GtkWidget *back_btn, *fwd_btn, *back_item, *fwd_item, *info_item, *search_item, *par_item;
    GtkListStore *mod_store, *book_store;
    GtkCheckMenuItem *chk_strong, *chk_notes, *chk_red, *chk_para;
    guint status_ctx;

    GArray *back, *fwd; /* KsPos */
    int book, chapter;
    gboolean busy;      /* widgets being updated from code: ignore their signals */
    gboolean at_result; /* showing a search result: the next one replaces it in history */
} KsWin;

/* ---- small helpers -------------------------------------------------------- */

static void status(KsWin *w, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void status(KsWin *w, const char *fmt, ...)
{
    va_list ap;
    char *msg;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_statusbar_pop(GTK_STATUSBAR(w->status), w->status_ctx);
    gtk_statusbar_push(GTK_STATUSBAR(w->status), w->status_ctx, msg);
    g_free(msg);
}

static void error_dialog(KsWin *w, const char *primary, const char *secondary)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "%s", primary);

    if (secondary)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", secondary);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static int module_index(KsWin *w, const KsModule *m)
{
    for (guint i = 0; i < w->mods->len; i++)
        if (g_ptr_array_index(w->mods, i) == m)
            return (int)i;
    return -1;
}

static KsModule *find_module(KsWin *w, const char *path)
{
    char *real = realpath(path, NULL);
    KsModule *found = NULL;

    for (guint i = 0; real && i < w->mods->len && !found; i++) {
        KsModule *m = g_ptr_array_index(w->mods, i);

        if (strcmp(m->real, real) == 0)
            found = m;
    }
    free(real);
    return found;
}

/* ---- widgets <-> state ---------------------------------------------------- */

static void fill_modules(KsWin *w)
{
    w->busy = TRUE;
    gtk_list_store_clear(w->mod_store);
    for (guint i = 0; i < w->mods->len; i++) {
        KsModule *m = g_ptr_array_index(w->mods, i);
        GtkTreeIter it;
        char *label;

        if (m->abbrev && strcmp(m->abbrev, m->title) != 0)
            label = g_markup_printf_escaped("<b>%s</b> — %s", m->abbrev, m->title);
        else
            label = g_markup_printf_escaped("<b>%s</b>", m->title);
        gtk_list_store_append(w->mod_store, &it);
        gtk_list_store_set(w->mod_store, &it, MOD_COL_LABEL, label, MOD_COL_INDEX, (int)i,
                           MOD_COL_SENSITIVE, m->supported, -1);
        g_free(label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->mod_combo), w->mod ? module_index(w, w->mod) : -1);
    w->busy = FALSE;
}

static void fill_books(KsWin *w)
{
    w->busy = TRUE;
    gtk_list_store_clear(w->book_store);
    for (int b = 1; w->mod && b <= KS_MAX_BOOK; b++) {
        GtkTreeIter it;

        if (!w->mod->nchapters[b])
            continue;
        gtk_list_store_append(w->book_store, &it);
        gtk_list_store_set(w->book_store, &it, BOOK_COL_NAME, ks_book_name(b), BOOK_COL_BOOK, b, -1);
    }
    w->busy = FALSE;
}

static void sync_widgets(KsWin *w)
{
    GtkTreeModel *model = GTK_TREE_MODEL(w->book_store);
    GtkTreeIter it;
    gboolean ok;
    char *ref;

    w->busy = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->mod_combo), w->mod ? module_index(w, w->mod) : -1);
    for (ok = gtk_tree_model_get_iter_first(model, &it); ok; ok = gtk_tree_model_iter_next(model, &it)) {
        int b;

        gtk_tree_model_get(model, &it, BOOK_COL_BOOK, &b, -1);
        if (b == w->book) {
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(w->book_combo), &it);
            break;
        }
    }
    if (w->mod) {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(w->chap_spin), 1, MAX(1, w->mod->nchapters[w->book]));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->chap_spin), w->chapter);
    }
    w->busy = FALSE;

    gtk_widget_set_sensitive(w->back_btn, w->back->len > 0);
    gtk_widget_set_sensitive(w->back_item, w->back->len > 0);
    gtk_widget_set_sensitive(w->fwd_btn, w->fwd->len > 0);
    gtk_widget_set_sensitive(w->fwd_item, w->fwd->len > 0);
    gtk_widget_set_sensitive(w->info_item, w->mod != NULL);
    gtk_widget_set_sensitive(w->search_item, w->mod != NULL);
    gtk_widget_set_sensitive(w->par_item, w->mod != NULL);

    if (w->mod) {
        ref = g_strdup_printf("%s %d", ks_book_name(w->book), w->chapter);
        status(w, "%s — %s", ref, w->mod->title);
        {
            GString *labels = g_string_new(ks_module_label(w->mod));
            char *title;

            for (guint i = 0; i < w->par->len; i++)
                g_string_append_printf(labels, " + %s", ks_module_label(g_ptr_array_index(w->par, i)));
            title = g_strdup_printf("%s (%s) — kisword", ref, labels->str);
            gtk_window_set_title(GTK_WINDOW(w->win), title);
            g_free(title);
            g_string_free(labels, TRUE);
        }
        g_free(ref);
    }
}

/* ---- navigation ----------------------------------------------------------- */

static KsPos current_pos(KsWin *w)
{
    KsPos p = { w->book, w->chapter, ks_view_top_verse(w->view) };

    return p;
}

static void hist_push(GArray *a, KsPos p)
{
    if (a->len >= HIST_MAX)
        g_array_remove_index(a, 0);
    g_array_append_val(a, p);
}

/* Shows book/chapter (clamped to what the module has). push: a jump the
 * Back button should be able to undo (book choice, "go to"); sequential
 * chapter reading does not push. */
static void navigate(KsWin *w, int book, int chapter, int scroll_verse, int hl_from, int hl_to, gboolean push)
{
    KsModule *m = w->mod;

    if (!m)
        return;
    if (book < 1 || book > KS_MAX_BOOK || !m->nchapters[book]) {
        book = ks_module_next_book(m, 0);
        chapter = 1;
        scroll_verse = hl_from = hl_to = 0;
    }
    chapter = CLAMP(chapter, 1, m->nchapters[book]);
    if (push && w->book) {
        hist_push(w->back, current_pos(w));
        g_array_set_size(w->fwd, 0);
    }
    w->at_result = FALSE;
    w->book = book;
    w->chapter = chapter;
    {
        KsModule *mods[KS_VIEW_MAX_MODS] = { m };
        int n = 1;

        for (guint i = 0; w->cfg->interleave && i < w->par->len && n < KS_VIEW_MAX_MODS; i++)
            mods[n++] = g_ptr_array_index(w->par, i);
        w->syncing = TRUE;
        ks_view_show(w->view, mods, n, book, chapter, scroll_verse, hl_from, hl_to);
        for (guint i = 0; i < w->par_views->len; i++)
            ks_view_show(g_ptr_array_index(w->par_views, i), (KsModule **)&g_ptr_array_index(w->par, i), 1, book,
                         chapter, scroll_verse, hl_from, hl_to);
        w->syncing = FALSE;
    }
    ks_search_pane_set_book(w->search, book);
    sync_widgets(w);
}

static void step_chapter(KsWin *w, int dir)
{
    int b = w->book, c = w->chapter + dir;

    if (!w->mod)
        return;
    if (c < 1) {
        b = ks_module_prev_book(w->mod, b);
        if (!b)
            return;
        c = w->mod->nchapters[b];
    } else if (c > w->mod->nchapters[b]) {
        b = ks_module_next_book(w->mod, b);
        if (!b)
            return;
        c = 1;
    }
    navigate(w, b, c, 0, 0, 0, FALSE);
}

static void hist_go(KsWin *w, GArray *from, GArray *to)
{
    KsPos p;

    if (!w->mod || !from->len)
        return;
    p = g_array_index(from, KsPos, from->len - 1);
    g_array_set_size(from, from->len - 1);
    hist_push(to, current_pos(w));
    navigate(w, p.book, p.chapter, p.verse, 0, 0, FALSE);
}

/* ---- parallel Bibles ------------------------------------------------------ */

static void parallels_changed(KsWin *w, gboolean show);

/* Columns scroll together: the verse at the top of the one scrolled, and
 * how far into it, goes to the others. */
static void on_scrolled(GtkAdjustment *adj, KsWin *w)
{
    KsView *src = NULL;
    int verse;
    double frac;

    if (w->syncing || !w->par_views->len)
        return;
    if (ks_view_vadjustment(w->view) == adj)
        src = w->view;
    for (guint i = 0; !src && i < w->par_views->len; i++)
        if (ks_view_vadjustment(g_ptr_array_index(w->par_views, i)) == adj)
            src = g_ptr_array_index(w->par_views, i);
    if (!src || !ks_view_get_anchor(src, &verse, &frac))
        return;
    w->syncing = TRUE;
    if (src != w->view)
        ks_view_set_anchor(w->view, verse, frac);
    for (guint i = 0; i < w->par_views->len; i++)
        if (g_ptr_array_index(w->par_views, i) != src)
            ks_view_set_anchor(g_ptr_array_index(w->par_views, i), verse, frac);
    w->syncing = FALSE;
}

static void remove_parallel(KsWin *w, KsModule *m)
{
    if (g_ptr_array_remove(w->par, m)) {
        ks_module_close(m);
        parallels_changed(w, TRUE);
    }
}

static void on_column_close(GtkWidget *b, KsWin *w)
{
    remove_parallel(w, g_object_get_data(G_OBJECT(b), "module"));
}

static GtkWidget *column_head(KsModule *m, KsWin *w)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 2), *label = gtk_label_new(NULL), *b;
    char *markup = g_markup_printf_escaped("<b>%s</b>", ks_module_label(m));

    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(label, m->title);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    if (w) {
        b = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU));
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(b, _("Close this parallel Bible"));
        g_object_set_data(G_OBJECT(b), "module", m);
        g_signal_connect(b, "clicked", G_CALLBACK(on_column_close), w);
        gtk_box_pack_end(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    g_free(markup);
    return box;
}

static void set_main_head(KsWin *w, gboolean show)
{
    GtkWidget *parent = gtk_widget_get_parent(ks_view_widget(w->view));
    GtkWidget *head;

    if (w->main_head) {
        gtk_widget_destroy(w->main_head);
        w->main_head = NULL;
    }
    if (!show || !w->mod)
        return;
    head = column_head(w->mod, NULL);
    gtk_widget_set_size_request(head, -1, 24);
    gtk_box_pack_start(GTK_BOX(parent), head, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(parent), head, 0);
    gtk_widget_show_all(head);
    w->main_head = head;
}

/* The columns for w->par (columns mode) and the config; show: the chapter
 * again. */
static void parallels_changed(KsWin *w, gboolean show)
{
    gboolean columns = !w->cfg->interleave && w->par->len;
    char **paths;

    for (guint i = 0; i < w->par_views->len; i++)
        ks_view_free(g_ptr_array_index(w->par_views, i));
    for (guint i = 0; i < w->par_cols->len; i++)
        gtk_widget_destroy(g_ptr_array_index(w->par_cols, i));
    g_ptr_array_set_size(w->par_views, 0);
    g_ptr_array_set_size(w->par_cols, 0);

    for (guint i = 0; columns && i < w->par->len; i++) {
        KsModule *m = g_ptr_array_index(w->par, i);
        KsViewOptions opt = { w->cfg->show_strong, w->cfg->show_notes, w->cfg->red_letters, w->cfg->paragraphs };
        KsView *v = ks_view_new(&opt);
        GtkWidget *col = gtk_vbox_new(FALSE, 0), *head = column_head(m, w);

        gtk_widget_set_size_request(head, -1, 24);
        ks_view_set_font(v, w->font);
        gtk_box_pack_start(GTK_BOX(col), head, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(col), ks_view_widget(v), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(w->columns), col, TRUE, TRUE, 0);
        gtk_widget_show_all(col);
        g_signal_connect(ks_view_vadjustment(v), "value-changed", G_CALLBACK(on_scrolled), w);
        g_ptr_array_add(w->par_views, v);
        g_ptr_array_add(w->par_cols, col);
    }
    set_main_head(w, columns);

    paths = g_new0(char *, w->par->len + 1);
    for (guint i = 0; i < w->par->len; i++)
        paths[i] = g_strdup(((KsModule *)g_ptr_array_index(w->par, i))->real);
    g_strfreev(w->cfg->parallel);
    w->cfg->parallel = paths;

    if (show && w->mod && w->book)
        navigate(w, w->book, w->chapter, ks_view_top_verse(w->view), 0, 0, FALSE);
}

/* Opens m as a parallel; FALSE (with a dialog) if it can't be. */
static gboolean add_parallel(KsWin *w, KsModule *m, gboolean quiet)
{
    GError *err = NULL;

    if (m == w->mod || !m->supported || g_ptr_array_find(w->par, m, NULL) || w->par->len >= KS_VIEW_MAX_MODS - 1)
        return FALSE;
    if (!ks_module_open(m, &err)) {
        if (!quiet)
            error_dialog(w, _("Could not open the module"), err->message);
        g_error_free(err);
        return FALSE;
    }
    g_ptr_array_add(w->par, m);
    return TRUE;
}

enum { PAR_COL_ON, PAR_COL_LABEL, PAR_COL_INDEX, PAR_N_COLS };

static void on_par_toggled(GtkCellRendererToggle *cell, char *path, GtkListStore *store)
{
    GtkTreeIter it;
    gboolean on;

    (void)cell;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &it, path))
        return;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &it, PAR_COL_ON, &on, -1);
    gtk_list_store_set(store, &it, PAR_COL_ON, !on, -1);
}

/* View > Parallel Bibles: which ones, and columns or interleaved. */
static void cmd_parallel(KsWin *w)
{
    GtkWidget *d, *box, *sw, *tree, *cols, *inter, *label;
    GtkListStore *store;
    GtkCellRenderer *cell;
    GtkTreeIter it;
    gboolean ok;

    if (!w->mod)
        return;
    d = gtk_dialog_new_with_buttons(_("Parallel Bibles"), GTK_WINDOW(w->win), GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(d), 460, 480);
    box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    label = gtk_label_new(NULL);
    {
        char *text = g_strdup_printf(_("Show these Bibles along with %s (up to %d):"), ks_module_label(w->mod),
                                     KS_VIEW_MAX_MODS - 1);

        gtk_label_set_text(GTK_LABEL(label), text);
        g_free(text);
    }
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    /* The ones shown now first, in their order, then the rest. */
    store = gtk_list_store_new(PAR_N_COLS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_INT);
    for (guint k = 0; k < w->par->len + w->mods->len; k++) {
        KsModule *m = k < w->par->len ? g_ptr_array_index(w->par, k) : g_ptr_array_index(w->mods, k - w->par->len);
        int i = module_index(w, m);
        char *markup;

        if (m == w->mod || !m->supported || (k >= w->par->len && g_ptr_array_find(w->par, m, NULL)))
            continue;
        markup = m->abbrev && strcmp(m->abbrev, m->title) != 0
                     ? g_markup_printf_escaped("<b>%s</b> — %s", m->abbrev, m->title)
                     : g_markup_printf_escaped("<b>%s</b>", m->title);
        gtk_list_store_insert_with_values(store, &it, -1, PAR_COL_ON, k < w->par->len, PAR_COL_LABEL, markup,
                                          PAR_COL_INDEX, i, -1);
        g_free(markup);
    }
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    cell = gtk_cell_renderer_toggle_new();
    g_signal_connect(cell, "toggled", G_CALLBACK(on_par_toggled), store);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, NULL, cell, "active", PAR_COL_ON, NULL);
    cell = gtk_cell_renderer_text_new();
    g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, NULL, cell, "markup", PAR_COL_LABEL,
                                                NULL);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), tree);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);

    cols = gtk_radio_button_new_with_mnemonic(NULL, _("Side by side, in _columns"));
    inter = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(cols),
                                                           _("_Interleaved, verse by verse"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cfg->interleave ? inter : cols), TRUE);
    gtk_box_pack_start(GTK_BOX(box), cols, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), inter, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), box, TRUE, TRUE, 0);
    gtk_widget_show_all(box);

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        GPtrArray *old = g_ptr_array_new();

        for (guint i = 0; i < w->par->len; i++)
            g_ptr_array_add(old, g_ptr_array_index(w->par, i));
        g_ptr_array_set_size(w->par, 0);
        for (ok = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &it); ok;
             ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &it)) {
            gboolean on;
            int idx;

            gtk_tree_model_get(GTK_TREE_MODEL(store), &it, PAR_COL_ON, &on, PAR_COL_INDEX, &idx, -1);
            if (on && w->par->len >= KS_VIEW_MAX_MODS - 1) {
                status(w, _("At most %d parallel Bibles; the others were left out."), KS_VIEW_MAX_MODS - 1);
                break;
            }
            if (on)
                add_parallel(w, g_ptr_array_index(w->mods, idx), FALSE);
        }
        for (guint i = 0; i < old->len; i++)
            if (!g_ptr_array_find(w->par, g_ptr_array_index(old, i), NULL))
                ks_module_close(g_ptr_array_index(old, i));
        g_ptr_array_free(old, TRUE);
        w->cfg->interleave = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(inter));
        parallels_changed(w, TRUE);
    }
    g_object_unref(store);
    gtk_widget_destroy(d);
}

/* show: render right away (FALSE only at startup, where the first
 * position is decided afterwards and rendered once). */
static gboolean switch_module(KsWin *w, KsModule *m, gboolean show)
{
    GError *err = NULL;
    int top;
    KsModule *old = w->mod;

    if (m == old)
        return TRUE;
    if (!m->supported) {
        error_dialog(w, m->title, m->unsupported_why);
        return FALSE;
    }
    if (!ks_module_open(m, &err)) {
        error_dialog(w, _("Could not open the module"), err->message);
        g_error_free(err);
        return FALSE;
    }
    top = old ? ks_view_top_verse(w->view) : 0;
    w->mod = m;
    g_ptr_array_remove(w->par, m); /* a parallel becoming the main one */
    if (w->par->len || w->main_head)
        parallels_changed(w, FALSE); /* also relabels the main column */
    if (old && !g_ptr_array_find(w->par, old, NULL))
        ks_module_close(old);
    fill_books(w);
    ks_search_pane_set_module(w->search, m);
    gtk_widget_set_sensitive(w->book_combo, TRUE);
    gtk_widget_set_sensitive(w->chap_spin, TRUE);
    if (show)
        navigate(w, w->book, w->chapter, top, 0, 0, FALSE);

    g_free(w->cfg->module);
    w->cfg->module = g_strdup(m->real);
    return TRUE;
}

/* ---- callbacks ------------------------------------------------------------ */

static void on_mod_changed(GtkComboBox *c, KsWin *w)
{
    int i = gtk_combo_box_get_active(c);

    if (w->busy || i < 0 || i >= (int)w->mods->len)
        return;
    if (!switch_module(w, g_ptr_array_index(w->mods, i), TRUE))
        sync_widgets(w); /* back to the module still shown */
}

static void on_book_changed(GtkComboBox *c, KsWin *w)
{
    GtkTreeIter it;
    int b;

    if (w->busy || !gtk_combo_box_get_active_iter(c, &it))
        return;
    gtk_tree_model_get(GTK_TREE_MODEL(w->book_store), &it, BOOK_COL_BOOK, &b, -1);
    if (b != w->book)
        navigate(w, b, 1, 0, 0, 0, TRUE);
}

static void on_chapter_changed(GtkSpinButton *s, KsWin *w)
{
    int c = gtk_spin_button_get_value_as_int(s);

    if (!w->busy && c != w->chapter)
        navigate(w, w->book, c, 0, 0, 0, FALSE);
}

static gboolean go_to_ref(KsWin *w, const char *text)
{
    KsRef r;
    int book;

    if (!w->mod)
        return FALSE;
    if (!ks_ref_parse(text, &r)) {
        status(w, _("Reference not recognized: %s"), text);
        return FALSE;
    }
    book = r.book ? r.book : w->book;
    if (book > KS_MAX_BOOK || !w->mod->nchapters[book]) {
        status(w, _("%s is not in this module."), ks_book_name(book));
        return FALSE;
    }
    navigate(w, book, r.chapter, 0, r.verse, r.verse_end, TRUE);
    gtk_widget_grab_focus(ks_view_text(w->view));
    return TRUE;
}

static void on_entry_activate(GtkEntry *e, KsWin *w)
{
    go_to_ref(w, gtk_entry_get_text(e));
}

static void apply_options(KsWin *w)
{
    KsViewOptions o = {
        gtk_check_menu_item_get_active(w->chk_strong),
        gtk_check_menu_item_get_active(w->chk_notes),
        gtk_check_menu_item_get_active(w->chk_red),
        gtk_check_menu_item_get_active(w->chk_para),
    };

    w->cfg->show_strong = o.show_strong;
    w->cfg->show_notes = o.show_notes;
    w->cfg->red_letters = o.red_letters;
    w->cfg->paragraphs = o.paragraphs;
    w->syncing = TRUE;
    ks_view_set_options(w->view, &o);
    for (guint i = 0; i < w->par_views->len; i++)
        ks_view_set_options(g_ptr_array_index(w->par_views, i), &o);
    w->syncing = FALSE;
}

static void set_font(KsWin *w, PangoFontDescription *d)
{
    if (w->font)
        pango_font_description_free(w->font);
    w->font = d;
    ks_view_set_font(w->view, d);
    for (guint i = 0; i < w->par_views->len; i++)
        ks_view_set_font(g_ptr_array_index(w->par_views, i), d);
    g_free(w->cfg->font);
    w->cfg->font = pango_font_description_to_string(d);
}

static void zoom(KsWin *w, int delta)
{
    PangoFontDescription *d = pango_font_description_copy(w->font);
    int size = pango_font_description_get_size(d) / PANGO_SCALE;

    if (delta) {
        size = CLAMP((size > 0 ? size : 12) + delta, 6, 48);
    } else {
        PangoFontDescription *def = pango_font_description_from_string(KS_DEFAULT_FONT);

        size = pango_font_description_get_size(def) / PANGO_SCALE;
        pango_font_description_free(def);
    }
    pango_font_description_set_size(d, size * PANGO_SCALE);
    set_font(w, d);
}

static void save_state(KsWin *w)
{
    if (w->mod) {
        w->cfg->book = w->book;
        w->cfg->chapter = w->chapter;
        w->cfg->verse = ks_view_top_verse(w->view);
    }
    gtk_window_get_size(GTK_WINDOW(w->win), &w->cfg->width, &w->cfg->height);
    ks_config_save(w->cfg);
}

/* Menu commands, connected swapped: they get the KsWin first. */
static void cmd_quit(KsWin *w)
{
    save_state(w);
    gtk_main_quit();
}

/* Logout or `kill`: still save the position and settings. */
static gboolean on_sigterm(gpointer data)
{
    cmd_quit(data);
    return G_SOURCE_REMOVE;
}

static gboolean on_delete(GtkWidget *win, GdkEvent *e, KsWin *w)
{
    (void)win;
    (void)e;
    cmd_quit(w);
    return FALSE;
}

static void cmd_open(KsWin *w)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(_("Open MySword Bible module"), GTK_WINDOW(w->win),
                                               GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *f = gtk_file_filter_new();

    gtk_file_filter_set_name(f, _("MySword Bibles (*.bbl.mybible)"));
    gtk_file_filter_add_pattern(f, "*.bbl.mybible");
    gtk_file_filter_add_pattern(f, "*.BBL.MYBIBLE");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        KsModule *m = find_module(w, path);

        gtk_widget_hide(d);
        if (!m) {
            GError *err = NULL;

            m = ks_module_probe(path, &err);
            if (m) {
                g_ptr_array_add(w->mods, m);
                ks_config_add_module_file(w->cfg, path);
                fill_modules(w);
            } else {
                error_dialog(w, _("Could not open the module"), err->message);
                g_error_free(err);
            }
        }
        if (m && switch_module(w, m, TRUE))
            sync_widgets(w);
        g_free(path);
    }
    gtk_widget_destroy(d);
}

static void cmd_font(KsWin *w)
{
    GtkWidget *d = gtk_font_selection_dialog_new(_("Text font"));
    char *cur = pango_font_description_to_string(w->font);

    gtk_window_set_transient_for(GTK_WINDOW(d), GTK_WINDOW(w->win));
    gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(d), cur);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *name = gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(d));

        set_font(w, pango_font_description_from_string(name));
        g_free(name);
    }
    g_free(cur);
    gtk_widget_destroy(d);
}

/* A result picked in the search panel. Going through the results one by
 * one leaves a single entry in the history, the place the search began. */
static void search_go(int book, int chapter, int verse, void *data)
{
    KsWin *w = data;

    navigate(w, book, chapter, 0, verse, 0, !w->at_result);
    w->at_result = TRUE;
}

static void cmd_search(KsWin *w)
{
    if (w->mod)
        ks_search_pane_focus(w->search);
}

static void on_search_hidden(KsWin *w)
{
    gtk_widget_grab_focus(ks_view_text(w->view));
}

static void cmd_back(KsWin *w) { hist_go(w, w->back, w->fwd); }
static void cmd_forward(KsWin *w) { hist_go(w, w->fwd, w->back); }
static void cmd_prev_chapter(KsWin *w) { step_chapter(w, -1); }
static void cmd_next_chapter(KsWin *w) { step_chapter(w, +1); }
static void cmd_zoom_in(KsWin *w) { zoom(w, +1); }
static void cmd_zoom_out(KsWin *w) { zoom(w, -1); }
static void cmd_zoom_reset(KsWin *w) { zoom(w, 0); }

static void cmd_goto(KsWin *w)
{
    gtk_widget_grab_focus(w->entry);
    gtk_editable_select_region(GTK_EDITABLE(w->entry), 0, -1);
}

static void cmd_about(KsWin *w)
{
    gtk_show_about_dialog(GTK_WINDOW(w->win), "program-name", "kisword", "version", KS_VERSION,
                          "comments", _("A light Bible reader for MySword modules."), NULL);
}

/* ---- module information ---------------------------------------------------- */

typedef struct {
    GtkTextBuffer *b;
    GtkTextTag *tags[7]; /* by KS_HTML_* bit */
} InfoText;

static void info_html_cb(const char *text, int len, unsigned style, void *data)
{
    InfoText *it = data;
    GtkTextIter end, start;
    int off;

    gtk_text_buffer_get_end_iter(it->b, &end);
    off = gtk_text_iter_get_offset(&end);
    gtk_text_buffer_insert(it->b, &end, text, len);
    gtk_text_buffer_get_iter_at_offset(it->b, &start, off);
    for (int i = 0; i < 7; i++)
        if (style & (1u << i))
            gtk_text_buffer_apply_tag(it->b, it->tags[i], &start, &end);
}

static void info_insert(GtkTextBuffer *b, const char *text, const char *tag)
{
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(b, &end);
    if (tag)
        gtk_text_buffer_insert_with_tags_by_name(b, &end, text, -1, tag, NULL);
    else
        gtk_text_buffer_insert(b, &end, text, -1);
}

static void info_field(GtkTextBuffer *b, const char *label, const char *value)
{
    char *l;

    if (!value || !*value)
        return;
    l = g_strdup_printf("%s: ", label);
    info_insert(b, l, "label");
    info_insert(b, value, NULL);
    info_insert(b, "\n", NULL);
    g_free(l);
}

/* Help > About the module: what Details says, with Comments as rich text. */
static void cmd_module_info(KsWin *w)
{
    static const struct { const char *key, *label; } fields[] = {
        { "description", N_("Description") }, { "abbreviation", N_("Abbreviation") },
        { "language", N_("Language") },       { "version", N_("Version") },
        { "versiondate", N_("Version date") }, { "publishdate", N_("Published") },
        { "publisher", N_("Publisher") },     { "author", N_("Author") },
        { "creator", N_("Creator") },         { "source", N_("Source") },
    };
    KsModule *m = w->mod;
    GHashTable *d;
    GtkWidget *dlg, *sw, *tv;
    GtkTextBuffer *b;
    InfoText it;
    GString *contents;
    int books = 0;
    const char *comments, *editorial;

    if (!m)
        return;
    d = ks_module_details(m);
    if (!d)
        d = g_hash_table_new(g_str_hash, g_str_equal);

    dlg = gtk_dialog_new_with_buttons(_("About the module"), GTK_WINDOW(w->win), GTK_DIALOG_DESTROY_WITH_PARENT,
                                      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 520);
    tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(tv), 2);
    b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_text_buffer_create_tag(b, "title", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_X_LARGE,
                               "pixels-above-lines", 8, "pixels-below-lines", 8, NULL);
    gtk_text_buffer_create_tag(b, "label", "weight", PANGO_WEIGHT_BOLD, NULL);
    it.b = b;
    it.tags[0] = gtk_text_buffer_create_tag(b, NULL, "weight", PANGO_WEIGHT_BOLD, NULL);
    it.tags[1] = gtk_text_buffer_create_tag(b, NULL, "style", PANGO_STYLE_ITALIC, NULL);
    it.tags[2] = gtk_text_buffer_create_tag(b, NULL, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    it.tags[3] = gtk_text_buffer_create_tag(b, NULL, "rise", 5 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    it.tags[4] = gtk_text_buffer_create_tag(b, NULL, "rise", -3 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    it.tags[5] = gtk_text_buffer_create_tag(b, NULL, "scale", PANGO_SCALE_LARGE, NULL);
    it.tags[6] = gtk_text_buffer_create_tag(b, NULL, "foreground", "#3b78c4", NULL);

    info_insert(b, m->title, "title");
    info_insert(b, "\n", NULL);
    for (size_t i = 0; i < G_N_ELEMENTS(fields); i++) {
        const char *v = g_hash_table_lookup(d, fields[i].key);

        char *date = NULL;

        if (strcmp(fields[i].key, "description") == 0 && v && strcmp(v, m->title) == 0)
            continue;
        /* "2013-11-07 00:00:00": dates stored as timestamps. */
        if (v && g_str_has_suffix(v, " 00:00:00"))
            v = date = g_strndup(v, strlen(v) - 9);
        info_field(b, _(fields[i].label), v);
        g_free(date);
    }

    contents = g_string_new(NULL);
    for (int i = 1; i <= KS_MAX_BOOK; i++)
        books += m->nchapters[i] != 0;
    if (books)
        g_string_append_printf(contents, ngettext("%d book", "%d books", books), books);
    if (m->strong)
        g_string_append_printf(contents, "%s%s", contents->len ? ", " : "", _("Strong's numbers"));
    if (m->rtl || m->rtl_ot > 0 || m->rtl_nt > 0)
        g_string_append_printf(contents, "%s%s", contents->len ? ", " : "", _("right to left"));
    if (m->rules)
        g_string_append_printf(contents, "%s%s", contents->len ? ", " : "", _("verse rules"));
    info_field(b, _("Contents"), contents->str);
    g_string_free(contents, TRUE);
    info_field(b, _("File"), m->path);

    comments = g_hash_table_lookup(d, "comments");
    editorial = g_hash_table_lookup(d, "editorialcomments");
    if (comments) {
        info_insert(b, "\n", NULL);
        ks_html_parse(comments, info_html_cb, &it);
        info_insert(b, "\n", NULL);
    }
    if (editorial) {
        info_insert(b, "\n", NULL);
        info_insert(b, _("Editorial comments"), "label");
        info_insert(b, "\n", NULL);
        ks_html_parse(editorial, info_html_cb, &it);
    }
    g_hash_table_destroy(d);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_set_border_width(GTK_CONTAINER(sw), 6);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), sw, TRUE, TRUE, 0);
    gtk_widget_show_all(sw);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* ---- building ------------------------------------------------------------- */

static GtkWidget *menu_item(GtkWidget *menu, GtkAccelGroup *ag, const char *label, guint key,
                            GdkModifierType mods, void (*cmd)(KsWin *), KsWin *w)
{
    GtkWidget *mi = gtk_menu_item_new_with_mnemonic(label);

    if (key)
        gtk_widget_add_accelerator(mi, "activate", ag, key, mods, GTK_ACCEL_VISIBLE);
    g_signal_connect_swapped(mi, "activate", G_CALLBACK(cmd), w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    return mi;
}

static GtkCheckMenuItem *check_item(GtkWidget *menu, GtkAccelGroup *ag, const char *label, guint key,
                                    gboolean active, KsWin *w)
{
    GtkWidget *mi = gtk_check_menu_item_new_with_mnemonic(label);

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), active);
    if (key)
        gtk_widget_add_accelerator(mi, "activate", ag, key, 0, GTK_ACCEL_VISIBLE);
    g_signal_connect_swapped(mi, "toggled", G_CALLBACK(apply_options), w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    return GTK_CHECK_MENU_ITEM(mi);
}

static GtkWidget *submenu(GtkWidget *bar, const char *label)
{
    GtkWidget *top = gtk_menu_item_new_with_mnemonic(label), *menu = gtk_menu_new();

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), top);
    return menu;
}

static void separator(GtkWidget *menu)
{
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}

static GtkWidget *build_menus(KsWin *w, GtkAccelGroup *ag)
{
    GtkWidget *bar = gtk_menu_bar_new(), *m, *mi;

    m = submenu(bar, _("_File"));
    menu_item(m, ag, _("_Open module…"), GDK_KEY_o, GDK_CONTROL_MASK, cmd_open, w);
    separator(m);
    menu_item(m, ag, _("_Quit"), GDK_KEY_q, GDK_CONTROL_MASK, cmd_quit, w);

    m = submenu(bar, _("_View"));
    w->chk_strong = check_item(m, ag, _("_Strong's numbers"), GDK_KEY_F9, w->cfg->show_strong, w);
    w->chk_notes = check_item(m, ag, _("_Footnotes"), 0, w->cfg->show_notes, w);
    w->chk_red = check_item(m, ag, _("Words of Jesus in _red"), 0, w->cfg->red_letters, w);
    w->chk_para = check_item(m, ag, _("_Paragraph layout"), 0, w->cfg->paragraphs, w);
    separator(m);
    w->par_item = menu_item(m, ag, _("Pa_rallel Bibles…"), GDK_KEY_F8, 0, cmd_parallel, w);
    separator(m);
    mi = menu_item(m, ag, _("Zoom _in"), GDK_KEY_plus, GDK_CONTROL_MASK, cmd_zoom_in, w);
    gtk_widget_add_accelerator(mi, "activate", ag, GDK_KEY_equal, GDK_CONTROL_MASK, 0);
    gtk_widget_add_accelerator(mi, "activate", ag, GDK_KEY_KP_Add, GDK_CONTROL_MASK, 0);
    mi = menu_item(m, ag, _("Zoom _out"), GDK_KEY_minus, GDK_CONTROL_MASK, cmd_zoom_out, w);
    gtk_widget_add_accelerator(mi, "activate", ag, GDK_KEY_KP_Subtract, GDK_CONTROL_MASK, 0);
    menu_item(m, ag, _("_Normal size"), GDK_KEY_0, GDK_CONTROL_MASK, cmd_zoom_reset, w);
    menu_item(m, ag, _("_Font…"), 0, 0, cmd_font, w);

    m = submenu(bar, _("_Go"));
    w->back_item = menu_item(m, ag, _("_Back"), GDK_KEY_Left, GDK_MOD1_MASK, cmd_back, w);
    w->fwd_item = menu_item(m, ag, _("_Forward"), GDK_KEY_Right, GDK_MOD1_MASK, cmd_forward, w);
    separator(m);
    menu_item(m, ag, _("_Previous chapter"), GDK_KEY_Page_Up, GDK_CONTROL_MASK, cmd_prev_chapter, w);
    menu_item(m, ag, _("_Next chapter"), GDK_KEY_Page_Down, GDK_CONTROL_MASK, cmd_next_chapter, w);
    separator(m);
    menu_item(m, ag, _("_Go to reference…"), GDK_KEY_l, GDK_CONTROL_MASK, cmd_goto, w);
    w->search_item = menu_item(m, ag, _("_Search…"), GDK_KEY_f, GDK_CONTROL_MASK, cmd_search, w);

    m = submenu(bar, _("_Help"));
    w->info_item = menu_item(m, ag, _("About the _module…"), 0, 0, cmd_module_info, w);
    menu_item(m, ag, _("_About"), 0, 0, cmd_about, w);
    return bar;
}

static GtkWidget *icon_button(const char *stock, const char *tip, void (*cmd)(KsWin *), KsWin *w)
{
    GtkWidget *b = gtk_button_new();

    gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_stock(stock, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect_swapped(b, "clicked", G_CALLBACK(cmd), w);
    return b;
}

static GtkWidget *build_toolbar(KsWin *w)
{
    GtkWidget *box = gtk_hbox_new(FALSE, 4);
    GtkCellRenderer *cell;

    gtk_container_set_border_width(GTK_CONTAINER(box), 4);

    w->mod_store = gtk_list_store_new(MOD_N_COLS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    w->mod_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(w->mod_store));
    cell = gtk_cell_renderer_text_new();
    g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 30, "single-paragraph-mode", TRUE, NULL);
    {
        /* The combo is as tall as its tallest row, and some titles are in
         * scripts with very tall glyphs (Tibetan, Burmese): rows get the
         * height of ordinary text. */
        PangoFontMetrics *fm = pango_context_get_metrics(gtk_widget_get_pango_context(w->mod_combo),
                                                         gtk_widget_get_style(w->mod_combo)->font_desc, NULL);
        int ypad;

        g_object_get(cell, "ypad", &ypad, NULL);
        gtk_cell_renderer_set_fixed_size(cell, -1, PANGO_PIXELS(pango_font_metrics_get_ascent(fm) +
                                                                pango_font_metrics_get_descent(fm)) + 2 * ypad);
        pango_font_metrics_unref(fm);
    }
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w->mod_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(w->mod_combo), cell, "markup", MOD_COL_LABEL,
                                   "sensitive", MOD_COL_SENSITIVE, NULL);
    gtk_widget_set_tooltip_text(w->mod_combo, _("Bible module"));
    g_signal_connect(w->mod_combo, "changed", G_CALLBACK(on_mod_changed), w);

    w->book_store = gtk_list_store_new(BOOK_N_COLS, G_TYPE_STRING, G_TYPE_INT);
    w->book_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(w->book_store));
    cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w->book_combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(w->book_combo), cell, "text", BOOK_COL_NAME, NULL);
    gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(w->book_combo), 4);
    gtk_widget_set_tooltip_text(w->book_combo, _("Book"));
    g_signal_connect(w->book_combo, "changed", G_CALLBACK(on_book_changed), w);

    w->chap_spin = gtk_spin_button_new_with_range(1, 150, 1);
    gtk_entry_set_width_chars(GTK_ENTRY(w->chap_spin), 3);
    gtk_widget_set_tooltip_text(w->chap_spin, _("Chapter"));
    g_signal_connect(w->chap_spin, "value-changed", G_CALLBACK(on_chapter_changed), w);

    w->back_btn = icon_button(GTK_STOCK_GO_BACK, _("Back (Alt+Left)"), cmd_back, w);
    w->fwd_btn = icon_button(GTK_STOCK_GO_FORWARD, _("Forward (Alt+Right)"), cmd_forward, w);

    w->entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry), 14);
    gtk_widget_set_tooltip_text(w->entry, _("Go to a reference, e.g. \"Jo 3:16\", \"1Co 13\" or \"gn 1\" (Ctrl+L)"));
    g_signal_connect(w->entry, "activate", G_CALLBACK(on_entry_activate), w);

    gtk_box_pack_start(GTK_BOX(box), w->mod_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w->book_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w->chap_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w->back_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w->fwd_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), w->entry, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), gtk_label_new(_("Go to:")), FALSE, FALSE, 0);
    return box;
}

/* One 48px icon: gtk_window_set_icon_name() hands X every size the theme
 * has, and the large ones overflow the property ("icons too large"). */
static void set_icon(GtkWidget *win)
{
    GdkPixbuf *pb = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "accessories-dictionary", 48, 0, NULL);

    if (pb) {
        gtk_window_set_icon(GTK_WINDOW(win), pb);
        g_object_unref(pb);
    }
}

static KsModule *initial_module(KsWin *w, char **files)
{
    KsModule *m = NULL;

    if (files && files[0])
        m = find_module(w, files[0]);
    if (!m && w->cfg->module)
        m = find_module(w, w->cfg->module);
    for (guint i = 0; (!m || !m->supported) && i < w->mods->len; i++)
        m = g_ptr_array_index(w->mods, i);
    return m && m->supported ? m : NULL;
}

void ks_window_run(KsConfig *cfg, char **files, const char *ref)
{
    KsWin *w = g_new0(KsWin, 1);
    GtkAccelGroup *ag = gtk_accel_group_new();
    GtkWidget *vbox, *paned;
    KsViewOptions opt = { cfg->show_strong, cfg->show_notes, cfg->red_letters, cfg->paragraphs };
    char *default_dir = ks_config_modules_dir();
    GPtrArray *dirs = g_ptr_array_new(), *extra = g_ptr_array_new();
    KsModule *m;

    w->cfg = cfg;
    w->par = g_ptr_array_new();
    w->par_views = g_ptr_array_new();
    w->par_cols = g_ptr_array_new();
    w->back = g_array_new(FALSE, FALSE, sizeof(KsPos));
    w->fwd = g_array_new(FALSE, FALSE, sizeof(KsPos));
    w->book = cfg->book;
    w->chapter = cfg->chapter;

    g_ptr_array_add(dirs, default_dir);
    for (int i = 0; cfg->modules_dirs[i]; i++)
        g_ptr_array_add(dirs, cfg->modules_dirs[i]);
    g_ptr_array_add(dirs, NULL);
    for (int i = 0; files && files[i]; i++)
        g_ptr_array_add(extra, files[i]);
    for (int i = 0; cfg->module_files[i]; i++)
        g_ptr_array_add(extra, cfg->module_files[i]);
    g_ptr_array_add(extra, NULL);
    w->mods = ks_module_scan((const char *const *)dirs->pdata, (const char *const *)extra->pdata);
    g_ptr_array_free(dirs, TRUE);
    g_ptr_array_free(extra, TRUE);

    w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->win), "kisword");
    gtk_window_set_default_size(GTK_WINDOW(w->win), cfg->width, cfg->height);
    set_icon(w->win);
    gtk_window_add_accel_group(GTK_WINDOW(w->win), ag);
    g_signal_connect(w->win, "delete-event", G_CALLBACK(on_delete), w);
    g_unix_signal_add(SIGTERM, on_sigterm, w);
    g_unix_signal_add(SIGINT, on_sigterm, w);

    w->view = ks_view_new(&opt);
    w->font = pango_font_description_from_string(cfg->font ? cfg->font : KS_DEFAULT_FONT);
    ks_view_set_font(w->view, w->font);
    w->search = ks_search_pane_new(search_go, w);
    g_signal_connect_swapped(ks_search_pane_widget(w->search), "hide", G_CALLBACK(on_search_hidden), w);

    /* The main view in a column of its own, the parallels' columns after it. */
    w->columns = gtk_hbox_new(TRUE, 2);
    {
        GtkWidget *col = gtk_vbox_new(FALSE, 0);

        gtk_box_pack_start(GTK_BOX(col), ks_view_widget(w->view), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(w->columns), col, TRUE, TRUE, 0);
    }
    g_signal_connect(ks_view_vadjustment(w->view), "value-changed", G_CALLBACK(on_scrolled), w);
    paned = gtk_hpaned_new();
    gtk_paned_pack1(GTK_PANED(paned), w->columns, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), ks_search_pane_widget(w->search), FALSE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), MAX(cfg->width - 330, 300));

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), build_menus(w, ag), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), build_toolbar(w), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);
    w->status = gtk_statusbar_new();
    w->status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(w->status), "main");
    gtk_box_pack_start(GTK_BOX(vbox), w->status, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(w->win), vbox);

    fill_modules(w);
    m = initial_module(w, files);
    if (m && switch_module(w, m, FALSE)) {
        for (int i = 0; cfg->parallel[i]; i++) {
            KsModule *p = find_module(w, cfg->parallel[i]);

            if (p)
                add_parallel(w, p, TRUE);
        }
        parallels_changed(w, FALSE); /* the chapter comes below */
        /* The status bar keeps the error if the reference is bad. */
        if (!(ref && *ref && go_to_ref(w, ref)))
            navigate(w, w->book, w->chapter, cfg->verse, 0, 0, FALSE);
    } else {
        char *msg = g_strdup_printf(_("No MySword Bible module found.\n\n"
                                      "Copy *.bbl.mybible files to %s, or use File → Open module…"),
                                    default_dir);

        ks_view_message(w->view, msg);
        g_free(msg);
        gtk_widget_set_sensitive(w->book_combo, FALSE);
        gtk_widget_set_sensitive(w->chap_spin, FALSE);
        sync_widgets(w);
    }
    g_free(default_dir);

    gtk_widget_show_all(w->win);
    gtk_widget_grab_focus(ks_view_text(w->view));
    gtk_main();
}
