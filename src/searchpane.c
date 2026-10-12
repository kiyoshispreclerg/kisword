/* searchpane.c - the search side panel (see searchpane.h). */
#include "searchpane.h"

#include "gbf.h"
#include "ks_i18n.h"
#include "ref.h"
#include "search.h"

#include <gdk/gdkkeysyms.h>
#include <string.h>

#define MAX_ROWS 10000
#define SNIPPET_CHARS 220
#define INDEX_STEP 300 /* verses per idle call: ~10 ms for the heavy modules */

enum { COL_ROW, N_COLS };
enum { RANGE_ALL, RANGE_OT, RANGE_NT, RANGE_BOOK };

struct KsSearchPane {
    KsSearchGoCb go;
    void *data;
    KsModule *mod;
    int book;

    GtkWidget *box, *entry, *mode, *whole, *range, *progress, *count, *tree;
    GtkListStore *store;
    GtkCellRenderer *cell;
    int wrap_width;

    KsSearchIndex *idx;
    guint idle;        /* indexing in progress */
    char *query;       /* last query run (or waiting for the index) */
    GArray *hits;      /* KsHit, what the list shows */
    GPtrArray *snippets; /* per row, markup, filled when the row is drawn */
    gboolean filling;  /* list being refilled: no "go" */
};

static void clear_results(KsSearchPane *p)
{
    p->filling = TRUE;
    gtk_list_store_clear(p->store);
    p->filling = FALSE;
    if (p->hits)
        g_array_set_size(p->hits, 0);
    g_ptr_array_set_size(p->snippets, 0);
    gtk_label_set_text(GTK_LABEL(p->count), "");
}

static void options(KsSearchPane *p, KsSearchOptions *o)
{
    o->mode = (KsSearchMode)gtk_combo_box_get_active(GTK_COMBO_BOX(p->mode));
    o->whole_words = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->whole));
    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(p->range))) {
    case RANGE_OT:
        o->book_from = 1;
        o->book_to = 39;
        break;
    case RANGE_NT:
        o->book_from = 40;
        o->book_to = 66;
        break;
    case RANGE_BOOK:
        o->book_from = o->book_to = MAX(p->book, 1);
        break;
    default:
        o->book_from = o->book_to = 0;
    }
}

static void show_hits(KsSearchPane *p, GArray *hits)
{
    guint shown = MIN(hits->len, MAX_ROWS);
    char *msg;

    clear_results(p);
    if (p->hits)
        g_array_free(p->hits, TRUE);
    p->hits = hits;
    g_ptr_array_set_size(p->snippets, shown);
    p->filling = TRUE;
    for (guint i = 0; i < shown; i++) {
        GtkTreeIter it;

        gtk_list_store_insert_with_values(p->store, &it, -1, COL_ROW, (int)i, -1);
    }
    p->filling = FALSE;
    if (hits->len > shown)
        msg = g_strdup_printf(_("%u verses (showing the first %u)"), hits->len, shown);
    else
        msg = g_strdup_printf(ngettext("%u verse", "%u verses", hits->len), hits->len);
    gtk_label_set_text(GTK_LABEL(p->count), msg);
    g_free(msg);
}

static void run_search(KsSearchPane *p)
{
    KsSearchOptions o;
    char testament;
    int number;

    if (!p->mod || !p->query || !*p->query)
        return;
    options(p, &o);
    if (ks_search_parse_strong(p->query, &testament, &number)) {
        show_hits(p, ks_search_strong(p->mod, testament, number, &o));
    } else if (p->idx && ks_search_index_done(p->idx)) {
        show_hits(p, ks_search_text(p->idx, p->query, &o));
    }
}

static gboolean index_step_cb(gpointer data)
{
    KsSearchPane *p = data;

    if (!ks_search_index_step(p->idx, INDEX_STEP)) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p->progress), ks_search_index_progress(p->idx));
        return G_SOURCE_CONTINUE;
    }
    p->idle = 0;
    gtk_widget_hide(p->progress);
    run_search(p);
    return G_SOURCE_REMOVE;
}

/* Runs the query now, or once the index is built. */
static void start(KsSearchPane *p)
{
    char testament;
    int number;

    if (!p->mod || !p->query || !*p->query) {
        clear_results(p);
        return;
    }
    if (ks_search_parse_strong(p->query, &testament, &number) || (p->idx && ks_search_index_done(p->idx))) {
        run_search(p);
        return;
    }
    clear_results(p);
    if (!p->idx)
        p->idx = ks_search_index_new(p->mod);
    if (!p->idle) {
        gtk_label_set_text(GTK_LABEL(p->count), _("Preparing the search…"));
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p->progress), ks_search_index_progress(p->idx));
        gtk_widget_show(p->progress);
        p->idle = g_idle_add(index_step_cb, p);
    }
}

static void on_activate(KsSearchPane *p)
{
    char *q = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(p->entry))));

    g_free(p->query);
    p->query = q;
    start(p);
}

static void on_option_changed(KsSearchPane *p)
{
    if (p->query && *p->query)
        start(p);
}

/* ---- results list --------------------------------------------------------- */

static char *snippet_markup(KsSearchPane *p, const KsHit *h)
{
    char *ref = ks_ref_format(h->book, h->chapter, h->verse, 0);
    char *raw = ks_module_verse(p->mod, h->book, h->chapter, h->verse);
    char *plain = ks_gbf_plain(raw, p->mod->css), *out;

    if (g_utf8_strlen(plain, -1) > SNIPPET_CHARS) {
        char *cut = g_utf8_offset_to_pointer(plain, SNIPPET_CHARS);

        strcpy(cut, "…");
    }
    out = g_markup_printf_escaped("<b>%s</b>  %s", ref, plain);
    g_free(plain);
    g_free(raw);
    g_free(ref);
    return out;
}

/* Verse texts are read only for the rows actually drawn. */
static void cell_data(GtkTreeViewColumn *col, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *it,
                      gpointer data)
{
    KsSearchPane *p = data;
    int row;

    (void)col;
    gtk_tree_model_get(model, it, COL_ROW, &row, -1);
    if (row < 0 || row >= (int)p->snippets->len || !p->mod) {
        g_object_set(cell, "markup", "", NULL);
        return;
    }
    if (!g_ptr_array_index(p->snippets, row))
        g_ptr_array_index(p->snippets, row) = snippet_markup(p, &g_array_index(p->hits, KsHit, row));
    g_object_set(cell, "markup", g_ptr_array_index(p->snippets, row), NULL);
}

static void on_selection(GtkTreeSelection *sel, KsSearchPane *p)
{
    GtkTreeModel *model;
    GtkTreeIter it;
    int row;

    if (p->filling || !gtk_tree_selection_get_selected(sel, &model, &it))
        return;
    gtk_tree_model_get(model, &it, COL_ROW, &row, -1);
    if (p->hits && row >= 0 && row < (int)p->hits->len) {
        const KsHit *h = &g_array_index(p->hits, KsHit, row);

        p->go(h->book, h->chapter, h->verse, p->data);
    }
}

/* Wrapped rows need the wrap width in pixels; follow the panel's width. */
static void on_tree_size(GtkWidget *w, GtkAllocation *a, KsSearchPane *p)
{
    int width = a->width - 12;

    (void)w;
    if (width > 50 && width != p->wrap_width) {
        p->wrap_width = width;
        g_object_set(p->cell, "wrap-width", width, NULL);
        gtk_tree_view_columns_autosize(GTK_TREE_VIEW(p->tree));
    }
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, KsSearchPane *p)
{
    (void)w;
    if (e->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(p->box);
        return TRUE;
    }
    return FALSE;
}

/* ---- public ------------------------------------------------------------------ */

static GtkWidget *text_combo(const char *const *items, int n)
{
    GtkWidget *c = gtk_combo_box_text_new();

    for (int i = 0; i < n; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), _(items[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), 0);
    return c;
}

KsSearchPane *ks_search_pane_new(KsSearchGoCb go, void *data)
{
    static const char *const modes[] = { N_("All words"), N_("Exact phrase"), N_("Any word") };
    static const char *const ranges[] = { N_("Whole Bible"), N_("Old Testament"), N_("New Testament"),
                                          N_("Current book") };
    KsSearchPane *p = g_new0(KsSearchPane, 1);
    GtkWidget *row, *btn, *sw;
    GtkTreeViewColumn *col;

    p->go = go;
    p->data = data;
    p->snippets = g_ptr_array_new_with_free_func(g_free);

    p->box = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(p->box), 4);
    g_signal_connect(p->box, "key-press-event", G_CALLBACK(on_key), p);

    row = gtk_hbox_new(FALSE, 2);
    p->entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(p->entry, _("Words to search for, or a Strong's number such as H430 or G26"));
    g_signal_connect_swapped(p->entry, "activate", G_CALLBACK(on_activate), p);
    gtk_box_pack_start(GTK_BOX(row), p->entry, TRUE, TRUE, 0);
    btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_stock(GTK_STOCK_FIND, GTK_ICON_SIZE_MENU));
    gtk_widget_set_tooltip_text(btn, _("Search"));
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(on_activate), p);
    gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
    btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU));
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, _("Close the search (Esc)"));
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_widget_hide), p->box);
    gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(p->box), row, FALSE, FALSE, 0);

    row = gtk_hbox_new(FALSE, 4);
    p->mode = text_combo(modes, G_N_ELEMENTS(modes));
    g_signal_connect_swapped(p->mode, "changed", G_CALLBACK(on_option_changed), p);
    gtk_box_pack_start(GTK_BOX(row), p->mode, TRUE, TRUE, 0);
    p->whole = gtk_check_button_new_with_mnemonic(_("_Whole words"));
    g_signal_connect_swapped(p->whole, "toggled", G_CALLBACK(on_option_changed), p);
    gtk_box_pack_start(GTK_BOX(row), p->whole, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(p->box), row, FALSE, FALSE, 0);

    p->range = text_combo(ranges, G_N_ELEMENTS(ranges));
    g_signal_connect_swapped(p->range, "changed", G_CALLBACK(on_option_changed), p);
    gtk_box_pack_start(GTK_BOX(p->box), p->range, FALSE, FALSE, 0);

    p->progress = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(p->progress, TRUE);
    gtk_box_pack_start(GTK_BOX(p->box), p->progress, FALSE, FALSE, 0);
    p->count = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(p->count), 0.0, 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(p->count), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(p->box), p->count, FALSE, FALSE, 0);

    p->store = gtk_list_store_new(N_COLS, G_TYPE_INT);
    p->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(p->tree), FALSE);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(p->tree), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(p->tree), FALSE);
    p->cell = gtk_cell_renderer_text_new();
    g_object_set(p->cell, "wrap-mode", PANGO_WRAP_WORD_CHAR, "wrap-width", 200, "ypad", 3, NULL);
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, p->cell, TRUE);
    gtk_tree_view_column_set_cell_data_func(col, p->cell, cell_data, p, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(p->tree), col);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->tree)), "changed", G_CALLBACK(on_selection), p);
    g_signal_connect(p->tree, "size-allocate", G_CALLBACK(on_tree_size), p);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), p->tree);
    gtk_box_pack_start(GTK_BOX(p->box), sw, TRUE, TRUE, 0);

    gtk_widget_show_all(p->box);
    gtk_widget_set_no_show_all(p->box, TRUE);
    gtk_widget_hide(p->box);
    return p;
}

GtkWidget *ks_search_pane_widget(KsSearchPane *p)
{
    return p->box;
}

void ks_search_pane_set_module(KsSearchPane *p, KsModule *m)
{
    if (m == p->mod)
        return;
    if (p->idle)
        g_source_remove(p->idle);
    p->idle = 0;
    gtk_widget_hide(p->progress);
    g_clear_pointer(&p->idx, ks_search_index_free);
    clear_results(p);
    p->mod = m;
    if (gtk_widget_get_visible(p->box))
        start(p);
}

void ks_search_pane_set_book(KsSearchPane *p, int book)
{
    p->book = book; /* used at the next search, not by re-running this one */
}

void ks_search_pane_focus(KsSearchPane *p)
{
    gtk_widget_show(p->box);
    gtk_widget_grab_focus(p->entry);
    gtk_editable_select_region(GTK_EDITABLE(p->entry), 0, -1);
}

gboolean ks_search_pane_visible(KsSearchPane *p)
{
    return gtk_widget_get_visible(p->box);
}
