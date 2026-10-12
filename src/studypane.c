/* studypane.c - commentaries, dictionaries, books and journals beside the
 * Bible (see studypane.h).
 *
 * One tab per kind of module, each with its module chooser and an
 * htmlview page. Tabs do nothing until first shown: with the panel hidden
 * kisword starts as fast as before. Modules are opened when first used and
 * stay open; they are read-only SQLite files, cheap to keep. */
#include "studypane.h"

#include "htmlview.h"
#include "infodialog.h"
#include "ks_i18n.h"
#include "link.h"

#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>

#define WORDS_PAGE 200
#define HIST_MAX 50

enum { MOD_COL_LABEL, MOD_COL_RES, MOD_COL_SENSITIVE, MOD_N_COLS };
enum { LIST_COL_TEXT, LIST_COL_KEY, LIST_N_COLS };

typedef enum { PREFER_NONE, PREFER_STRONG, PREFER_MORPH } Prefer;

typedef struct {
    KsRes *res;
    char *key;
} Visit;

typedef struct {
    int from, to;
} Range;

typedef struct {
    KsStudy *s;
    KsResKind kind;
    gboolean ready; /* filled in once, the first time it is shown */
    GtkWidget *page, *combo, *entry, *tree, *back_btn, *prev_btn, *next_btn, *info_btn;
    GtkListStore *mods, *list;
    KsHtmlView *view;
    KsRes *res;     /* the module chosen, open */
    char *key;      /* the word or entry id shown */
    GArray *back;   /* Visit */
    gboolean list_full; /* dictionary: no words past the last row */
    guint filter_id;

    /* Commentary: the place wanted and the page shown. */
    int book, chapter, verse;
    KsRes *shown_res;
    int shown_book, shown_chapter;
    GArray *ranges; /* Range of each comment on the page */
} Tab;

struct KsStudy {
    KsConfig *cfg;
    GPtrArray *mods;
    KsStudyCallbacks cb;
    void *data;
    GtkWidget *notebook;
    Tab tabs[KS_RES_N];
    gboolean busy; /* widgets being changed from code */
};

static void status(KsStudy *s, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void status(KsStudy *s, const char *fmt, ...)
{
    va_list ap;
    char *msg;

    if (!s->cb.status)
        return;
    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    s->cb.status(msg, s->data);
    g_free(msg);
}

static void tab_ready(Tab *t);
static void commentary_refresh(Tab *t);

/* ---- modules -------------------------------------------------------------- */

static KsRes *find_res(KsStudy *s, int kind, const char *name)
{
    for (guint i = 0; name && i < s->mods->len; i++) {
        KsRes *r = g_ptr_array_index(s->mods, i);

        if ((kind < 0 || (int)r->kind == kind || (kind == KS_RES_BOOK && r->kind == KS_RES_JOURNAL)) &&
            r->supported && ks_res_named(r, name))
            return r;
    }
    return NULL;
}

static KsRes *find_path(KsStudy *s, KsResKind kind, const char *path)
{
    for (guint i = 0; path && i < s->mods->len; i++) {
        KsRes *r = g_ptr_array_index(s->mods, i);

        if (r->kind == kind && strcmp(r->real, path) == 0)
            return r;
    }
    return NULL;
}

static void fill_mods(Tab *t)
{
    KsStudy *s = t->s;
    GtkTreeIter it;
    int active = -1, n = 0;

    s->busy = TRUE;
    gtk_list_store_clear(t->mods);
    for (guint i = 0; i < s->mods->len; i++) {
        KsRes *r = g_ptr_array_index(s->mods, i);
        char *label;

        if (r->kind != t->kind)
            continue;
        if (r->abbrev && strcmp(r->abbrev, r->title) != 0)
            label = g_markup_printf_escaped("<b>%s</b> — %s", r->abbrev, r->title);
        else
            label = g_markup_printf_escaped("<b>%s</b>", r->title);
        gtk_list_store_insert_with_values(t->mods, &it, -1, MOD_COL_LABEL, label, MOD_COL_RES, r, MOD_COL_SENSITIVE,
                                          r->supported, -1);
        if (r == t->res)
            active = n;
        n++;
        g_free(label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(t->combo), active);
    gtk_widget_set_sensitive(t->combo, n > 0);
    s->busy = FALSE;
}

static void sync_buttons(Tab *t)
{
    if (t->back_btn)
        gtk_widget_set_sensitive(t->back_btn, t->back->len > 0);
    gtk_widget_set_sensitive(t->info_btn, t->res != NULL);
}

/* Makes r the tab's module (opening it); FALSE, with a message, if it
 * can't be opened. user: chosen in the combo, so remembered as the
 * preferred dictionary for Strong's numbers too. */
static gboolean use_module(Tab *t, KsRes *r, gboolean user)
{
    KsStudy *s = t->s;
    GError *err = NULL;

    if (r == t->res)
        return TRUE;
    if (!r->supported) {
        status(s, "%s: %s", ks_res_label(r), r->unsupported_why);
        return FALSE;
    }
    if (!ks_res_open(r, &err)) {
        status(s, "%s", err->message);
        g_error_free(err);
        return FALSE;
    }
    t->res = r;
    g_free(s->cfg->res_module[t->kind]);
    s->cfg->res_module[t->kind] = g_strdup(r->real);
    if (user && r->kind == KS_RES_DICT && r->strong) {
        g_free(s->cfg->strong_dict);
        s->cfg->strong_dict = g_strdup(r->real);
    }
    fill_mods(t);
    sync_buttons(t);
    return TRUE;
}

static void push_visit(Tab *t)
{
    Visit v;

    if (!t->res || !t->key)
        return;
    if (t->back->len >= HIST_MAX) {
        g_free(g_array_index(t->back, Visit, 0).key);
        g_array_remove_index(t->back, 0);
    }
    v.res = t->res;
    v.key = g_strdup(t->key);
    g_array_append_val(t->back, v);
}

/* ---- dictionary --------------------------------------------------------- */

static void list_append(Tab *t, GPtrArray *words)
{
    GtkTreeIter it;

    for (guint i = 0; i < words->len; i++)
        gtk_list_store_insert_with_values(t->list, &it, -1, LIST_COL_TEXT, g_ptr_array_index(words, i), LIST_COL_KEY,
                                          g_ptr_array_index(words, i), -1);
    t->list_full = words->len < WORDS_PAGE;
}

/* The word list from the first word >= from; the first row selected if it
 * is that word. */
static void words_fill(Tab *t, const char *from)
{
    GPtrArray *words = t->res ? ks_res_words(t->res, from, FALSE, WORDS_PAGE) : g_ptr_array_new();
    GtkTreePath *path;

    t->s->busy = TRUE;
    gtk_list_store_clear(t->list);
    list_append(t, words);
    path = gtk_tree_path_new_first();
    if (words->len && from && g_ascii_strcasecmp(g_ptr_array_index(words, 0), from) == 0)
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(t->tree), path, NULL, FALSE);
    if (words->len)
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(t->tree), path, NULL, TRUE, 0.0, 0.0);
    gtk_tree_path_free(path);
    t->s->busy = FALSE;
    g_ptr_array_free(words, TRUE);
}

static void words_more(Tab *t)
{
    GtkTreeModel *model = GTK_TREE_MODEL(t->list);
    int n = gtk_tree_model_iter_n_children(model, NULL);
    GtkTreeIter it;
    char *last;
    GPtrArray *words;

    if (t->list_full || !t->res || !n || !gtk_tree_model_iter_nth_child(model, &it, NULL, n - 1))
        return;
    gtk_tree_model_get(model, &it, LIST_COL_KEY, &last, -1);
    words = ks_res_words(t->res, last, TRUE, WORDS_PAGE);
    t->s->busy = TRUE;
    list_append(t, words);
    t->s->busy = FALSE;
    g_ptr_array_free(words, TRUE);
    g_free(last);
}

static void set_entry(Tab *t, const char *text)
{
    t->s->busy = TRUE;
    gtk_entry_set_text(GTK_ENTRY(t->entry), text);
    t->s->busy = FALSE;
}

/* The entry for word in dictionary r, made the tab's page; FALSE if r
 * doesn't have it. push: Back can return to what was shown. */
static gboolean show_word(Tab *t, KsRes *r, const char *word, const char *anchor, gboolean push)
{
    char *key = NULL, *html;

    if (!ks_res_open(r, NULL) || !(html = ks_res_lookup(r, word, &key)))
        return FALSE;
    if (push)
        push_visit(t);
    if (r != t->res && !use_module(t, r, FALSE)) {
        g_free(html);
        g_free(key);
        return FALSE;
    }
    g_free(t->key);
    t->key = key;
    ks_html_view_begin(t->view, r->rtl);
    ks_html_view_heading(t->view, key, NULL);
    ks_html_view_html(t->view, html, r->css);
    ks_html_view_end(t->view, NULL);
    if (anchor && !ks_html_view_scroll_to_anchor(t->view, anchor)) {
        /* "#BM1643-" points at <a name="BM1643">. */
        char *a = g_strdup(anchor);

        g_strchomp(a);
        if (*a && a[strlen(a) - 1] == '-')
            a[strlen(a) - 1] = '\0';
        ks_html_view_scroll_to_anchor(t->view, a);
        g_free(a);
    }
    set_entry(t, key);
    words_fill(t, key);
    sync_buttons(t);
    g_free(html);
    return TRUE;
}

static int name_has_strong(const KsRes *r)
{
    char *l = g_ascii_strdown(ks_res_label(r), -1), *b = g_path_get_basename(r->path);
    char *lb = g_ascii_strdown(b, -1);
    int yes = strstr(l, "strong") || strstr(lb, "strong");

    g_free(l);
    g_free(b);
    g_free(lb);
    return yes;
}

/* Looks key up in the module named, the dictionary shown, then the ones
 * likeliest to have it, then all; the first that has it is shown. */
static gboolean dict_find(KsStudy *s, const char *module, const char *key, const char *anchor, Prefer prefer)
{
    Tab *t = &s->tabs[KS_RES_DICT];
    GPtrArray *order = g_ptr_array_new();
    gboolean found = FALSE;
    KsRes *r;

    tab_ready(t);
    if (module && (r = find_res(s, KS_RES_DICT, module)))
        g_ptr_array_add(order, r);
    if (prefer == PREFER_STRONG && (r = find_path(s, KS_RES_DICT, s->cfg->strong_dict)))
        g_ptr_array_add(order, r);
    if (t->res)
        g_ptr_array_add(order, t->res);
    /* Then by how likely: 2 for the kind wanted, 1 for a "Strong" name. */
    for (int score = 2; score >= 0; score--) {
        for (guint i = 0; i < s->mods->len; i++) {
            KsRes *m = g_ptr_array_index(s->mods, i);
            int sc = 0;

            if (m->kind != KS_RES_DICT || !m->supported)
                continue;
            if (prefer == PREFER_STRONG)
                sc = m->strong ? 1 + name_has_strong(m) : 0;
            else if (prefer == PREFER_MORPH)
                sc = m->morph ? 2 : 0;
            if (sc == score && !g_ptr_array_find(order, m, NULL))
                g_ptr_array_add(order, m);
        }
    }
    for (guint i = 0; i < order->len && !found; i++)
        found = show_word(t, g_ptr_array_index(order, i), key, anchor, TRUE);
    g_ptr_array_free(order, TRUE);
    if (found)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), KS_RES_DICT);
    return found;
}

gboolean ks_study_strong(KsStudy *s, const char *number)
{
    char *n = ks_strong_normalize(number);
    gboolean ok = n && dict_find(s, NULL, n, NULL, PREFER_STRONG);

    g_free(n);
    return ok;
}

/* The word in the entry: as typed, as a Strong's number, or the first in
 * the list. */
static void lookup_entry(Tab *t)
{
    const char *text = gtk_entry_get_text(GTK_ENTRY(t->entry));
    char *strong = ks_strong_normalize(text);
    GtkTreeIter it;

    if (!t->res || !*text)
        goto out;
    if (show_word(t, t->res, strong ? strong : text, NULL, TRUE))
        goto out;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(t->list), &it)) {
        char *w;

        gtk_tree_model_get(GTK_TREE_MODEL(t->list), &it, LIST_COL_KEY, &w, -1);
        show_word(t, t->res, w, NULL, TRUE);
        g_free(w);
    } else {
        status(t->s, _("%s is not in %s."), text, ks_res_label(t->res));
    }
out:
    g_free(strong);
}

void ks_study_lookup(KsStudy *s, const char *word)
{
    Tab *t = &s->tabs[KS_RES_DICT];

    gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), KS_RES_DICT);
    tab_ready(t);
    set_entry(t, word);
    words_fill(t, word);
    lookup_entry(t);
}

static gboolean filter_cb(gpointer data)
{
    Tab *t = data;

    t->filter_id = 0;
    words_fill(t, gtk_entry_get_text(GTK_ENTRY(t->entry)));
    return G_SOURCE_REMOVE;
}

/* Typing moves the list; Enter shows the entry. */
static void on_entry_changed(GtkEditable *e, Tab *t)
{
    (void)e;
    if (t->s->busy)
        return;
    if (t->filter_id)
        g_source_remove(t->filter_id);
    t->filter_id = g_timeout_add(200, filter_cb, t);
}

static void on_entry_activate(GtkEntry *e, Tab *t)
{
    (void)e;
    if (t->filter_id) {
        g_source_remove(t->filter_id);
        filter_cb(t);
    }
    lookup_entry(t);
}

static void on_list_scrolled(GtkAdjustment *adj, Tab *t)
{
    if (t->kind == KS_RES_DICT &&
        gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) >= gtk_adjustment_get_upper(adj) - 40)
        words_more(t);
}

/* ---- books and journals ------------------------------------------------ */

static void entries_fill(Tab *t)
{
    GPtrArray *entries = t->res ? ks_res_entries(t->res) : g_ptr_array_new();
    GtkTreeIter it;

    t->s->busy = TRUE;
    gtk_list_store_clear(t->list);
    for (guint i = 0; i < entries->len; i++) {
        KsEntry *e = g_ptr_array_index(entries, i);

        gtk_list_store_insert_with_values(t->list, &it, -1, LIST_COL_TEXT, e->title, LIST_COL_KEY, e->id, -1);
    }
    t->s->busy = FALSE;
    g_ptr_array_free(entries, TRUE);
}

/* The row of the list with that id: selected, scrolled to, its title. */
static char *select_entry(Tab *t, const char *id, int *index, int *count)
{
    GtkTreeModel *model = GTK_TREE_MODEL(t->list);
    GtkTreeIter it;
    char *title = NULL;
    int i = 0;

    *index = -1;
    for (gboolean ok = gtk_tree_model_get_iter_first(model, &it); ok; ok = gtk_tree_model_iter_next(model, &it), i++) {
        char *k;

        gtk_tree_model_get(model, &it, LIST_COL_KEY, &k, -1);
        if (*index < 0 && strcmp(k, id) == 0) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &it);

            gtk_tree_model_get(model, &it, LIST_COL_TEXT, &title, -1);
            t->s->busy = TRUE;
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(t->tree), path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(t->tree), path, NULL, FALSE, 0.0, 0.0);
            t->s->busy = FALSE;
            gtk_tree_path_free(path);
            *index = i;
        }
        g_free(k);
    }
    *count = i;
    return title;
}

static gboolean show_entry(Tab *t, KsRes *r, const char *key, gboolean push)
{
    char *id = NULL, *html, *title;
    int index, count;

    if (!ks_res_open(r, NULL) || !(html = ks_res_entry(r, key, &id)))
        return FALSE;
    if (push)
        push_visit(t);
    if (r != t->res) {
        if (!use_module(t, r, FALSE)) {
            g_free(html);
            g_free(id);
            return FALSE;
        }
        entries_fill(t);
    }
    g_free(t->key);
    t->key = id;
    title = select_entry(t, id, &index, &count);
    ks_html_view_begin(t->view, r->rtl);
    ks_html_view_heading(t->view, title ? title : id, NULL);
    ks_html_view_html(t->view, html, r->css);
    ks_html_view_end(t->view, NULL);
    gtk_widget_set_sensitive(t->prev_btn, index > 0);
    gtk_widget_set_sensitive(t->next_btn, index >= 0 && index < count - 1);
    sync_buttons(t);
    g_free(title);
    g_free(html);
    return TRUE;
}

static void show_first_entry(Tab *t)
{
    GtkTreeIter it;
    char *id;

    if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(t->list), &it)) {
        ks_html_view_message(t->view, _("This module has no entries."));
        return;
    }
    gtk_tree_model_get(GTK_TREE_MODEL(t->list), &it, LIST_COL_KEY, &id, -1);
    show_entry(t, t->res, id, FALSE);
    g_free(id);
}

static void step_entry(Tab *t, int dir)
{
    GtkTreePath *path = NULL;
    GtkTreeIter it;

    gtk_tree_view_get_cursor(GTK_TREE_VIEW(t->tree), &path, NULL);
    if (!path)
        return;
    if (dir > 0)
        gtk_tree_path_next(path);
    else
        gtk_tree_path_prev(path);
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(t->list), &it, path)) {
        char *id;

        gtk_tree_model_get(GTK_TREE_MODEL(t->list), &it, LIST_COL_KEY, &id, -1);
        show_entry(t, t->res, id, FALSE);
        g_free(id);
    }
    gtk_tree_path_free(path);
}

static void on_prev(Tab *t) { step_entry(t, -1); }
static void on_next(Tab *t) { step_entry(t, +1); }

/* An entry of a book or journal by title: in r (NULL: the books tab's
 * module, then every book and journal). */
static gboolean entry_find(KsStudy *s, KsRes *r, const char *title)
{
    Tab *t;

    if (r) {
        t = &s->tabs[r->kind == KS_RES_JOURNAL ? KS_RES_JOURNAL : KS_RES_BOOK];
        tab_ready(t);
        if (!show_entry(t, r, title, TRUE))
            return FALSE;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), t->kind);
        return TRUE;
    }
    tab_ready(&s->tabs[KS_RES_BOOK]);
    if (s->tabs[KS_RES_BOOK].res && entry_find(s, s->tabs[KS_RES_BOOK].res, title))
        return TRUE;
    for (guint i = 0; i < s->mods->len; i++) {
        KsRes *m = g_ptr_array_index(s->mods, i);

        if ((m->kind == KS_RES_BOOK || m->kind == KS_RES_JOURNAL) && m->supported && entry_find(s, m, title))
            return TRUE;
    }
    return FALSE;
}

/* ---- commentary ---------------------------------------------------------- */

typedef struct {
    Tab *t;
    int n;
} CommentCtx;

static void comment_cb(int from, int to, const char *html, void *data)
{
    CommentCtx *c = data;
    Tab *t = c->t;
    char mark[16], *ref;
    Range r = { from, to };

    /* Verse 0: about the whole chapter. */
    ref = ks_ref_format(t->book, t->chapter, from, to > from ? to : 0);
    g_snprintf(mark, sizeof mark, "e%d", c->n++);
    ks_html_view_heading(t->view, ref, mark);
    ks_html_view_html(t->view, html, t->res->css);
    g_array_append_val(t->ranges, r);
    g_free(ref);
}

/* The comment on the verse wanted, or the last one before it. */
static void commentary_scroll(Tab *t)
{
    int pick = -1;
    char mark[16];

    for (guint i = 0; i < t->ranges->len && t->verse > 0; i++) {
        const Range *r = &g_array_index(t->ranges, Range, i);

        if (r->from <= t->verse && t->verse <= r->to) {
            pick = (int)i;
            break;
        }
        if (r->from <= t->verse)
            pick = (int)i;
    }
    g_snprintf(mark, sizeof mark, "e%d", MAX(pick, 0));
    ks_html_view_scroll_to(t->view, mark);
}

static void commentary_refresh(Tab *t)
{
    CommentCtx c = { t, 0 };

    if (!t->ready || !t->res || !t->book)
        return;
    if (t->shown_res == t->res && t->shown_book == t->book && t->shown_chapter == t->chapter) {
        commentary_scroll(t);
        return;
    }
    g_array_set_size(t->ranges, 0);
    ks_html_view_begin(t->view, t->res->rtl);
    if (!ks_res_comments(t->res, t->book, t->chapter, comment_cb, &c)) {
        char *ref = ks_ref_format(t->book, t->chapter, 0, 0);
        char *msg = g_strdup_printf(_("%s has no comment on %s."), ks_res_label(t->res), ref);

        ks_html_view_note(t->view, msg);
        g_free(msg);
        g_free(ref);
    }
    ks_html_view_end(t->view, NULL);
    t->shown_res = t->res;
    t->shown_book = t->book;
    t->shown_chapter = t->chapter;
    commentary_scroll(t);
}

static gboolean tab_visible(Tab *t)
{
    GtkNotebook *nb = GTK_NOTEBOOK(t->s->notebook);

    return gtk_widget_get_mapped(t->s->notebook) && gtk_notebook_get_current_page(nb) == (int)t->kind;
}

void ks_study_set_verse(KsStudy *s, int book, int chapter, int verse)
{
    Tab *t = &s->tabs[KS_RES_COMMENTARY];

    t->book = book;
    t->chapter = chapter;
    t->verse = verse;
    if (tab_visible(t))
        commentary_refresh(t);
}

/* ---- links ------------------------------------------------------------- */

static void open_picture(KsStudy *s, Tab *t, const KsLink *l)
{
    KsRes *r = l->module ? find_res(s, -1, l->module) : NULL;
    GBytes *bytes;

    if (!r)
        r = t->res;
    if (!r || !ks_res_open(r, NULL) || !(bytes = ks_res_image(r, l->key))) {
        status(s, _("Picture not found: %s"), l->key);
        return;
    }
    ks_image_window(s->notebook, bytes, l->key);
    g_bytes_unref(bytes);
}

static void on_link(KsHtmlView *hv, const char *href, int x, int y, void *data)
{
    Tab *t = data;
    KsStudy *s = t->s;
    KsLink l;
    KsRes *r;
    GError *err = NULL;

    if (!ks_link_parse(href, &l)) {
        status(s, _("Link not supported: %s"), href);
        ks_link_clear(&l);
        return;
    }
    switch (l.kind) {
    case KS_LINK_BIBLE:
        if (s->cb.go)
            s->cb.go(&l.ref, s->data);
        break;
    case KS_LINK_STRONG:
        if (!dict_find(s, l.module, l.key, NULL, PREFER_STRONG))
            status(s, _("No dictionary has %s."), l.key);
        break;
    case KS_LINK_DICT:
        if (!dict_find(s, l.module, l.key, l.anchor, PREFER_NONE))
            status(s, _("No dictionary has %s."), l.key);
        break;
    case KS_LINK_MORPH:
        if (!dict_find(s, l.module, l.key, NULL, PREFER_MORPH))
            status(s, _("No dictionary has %s."), l.key);
        break;
    case KS_LINK_COMMENTARY:
        if (l.module && (r = find_res(s, KS_RES_COMMENTARY, l.module))) {
            tab_ready(&s->tabs[KS_RES_COMMENTARY]);
            use_module(&s->tabs[KS_RES_COMMENTARY], r, FALSE);
        }
        gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), KS_RES_COMMENTARY);
        if (s->cb.go)
            s->cb.go(&l.ref, s->data);
        break;
    case KS_LINK_ENTRY:
    case KS_LINK_BOOK:
        r = l.module ? find_res(s, KS_RES_BOOK, l.module) : NULL;
        if (!r && l.kind == KS_LINK_ENTRY && (t->kind == KS_RES_BOOK || t->kind == KS_RES_JOURNAL))
            r = t->res;
        if (!(r ? entry_find(s, r, l.key) : entry_find(s, NULL, l.key)))
            status(s, _("Entry not found: %s"), l.key);
        break;
    case KS_LINK_PICTURE:
        open_picture(s, t, &l);
        break;
    case KS_LINK_POPUP:
        ks_html_view_popup(hv, l.key, x, y);
        break;
    case KS_LINK_URL:
        if (!gtk_show_uri(gtk_widget_get_screen(s->notebook), l.key, GDK_CURRENT_TIME, &err)) {
            status(s, "%s", err->message);
            g_error_free(err);
        }
        break;
    case KS_LINK_NONE:
        break;
    }
    ks_link_clear(&l);
}

static GBytes *on_image(KsHtmlView *hv, const char *src, void *data)
{
    Tab *t = data;

    (void)hv;
    return t->res ? ks_res_image(t->res, src) : NULL;
}

/* ---- tab widgets -------------------------------------------------------- */

static void on_combo_changed(GtkComboBox *c, Tab *t)
{
    GtkTreeIter it;
    KsRes *r;
    char *key;

    if (t->s->busy || !gtk_combo_box_get_active_iter(c, &it))
        return;
    gtk_tree_model_get(GTK_TREE_MODEL(t->mods), &it, MOD_COL_RES, &r, -1);
    if (r == t->res)
        return;
    key = g_strdup(t->key);
    if (!use_module(t, r, TRUE)) {
        fill_mods(t); /* back to the one shown */
        g_free(key);
        return;
    }
    switch (t->kind) {
    case KS_RES_COMMENTARY:
        commentary_refresh(t);
        break;
    case KS_RES_DICT:
        /* The same word in the other dictionary, as in MySword. */
        if (!key || !show_word(t, r, key, NULL, FALSE)) {
            words_fill(t, gtk_entry_get_text(GTK_ENTRY(t->entry)));
            ks_html_view_message(t->view, key ? _("This dictionary does not have this word.")
                                              : _("Type a word, or pick one from the list."));
        }
        break;
    default:
        entries_fill(t);
        if (!key || !show_entry(t, r, key, FALSE))
            show_first_entry(t);
        break;
    }
    g_free(key);
}

static void on_cursor_changed(GtkTreeView *tv, Tab *t)
{
    GtkTreePath *path = NULL;
    GtkTreeIter it;
    char *key;

    if (t->s->busy || !t->res)
        return;
    gtk_tree_view_get_cursor(tv, &path, NULL);
    if (!path)
        return;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(t->list), &it, path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(t->list), &it, LIST_COL_KEY, &key, -1);
        if (g_strcmp0(key, t->key) != 0) {
            if (t->kind == KS_RES_DICT) {
                /* Not words_fill() here: the list stays where it is. */
                char *html = ks_res_lookup(t->res, key, NULL);

                if (html) {
                    push_visit(t);
                    g_free(t->key);
                    t->key = g_strdup(key);
                    ks_html_view_begin(t->view, t->res->rtl);
                    ks_html_view_heading(t->view, key, NULL);
                    ks_html_view_html(t->view, html, t->res->css);
                    ks_html_view_end(t->view, NULL);
                    set_entry(t, key);
                    sync_buttons(t);
                    g_free(html);
                }
            } else {
                show_entry(t, t->res, key, TRUE);
            }
        }
        g_free(key);
    }
    gtk_tree_path_free(path);
}

static void on_back(Tab *t)
{
    Visit v;

    if (!t->back->len)
        return;
    v = g_array_index(t->back, Visit, t->back->len - 1);
    g_array_set_size(t->back, t->back->len - 1);
    if (t->kind == KS_RES_DICT)
        show_word(t, v.res, v.key, NULL, FALSE);
    else
        show_entry(t, v.res, v.key, FALSE);
    g_free(v.key);
    sync_buttons(t);
}

static void on_info(Tab *t)
{
    GHashTable *d;
    char *contents = NULL;

    if (!t->res)
        return;
    d = ks_res_details(t->res);
    if (t->res->kind == KS_RES_DICT && (t->res->strong || t->res->morph))
        contents = g_strdup(t->res->strong ? _("Strong's numbers") : _("morphology codes"));
    ks_info_dialog(GTK_WINDOW(gtk_widget_get_toplevel(t->s->notebook)), t->res->title, d, contents, t->res->path);
    g_free(contents);
    if (d)
        g_hash_table_destroy(d);
}

static GtkWidget *small_button(const char *stock, const char *tip, void (*cmd)(Tab *), Tab *t)
{
    GtkWidget *b = gtk_button_new();

    gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_stock(stock, GTK_ICON_SIZE_MENU));
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_widget_set_can_focus(b, FALSE);
    g_signal_connect_swapped(b, "clicked", G_CALLBACK(cmd), t);
    return b;
}

static GtkWidget *make_list(Tab *t)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    t->list = gtk_list_store_new(LIST_N_COLS, G_TYPE_STRING, G_TYPE_STRING);
    t->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->list));
    g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    col = gtk_tree_view_column_new_with_attributes(NULL, cell, "text", LIST_COL_TEXT, NULL);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(t->tree), col);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(t->tree), FALSE);
    gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(t->tree), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(t->tree), LIST_COL_TEXT);
    g_signal_connect(t->tree, "cursor-changed", G_CALLBACK(on_cursor_changed), t);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), t->tree);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw)), "value-changed",
                     G_CALLBACK(on_list_scrolled), t);
    return sw;
}

static void build_tab(KsStudy *s, KsResKind kind)
{
    static const char *const tips[KS_RES_N] = { N_("Commentary"), N_("Dictionary"), N_("Book"), N_("Journal") };
    Tab *t = &s->tabs[kind];
    GtkWidget *bar = gtk_hbox_new(FALSE, 2), *box = gtk_vbox_new(FALSE, 2);
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();

    t->s = s;
    t->kind = kind;
    t->back = g_array_new(FALSE, FALSE, sizeof(Visit));
    t->ranges = g_array_new(FALSE, FALSE, sizeof(Range));
    t->view = ks_html_view_new();
    ks_html_view_set_callbacks(t->view, on_link, on_image, t);

    t->mods = gtk_list_store_new(MOD_N_COLS, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN);
    t->combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(t->mods));
    g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 16, "single-paragraph-mode", TRUE, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(t->combo), cell, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(t->combo), cell, "markup", MOD_COL_LABEL, "sensitive",
                                   MOD_COL_SENSITIVE, NULL);
    gtk_widget_set_tooltip_text(t->combo, _(tips[kind]));
    g_signal_connect(t->combo, "changed", G_CALLBACK(on_combo_changed), t);
    gtk_box_pack_start(GTK_BOX(bar), t->combo, TRUE, TRUE, 0);

    if (kind == KS_RES_BOOK || kind == KS_RES_JOURNAL) {
        t->prev_btn = small_button(GTK_STOCK_GO_UP, _("Previous entry"), on_prev, t);
        t->next_btn = small_button(GTK_STOCK_GO_DOWN, _("Next entry"), on_next, t);
        gtk_box_pack_start(GTK_BOX(bar), t->prev_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), t->next_btn, FALSE, FALSE, 0);
    }
    if (kind != KS_RES_COMMENTARY) {
        t->back_btn = small_button(GTK_STOCK_GO_BACK, _("Back"), on_back, t);
        gtk_box_pack_start(GTK_BOX(bar), t->back_btn, FALSE, FALSE, 0);
    }
    t->info_btn = small_button(GTK_STOCK_INFO, _("About the module"), on_info, t);
    gtk_box_pack_start(GTK_BOX(bar), t->info_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

    if (kind == KS_RES_DICT) {
        t->entry = gtk_entry_new();
        gtk_widget_set_tooltip_text(t->entry, _("A word or a Strong's number (H430, G26); Enter shows it"));
        g_signal_connect(t->entry, "changed", G_CALLBACK(on_entry_changed), t);
        g_signal_connect(t->entry, "activate", G_CALLBACK(on_entry_activate), t);
        gtk_box_pack_start(GTK_BOX(box), t->entry, FALSE, FALSE, 0);
    }
    if (kind == KS_RES_COMMENTARY) {
        gtk_box_pack_start(GTK_BOX(box), ks_html_view_widget(t->view), TRUE, TRUE, 0);
    } else {
        GtkWidget *paned = gtk_vpaned_new();

        gtk_paned_pack1(GTK_PANED(paned), make_list(t), FALSE, TRUE);
        gtk_paned_pack2(GTK_PANED(paned), ks_html_view_widget(t->view), TRUE, TRUE);
        gtk_paned_set_position(GTK_PANED(paned), 130);
        gtk_box_pack_start(GTK_BOX(box), paned, TRUE, TRUE, 0);
    }
    gtk_container_set_border_width(GTK_CONTAINER(box), 2);
    t->page = box;
    sync_buttons(t);
}

static gint64 file_size(const char *path)
{
    GStatBuf st;

    return g_stat(path, &st) == 0 ? (gint64)st.st_size : 0;
}

/* With nothing chosen yet: the biggest commentary or journal (the one
 * with most in it, not a single book's comments or an empty journal), the
 * biggest Strong's dictionary, the first book. */
static KsRes *default_module(KsStudy *s, KsResKind kind)
{
    KsRes *best = NULL;
    gint64 best_score = -1;

    for (guint i = 0; i < s->mods->len; i++) {
        KsRes *m = g_ptr_array_index(s->mods, i);
        gint64 score = 0;

        if (m->kind != kind || !m->supported)
            continue;
        if (kind == KS_RES_COMMENTARY || kind == KS_RES_JOURNAL)
            score = file_size(m->path);
        else if (kind == KS_RES_DICT)
            score = (m->strong ? 2 + name_has_strong(m) : 0) * G_GINT64_CONSTANT(1000000000000) + file_size(m->path);
        if (score > best_score) {
            best = m;
            best_score = score;
        }
    }
    return best;
}

/* What to show in a tab the first time: the module and place saved, or
 * a sensible default. */
static void tab_ready(Tab *t)
{
    static const char *const none[KS_RES_N] = {
        N_("No commentary found.\n\nCopy *.cmt.mybible files to %s, or use File → Open module…"),
        N_("No dictionary found.\n\nCopy *.dct.mybible files to %s, or use File → Open module…"),
        N_("No book found.\n\nCopy *.bok.mybible files to %s, or use File → Open module…"),
        N_("No journal found.\n\nCopy *.jor.mybible files to %s, or use File → Open module…"),
    };
    KsStudy *s = t->s;
    KsRes *r;
    const char *key;

    if (t->ready)
        return;
    t->ready = TRUE;
    r = find_path(s, t->kind, s->cfg->res_module[t->kind]);
    if (!r || !r->supported)
        r = default_module(s, t->kind);
    fill_mods(t);
    if (!r || !use_module(t, r, FALSE)) {
        char *dir = g_build_filename(g_get_user_data_dir(), "kisword", "modules", NULL);
        char *msg = g_strdup_printf(_(none[t->kind]), dir);

        ks_html_view_message(t->view, msg);
        g_free(msg);
        g_free(dir);
        return;
    }
    key = s->cfg->res_key[t->kind];
    switch (t->kind) {
    case KS_RES_COMMENTARY:
        commentary_refresh(t);
        break;
    case KS_RES_DICT:
        if (!key || !*key || !show_word(t, r, key, NULL, FALSE)) {
            words_fill(t, "");
            ks_html_view_message(t->view, _("Type a word, or pick one from the list."));
        }
        break;
    default:
        entries_fill(t);
        if (!key || !*key || !show_entry(t, r, key, FALSE))
            show_first_entry(t);
        break;
    }
}

static void on_switch_page(GtkNotebook *nb, gpointer page, guint num, KsStudy *s)
{
    (void)nb;
    (void)page;
    if (num >= KS_RES_N || !gtk_widget_get_mapped(s->notebook))
        return;
    s->cfg->study_tab = (int)num;
    tab_ready(&s->tabs[num]);
    if (num == KS_RES_COMMENTARY)
        commentary_refresh(&s->tabs[num]);
}

static void on_map(GtkWidget *w, KsStudy *s)
{
    int num = gtk_notebook_get_current_page(GTK_NOTEBOOK(w));

    if (num < 0 || num >= KS_RES_N)
        return;
    tab_ready(&s->tabs[num]);
    if (num == KS_RES_COMMENTARY)
        commentary_refresh(&s->tabs[num]);
}

static void on_destroy(GtkWidget *w, KsStudy *s)
{
    (void)w;
    for (int k = 0; k < KS_RES_N; k++) {
        Tab *t = &s->tabs[k];

        if (t->filter_id)
            g_source_remove(t->filter_id);
        for (guint i = 0; i < t->back->len; i++)
            g_free(g_array_index(t->back, Visit, i).key);
        g_array_free(t->back, TRUE);
        g_array_free(t->ranges, TRUE);
        g_object_unref(t->mods);
        if (t->list)
            g_object_unref(t->list);
        g_free(t->key);
    }
    g_free(s);
}

KsStudy *ks_study_new(KsConfig *cfg, GPtrArray *mods, const KsStudyCallbacks *cb, void *data)
{
    static const char *const labels[KS_RES_N] = { N_("Commentary"), N_("Dictionary"), N_("Books"), N_("Journals") };
    KsStudy *s = g_new0(KsStudy, 1);

    s->cfg = cfg;
    s->mods = mods;
    s->cb = *cb;
    s->data = data;
    s->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(s->notebook), TRUE);
    for (int k = 0; k < KS_RES_N; k++) {
        build_tab(s, (KsResKind)k);
        gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), s->tabs[k].page, gtk_label_new(_(labels[k])));
    }
    gtk_widget_show_all(s->notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), cfg->study_tab);
    g_signal_connect(s->notebook, "switch-page", G_CALLBACK(on_switch_page), s);
    g_signal_connect_after(s->notebook, "map", G_CALLBACK(on_map), s);
    g_signal_connect(s->notebook, "destroy", G_CALLBACK(on_destroy), s);
    return s;
}

GtkWidget *ks_study_widget(KsStudy *s)
{
    return s->notebook;
}

void ks_study_set_font(KsStudy *s, const PangoFontDescription *font)
{
    for (int k = 0; k < KS_RES_N; k++)
        ks_html_view_set_font(s->tabs[k].view, font);
}

void ks_study_show_tab(KsStudy *s, KsResKind kind)
{
    gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), kind);
    tab_ready(&s->tabs[kind]);
    if (kind == KS_RES_COMMENTARY)
        commentary_refresh(&s->tabs[kind]);
    if (kind == KS_RES_DICT)
        gtk_widget_grab_focus(s->tabs[kind].entry);
}

KsResKind ks_study_current_tab(KsStudy *s)
{
    int n = gtk_notebook_get_current_page(GTK_NOTEBOOK(s->notebook));

    return n >= 0 && n < KS_RES_N ? (KsResKind)n : KS_RES_DICT;
}

void ks_study_added(KsStudy *s, KsRes *m)
{
    Tab *t = &s->tabs[m->kind];

    if (!t->ready) {
        g_free(s->cfg->res_module[m->kind]);
        s->cfg->res_module[m->kind] = g_strdup(m->real);
        ks_study_show_tab(s, m->kind);
        return;
    }
    fill_mods(t);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), m->kind);
    if (m->supported) {
        GtkTreeIter it;
        gboolean ok;

        /* Through the combo, as if picked there. */
        for (ok = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(t->mods), &it); ok;
             ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(t->mods), &it)) {
            KsRes *r;

            gtk_tree_model_get(GTK_TREE_MODEL(t->mods), &it, MOD_COL_RES, &r, -1);
            if (r == m)
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(t->combo), &it);
        }
    } else {
        status(s, "%s: %s", ks_res_label(m), m->unsupported_why);
    }
}

void ks_study_save(KsStudy *s)
{
    for (int k = 0; k < KS_RES_N; k++) {
        Tab *t = &s->tabs[k];

        if (!t->ready)
            continue;
        g_free(s->cfg->res_key[k]);
        s->cfg->res_key[k] = g_strdup(t->key);
    }
}
