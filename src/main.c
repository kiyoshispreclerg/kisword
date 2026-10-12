/* main.c - kisword: a light Bible reader for MySword modules.
 *
 * Besides the window, command-line modes that need no display:
 *   kisword --dump "Jo 3:16" FILE   prints verses as plain text (or a
 *                                   dictionary word, a chapter's comments,
 *                                   a book entry)
 *   kisword --check FILE...         parses every verse or entry, reports
 *                                   problems
 *   kisword --search WORDS FILE     prints the verses found
 * They are what `make test` uses against real modules. */
#include "books.h"
#include "config.h"
#include "gbf.h"
#include "html.h"
#include "ks_i18n.h"
#include "link.h"
#include "module.h"
#include "ref.h"
#include "res.h"
#include "search.h"
#include "window.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static KsRes *open_res(const char *path)
{
    GError *err = NULL;
    KsRes *r = ks_res_probe(path, &err);

    if (!r || !ks_res_open(r, &err)) {
        fprintf(stderr, "kisword: %s\n", err->message);
        g_error_free(err);
        ks_res_free(r);
        return NULL;
    }
    return r;
}

static void print_html(const char *html)
{
    char *plain = ks_html_plain(html);

    printf("%s\n", plain);
    g_free(plain);
}

typedef struct {
    int book, chapter, verse;
    int n;
} DumpComments;

static void dump_comment(int from, int to, const char *html, void *data)
{
    DumpComments *d = data;
    char *ref;

    if (d->verse && (d->verse < from || d->verse > to))
        return;
    ref = ks_ref_format(d->book, d->chapter, from, to);
    printf("%s%s\n", d->n++ ? "\n" : "", ref);
    print_html(html);
    g_free(ref);
}

/* --dump for dictionaries (a word), commentaries (a reference) and books
 * or journals (an entry's id or title). */
static int dump_res(const char *path, const char *key)
{
    KsRes *r = open_res(path);
    char *html = NULL;
    int status = 0;

    if (!r)
        return 1;
    if (r->kind == KS_RES_COMMENTARY) {
        KsRef ref;
        DumpComments d = { 0 };

        if (!ks_ref_parse(key, &ref) || !ref.book) {
            fprintf(stderr, "kisword: %s: %s\n", _("Reference not recognized"), key);
            status = 2;
        } else {
            d.book = ref.book;
            d.chapter = ref.chapter;
            d.verse = ref.verse;
            ks_res_comments(r, ref.book, ref.chapter, dump_comment, &d);
            status = d.n ? 0 : 1;
        }
    } else {
        char *strong = r->kind == KS_RES_DICT ? ks_strong_normalize(key) : NULL;

        html = r->kind == KS_RES_DICT ? ks_res_lookup(r, strong ? strong : key, NULL) : ks_res_entry(r, key, NULL);
        g_free(strong);
        if (html)
            print_html(html);
        else
            status = 1;
    }
    if (status == 1)
        fprintf(stderr, "kisword: %s: %s\n", _("Not found"), key);
    g_free(html);
    ks_res_free(r);
    return status;
}

static int cli_dump(char **files, const char *text)
{
    KsModule *m;
    GError *err = NULL;
    KsRef r;
    int to;

    if (!files || !files[0] || files[1]) {
        fprintf(stderr, "kisword: %s\n", _("--dump needs exactly one module file"));
        return 2;
    }
    if (ks_res_kind_of(files[0], &(KsResKind){ 0 }))
        return dump_res(files[0], text);
    if (!ks_ref_parse(text, &r) || !r.book) {
        fprintf(stderr, "kisword: %s: %s\n", _("Reference not recognized"), text);
        return 2;
    }
    m = ks_module_probe(files[0], &err);
    if (!m || !ks_module_open(m, &err)) {
        fprintf(stderr, "kisword: %s\n", err->message);
        g_error_free(err);
        ks_module_free(m);
        return 1;
    }
    to = r.verse_end ? r.verse_end : r.verse;

    sqlite3_stmt *st;
    sqlite3_prepare_v2(m->db, "SELECT Verse, Scripture FROM Bible WHERE Book = ?1 AND Chapter = ?2 "
                              "AND Verse BETWEEN ?3 AND ?4 ORDER BY Verse", -1, &st, NULL);
    sqlite3_bind_int(st, 1, r.book);
    sqlite3_bind_int(st, 2, r.chapter);
    sqlite3_bind_int(st, 3, r.verse ? r.verse : 0);
    sqlite3_bind_int(st, 4, r.verse ? to : G_MAXINT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *t = (const char *)sqlite3_column_text(st, 1);
        char *ruled = m->rules ? ks_rules_apply(m->rules, t ? t : "") : NULL;
        char *plain = ks_gbf_plain(ruled ? ruled : t ? t : "", m->css);

        printf("%d %s\n", sqlite3_column_int(st, 0), plain);
        g_free(plain);
        g_free(ruled);
    }
    sqlite3_finalize(st);
    ks_module_free(m);
    return 0;
}

/* --search: hits as "Jo 3:16 text", then a count line on stderr. */
static int cli_search(char **files, const char *query, gboolean phrase, gboolean whole)
{
    KsSearchOptions opt = { phrase ? KS_SEARCH_PHRASE : KS_SEARCH_ALL_WORDS, whole, 0, 0 };
    KsModule *m;
    GError *err = NULL;
    GArray *hits;
    gint64 t0, t1;
    char testament;
    int number;

    if (!files || !files[0] || files[1]) {
        fprintf(stderr, "kisword: %s\n", _("--search needs exactly one module file"));
        return 2;
    }
    m = ks_module_probe(files[0], &err);
    if (!m || !ks_module_open(m, &err)) {
        fprintf(stderr, "kisword: %s\n", err->message);
        g_error_free(err);
        ks_module_free(m);
        return 1;
    }
    t0 = g_get_monotonic_time();
    if (ks_search_parse_strong(query, &testament, &number)) {
        hits = ks_search_strong(m, testament, number, &opt);
        t1 = t0;
    } else {
        KsSearchIndex *idx = ks_search_index_new(m);

        while (!ks_search_index_step(idx, 1000))
            ;
        t1 = g_get_monotonic_time();
        hits = ks_search_text(idx, query, &opt);
        ks_search_index_free(idx);
    }
    for (guint i = 0; i < hits->len; i++) {
        const KsHit *h = &g_array_index(hits, KsHit, i);
        char *ref = ks_ref_format(h->book, h->chapter, h->verse, 0);
        char *raw = ks_module_verse(m, h->book, h->chapter, h->verse);
        char *plain = ks_gbf_plain(raw, m->css);

        printf("%s %s\n", ref, plain);
        g_free(plain);
        g_free(raw);
        g_free(ref);
    }
    fprintf(stderr, "%u verses (index %.0f ms, search %.0f ms)\n", hits->len, (t1 - t0) / 1000.0,
            (g_get_monotonic_time() - t1) / 1000.0);
    g_array_free(hits, TRUE);
    ks_module_free(m);
    return 0;
}

typedef struct {
    const KsStyleSheet *css;
    int verses, invalid_utf8, stray_lt;
    GHashTable *unknown; /* tag name -> count */
    char where[64];
    char first_unknown_where[64];
} Check;

static void check_span(const KsSpan *sp, void *data)
{
    Check *c = data;

    if (sp->kind == KS_SPAN_UNKNOWN) {
        char *name = g_strndup(sp->text, sp->len);
        int n = GPOINTER_TO_INT(g_hash_table_lookup(c->unknown, name));

        if (!g_hash_table_size(c->unknown))
            g_strlcpy(c->first_unknown_where, c->where, sizeof c->first_unknown_where);
        g_hash_table_insert(c->unknown, name, GINT_TO_POINTER(n + 1));
    } else if (sp->kind == KS_SPAN_TEXT && memchr(sp->text, '<', sp->len)) {
        c->stray_lt++;
    }
}

static void check_verse(int book, int chapter, int verse, const char *text, gboolean valid, void *data)
{
    Check *c = data;

    c->verses++;
    if (!valid)
        c->invalid_utf8++;
    g_snprintf(c->where, sizeof c->where, "%d:%d:%d", book, chapter, verse);
    ks_gbf_parse(text, c->css, check_span, c);
}

/* ---- --check for dictionaries, commentaries, books and journals ---- */

typedef struct {
    KsRes *r;
    int entries, invalid_utf8, links, images, missing_images;
    GHashTable *bad; /* link not understood (first chars) -> count */
    GHashTable *bad_where;
    const char *where;
} ResCheck;

static void check_run(const KsHtmlRun *run, void *data)
{
    ResCheck *c = data;

    if (run->href && run->len) {
        KsLink l;

        c->links++;
        if (!ks_link_parse(run->href, &l)) {
            char *key = g_strndup(run->href, 24);
            int n = GPOINTER_TO_INT(g_hash_table_lookup(c->bad, key));

            if (!n)
                g_hash_table_insert(c->bad_where, g_strdup(key), g_strdup(c->where));
            g_hash_table_insert(c->bad, key, GINT_TO_POINTER(n + 1));
        }
        ks_link_clear(&l);
    }
    if (run->image) {
        GBytes *b = ks_res_image(c->r, run->image);

        c->images++;
        if (b)
            g_bytes_unref(b);
        else
            c->missing_images++;
    }
}

static void check_entry(const char *where, const char *html, gboolean valid, void *data)
{
    ResCheck *c = data;

    c->entries++;
    if (!valid)
        c->invalid_utf8++;
    c->where = where;
    ks_html_render(html, c->r->css, check_run, c);
}

static int check_res(const char *path)
{
    static const char *const kinds[KS_RES_N] = { "commentary", "dictionary", "book", "journal" };
    GError *err = NULL;
    KsRes *r = ks_res_probe(path, &err);
    ResCheck c = { 0 };
    gint64 t0;
    int status = 0;

    if (!r) {
        printf("%s: ERROR %s\n", path, err->message);
        g_error_free(err);
        return 1;
    }
    if (!r->supported) {
        printf("%s: UNSUPPORTED %s — %s\n", path, ks_res_label(r), r->unsupported_why);
        ks_res_free(r);
        return 0;
    }
    if (!ks_res_open(r, &err)) {
        printf("%s: ERROR %s\n", path, err->message);
        g_error_free(err);
        ks_res_free(r);
        return 1;
    }
    c.r = r;
    c.bad = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    c.bad_where = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    t0 = g_get_monotonic_time();
    ks_res_foreach(r, check_entry, &c);
    printf("%s: %s %s, %d entries, %d links, parsed in %.0f ms", path, kinds[r->kind], ks_res_label(r), c.entries,
           c.links, (g_get_monotonic_time() - t0) / 1000.0);
    if (c.invalid_utf8) {
        status = 1;
        printf("\n  invalid UTF-8 entries: %d", c.invalid_utf8);
    }
    if (c.missing_images)
        printf("\n  note: images not in the module: %d of %d", c.missing_images, c.images);
    if (g_hash_table_size(c.bad)) {
        GHashTableIter it;
        gpointer k, v;
        int shown = 0;

        printf("\n  note: links not understood:");
        g_hash_table_iter_init(&it, c.bad);
        while (g_hash_table_iter_next(&it, &k, &v) && shown++ < 12)
            printf("\n    \"%s\": %d (first in %s)", (char *)k, GPOINTER_TO_INT(v),
                   (char *)g_hash_table_lookup(c.bad_where, k));
        if (g_hash_table_size(c.bad) > 12)
            printf("\n    (%u more)", g_hash_table_size(c.bad) - 12);
    }
    if (!status)
        printf(" — OK");
    printf("\n");
    g_hash_table_destroy(c.bad);
    g_hash_table_destroy(c.bad_where);
    ks_res_free(r);
    return status;
}

static int cli_check(char **files)
{
    int status = 0;

    for (int i = 0; files && files[i]; i++) {
        GError *err = NULL;
        KsModule *m;

        if (ks_res_kind_of(files[i], &(KsResKind){ 0 })) {
            status |= check_res(files[i]);
            continue;
        }
        m = ks_module_probe(files[i], &err);
        Check c = { 0 };
        gint64 t0;
        int books = 0;

        if (!m) {
            printf("%s: ERROR %s\n", files[i], err->message);
            g_error_free(err);
            status = 1;
            continue;
        }
        if (!m->supported) {
            printf("%s: UNSUPPORTED %s — %s\n", files[i], ks_module_label(m), m->unsupported_why);
            ks_module_free(m);
            continue;
        }
        if (!ks_module_open(m, &err)) {
            printf("%s: ERROR %s\n", files[i], err->message);
            g_error_free(err);
            ks_module_free(m);
            status = 1;
            continue;
        }
        c.unknown = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        c.css = m->css;
        t0 = g_get_monotonic_time();
        ks_module_foreach(m, check_verse, &c);
        for (int b = 1; b <= KS_MAX_BOOK; b++)
            books += m->nchapters[b] != 0;

        printf("%s: %s, %d books, %d verses, parsed in %.0f ms", files[i], ks_module_label(m), books, c.verses,
               (g_get_monotonic_time() - t0) / 1000.0);
        if (!g_hash_table_size(c.unknown) && !c.invalid_utf8) {
            printf(" — OK");
        } else {
            GHashTableIter it;
            gpointer k, v;

            status = 1;
            printf("\n  invalid UTF-8 verses: %d", c.invalid_utf8);
            g_hash_table_iter_init(&it, c.unknown);
            while (g_hash_table_iter_next(&it, &k, &v))
                printf("\n  unknown tag <%s>: %d", (char *)k, GPOINTER_TO_INT(v));
            if (g_hash_table_size(c.unknown))
                printf("\n  first unknown tag at %s", c.first_unknown_where);
        }
        if (c.stray_lt)
            printf("\n  note: %d text spans keep a literal '<'", c.stray_lt);
        if (m->rules || m->rules_invalid)
            printf("\n  note: %d verse rules applied, %d skipped (invalid)", ks_rules_count(m->rules),
                   m->rules_invalid);
        printf("\n");
        g_hash_table_destroy(c.unknown);
        ks_module_free(m);
    }
    return status;
}

int main(int argc, char **argv)
{
    char *ref = NULL, *dump = NULL, *search = NULL;
    char **files = NULL;
    gboolean check = FALSE, version = FALSE, phrase = FALSE, whole = FALSE;
    GOptionEntry entries[] = {
        { "ref", 'r', 0, G_OPTION_ARG_STRING, &ref, N_("Open at this reference (e.g. \"Jo 3:16\")"), N_("REF") },
        { "dump", 0, 0, G_OPTION_ARG_STRING, &dump, N_("Print REF from the given module as plain text and exit (a word or entry for dictionaries and books)"),
          N_("REF") },
        { "check", 0, 0, G_OPTION_ARG_NONE, &check, N_("Parse every verse or entry of the given modules and report problems"),
          NULL },
        { "search", 0, 0, G_OPTION_ARG_STRING, &search,
          N_("Print the verses of the given module with all these words (or a Strong's number like H430) and exit"),
          N_("WORDS") },
        { "phrase", 0, 0, G_OPTION_ARG_NONE, &phrase, N_("With --search: the words together, in order"), NULL },
        { "whole-words", 0, 0, G_OPTION_ARG_NONE, &whole, N_("With --search: match whole words only"), NULL },
        { "version", 0, 0, G_OPTION_ARG_NONE, &version, N_("Show the version and exit"), NULL },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &files, NULL, N_("[MODULE.mybible…]") },
        { NULL, 0, 0, 0, NULL, NULL, NULL },
    };
    GOptionContext *ctx;
    GError *err = NULL;
    KsConfig *cfg;
    int status = 0;

    ks_i18n_init();
    ctx = g_option_context_new(NULL);
    g_option_context_add_main_entries(ctx, entries, "kisword");
    /* GTK's own options, without opening the display yet: --dump and
     * --check must work with no X server. */
    g_option_context_add_group(ctx, gtk_get_option_group(FALSE));
    if (!g_option_context_parse(ctx, &argc, &argv, &err)) {
        fprintf(stderr, "kisword: %s\n", err->message);
        return 2;
    }
    g_option_context_free(ctx);

    if (version) {
        printf("kisword %s\n", KS_VERSION);
        return 0;
    }

    cfg = ks_config_load();
    ks_books_set_lang(cfg->book_names);

    if (dump) {
        status = cli_dump(files, dump);
    } else if (check) {
        status = cli_check(files);
    } else if (search) {
        status = cli_search(files, search, phrase, whole);
    } else {
        gtk_init(&argc, &argv);
        ks_window_run(cfg, files, ref);
    }
    ks_config_free(cfg);
    g_strfreev(files);
    return status;
}
