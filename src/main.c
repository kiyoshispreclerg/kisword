/* main.c - kisword: a light Bible reader for MySword modules.
 *
 * Besides the window, two command-line modes that need no display:
 *   kisword --dump "Jo 3:16" FILE   prints verses as plain text
 *   kisword --check FILE...         parses every verse, reports problems
 * Both are what `make test` uses against real modules. */
#include "books.h"
#include "config.h"
#include "gbf.h"
#include "ks_i18n.h"
#include "module.h"
#include "ref.h"
#include "window.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

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

static int cli_check(char **files)
{
    int status = 0;

    for (int i = 0; files && files[i]; i++) {
        GError *err = NULL;
        KsModule *m = ks_module_probe(files[i], &err);
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
    char *ref = NULL, *dump = NULL;
    char **files = NULL;
    gboolean check = FALSE, version = FALSE;
    GOptionEntry entries[] = {
        { "ref", 'r', 0, G_OPTION_ARG_STRING, &ref, N_("Open at this reference (e.g. \"Jo 3:16\")"), N_("REF") },
        { "dump", 0, 0, G_OPTION_ARG_STRING, &dump, N_("Print REF from the given module as plain text and exit"),
          N_("REF") },
        { "check", 0, 0, G_OPTION_ARG_NONE, &check, N_("Parse every verse of the given modules and report problems"),
          NULL },
        { "version", 0, 0, G_OPTION_ARG_NONE, &version, N_("Show the version and exit"), NULL },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &files, NULL, N_("[MODULE.bbl.mybible…]") },
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
    } else {
        gtk_init(&argc, &argv);
        ks_window_run(cfg, files, ref);
    }
    ks_config_free(cfg);
    g_strfreev(files);
    return status;
}
