/* test_search.c - folding, text search modes and Strong's search against a
 * small module built here. */
#include "search.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static const char sql[] =
    "CREATE TABLE Bible (Book INT, Chapter INT, Verse INT, Scripture TEXT);"
    "CREATE TABLE Details (Title TEXT, Abbreviation TEXT, VerseRules TEXT);"
    "INSERT INTO Details VALUES ('Search', 'SRC', '<WT>(.*?)<W(G\\d+)>' || char(9) ||"
    " '<a href=''s$2''>$1</a>');"
    "INSERT INTO Bible VALUES (1, 1, 1, 'No princípio<WH7225> criou Deus<WH430> os céus.');"
    "INSERT INTO Bible VALUES (1, 1, 2, 'E a terra era sem forma<WH08414>, Deus<WH0430>.<RF>nota amor<Rf>');"
    "INSERT INTO Bible VALUES (19, 23, 1, '<TS>Salmo de Davi<Ts>O SENHOR é o meu pastor; nada me faltará.');"
    "INSERT INTO Bible VALUES (43, 3, 16, 'Porque Deus amou o mundo de tal maneira...');"
    "INSERT INTO Bible VALUES (62, 4, 8, 'Aquele que não ama não conhece a Deus; porque Deus é amor.');"
    "INSERT INTO Bible VALUES (62, 4, 16, 'E nós conhecemos o <WT>amor<WG26> que Deus nos tem.');";

static char *refs(GArray *hits)
{
    GString *g = g_string_new(NULL);

    for (guint i = 0; i < hits->len; i++) {
        const KsHit *h = &g_array_index(hits, KsHit, i);

        g_string_append_printf(g, "%s%d.%d.%d", i ? " " : "", h->book, h->chapter, h->verse);
    }
    g_array_free(hits, TRUE);
    return g_string_free(g, FALSE);
}

static void expect(const KsSearchIndex *idx, const char *q, KsSearchMode mode, gboolean whole, int from, int to,
                   const char *want)
{
    KsSearchOptions opt = { mode, whole, from, to };
    char *got = refs(ks_search_text(idx, q, &opt));

    if (strcmp(got, want) != 0) {
        printf("FAIL search '%s' mode %d whole %d: want [%s] got [%s]\n", q, mode, whole, want, got);
        failures++;
    }
    g_free(got);
}

static void expect_fold(const char *in, const char *want)
{
    char *got = ks_search_fold(in);

    if (strcmp(got, want) != 0) {
        printf("FAIL fold '%s': want [%s] got [%s]\n", in, want, got);
        failures++;
    }
    g_free(got);
}

int main(void)
{
    char *dir = g_dir_make_tmp("kisword-search-XXXXXX", NULL);
    char *path = g_build_filename(dir, "s.bbl.mybible", NULL);
    KsSearchOptions all = { KS_SEARCH_ALL_WORDS, FALSE, 0, 0 };
    KsSearchIndex *idx;
    KsModule *m;
    sqlite3 *db;
    char t, *got;
    int n, steps = 0;

    expect_fold("No princípio, criou DEUS!", " no principio criou deus ");
    expect_fold("  ", " ");
    expect_fold("בְּרֵאשִׁ֖ית בָּרָ֣א", " בראשית ברא "); /* points and accents go */
    expect_fold("don't—stop", " don t stop ");
    expect_fold("神爱世人，甚至", " 神爱世人 甚至 ");
    expect_fold("a\xe2\x80\x8e" "b", " ab "); /* left-to-right mark */

    CHECK(ks_search_parse_strong("H430", &t, &n) && t == 'H' && n == 430);
    CHECK(ks_search_parse_strong(" g02316 ", &t, &n) && t == 'G' && n == 2316);
    CHECK(!ks_search_parse_strong("H", &t, &n) && !ks_search_parse_strong("Hab 1", &t, &n) &&
          !ks_search_parse_strong("x430", &t, &n) && !ks_search_parse_strong("H0", &t, &n));

    sqlite3_open(path, &db);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
    m = ks_module_probe(path, NULL);
    if (!m || !ks_module_open(m, NULL)) {
        printf("FAIL cannot open the test module\n");
        return 1;
    }

    idx = ks_search_index_new(m);
    CHECK(!ks_search_index_done(idx) && ks_search_index_progress(idx) == 0.0);
    got = refs(ks_search_text(idx, "deus", &all));
    CHECK(strcmp(got, "") == 0); /* not built yet: nothing */
    g_free(got);
    while (!ks_search_index_step(idx, 2))
        steps++;
    CHECK(steps == 3 && ks_search_index_done(idx) && ks_search_index_progress(idx) == 1.0);

    expect(idx, "deus", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "1.1.1 1.1.2 43.3.16 62.4.8 62.4.16");
    expect(idx, "DEUS céus", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "1.1.1");
    expect(idx, "principio", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "1.1.1");     /* accents ignored */
    expect(idx, "amor", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "62.4.8 62.4.16"); /* not in the note */
    expect(idx, "amo", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "43.3.16 62.4.8 62.4.16");
    expect(idx, "amo", KS_SEARCH_ALL_WORDS, TRUE, 0, 0, "");
    expect(idx, "amor", KS_SEARCH_ALL_WORDS, TRUE, 0, 0, "62.4.8 62.4.16");
    expect(idx, "deus amor", KS_SEARCH_PHRASE, FALSE, 0, 0, "");
    expect(idx, "deus, é amor", KS_SEARCH_PHRASE, FALSE, 0, 0, "62.4.8");    /* punctuation ignored */
    expect(idx, "eus é am", KS_SEARCH_PHRASE, FALSE, 0, 0, "62.4.8");
    expect(idx, "eus é am", KS_SEARCH_PHRASE, TRUE, 0, 0, "");
    expect(idx, "pastor mundo", KS_SEARCH_ANY_WORD, FALSE, 0, 0, "19.23.1 43.3.16");
    expect(idx, "salmo", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "");              /* titles are not text */
    expect(idx, "deus", KS_SEARCH_ALL_WORDS, FALSE, 40, 66, "43.3.16 62.4.8 62.4.16");
    expect(idx, "deus", KS_SEARCH_ALL_WORDS, FALSE, 1, 39, "1.1.1 1.1.2");
    expect(idx, "  ", KS_SEARCH_ALL_WORDS, FALSE, 0, 0, "");
    expect(idx, ",", KS_SEARCH_PHRASE, FALSE, 0, 0, "");
    ks_search_index_free(idx);

    got = refs(ks_search_strong(m, 'H', 430, &all));
    CHECK(strcmp(got, "1.1.1 1.1.2") == 0); /* <WH430> and <WH0430> */
    g_free(got);
    got = refs(ks_search_strong(m, 'G', 430, &all));
    CHECK(strcmp(got, "") == 0);
    g_free(got);
    got = refs(ks_search_strong(m, 'G', 26, &all)); /* link made by the VerseRules */
    CHECK(strcmp(got, "62.4.16") == 0);
    g_free(got);
    got = refs(ks_search_strong(m, 'H', 8414, &all));
    CHECK(strcmp(got, "1.1.2") == 0);
    g_free(got);

    ks_module_free(m);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);

    if (failures)
        printf("test_search: %d failure(s)\n", failures);
    else
        printf("test_search: OK\n");
    return failures != 0;
}
