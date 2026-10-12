/* test_res.c - dictionaries, commentaries, books and journals, against
 * small modules built here with the schemas of real ones (strong.dct with
 * relativeorder, kjvd without, OpenGNTd's extra tables, tsk.cmt, a book
 * with a hidden entry and an image, an encrypted dictionary). */
#include "res.h"

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

static void make_db(const char *path, const char *sql)
{
    sqlite3 *db;
    char *err = NULL;

    g_remove(path);
    if (sqlite3_open(path, &db) != SQLITE_OK || sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        printf("cannot build %s: %s\n", path, err ? err : sqlite3_errmsg(db));
        failures++;
    }
    sqlite3_free(err);
    sqlite3_close(db);
}

#define DICT_DETAILS                                                                                   \
    "CREATE TABLE details(title TEXT, abbreviation TEXT, description TEXT, strong INTEGER, "             \
    "righttoleft INT default 0, customcss TEXT);"

static const char strong_sql[] = DICT_DETAILS
    "INSERT INTO details VALUES ('Strong', 'Strong', 'desc', 1, 0, '.c{color:red}');"
    "CREATE TABLE dictionary(relativeorder INTEGER, word TEXT primary key collate nocase, data TEXT);"
    "INSERT INTO dictionary VALUES (1, 'H1', '<p>father</p>');"
    "INSERT INTO dictionary VALUES (2, 'H2', '<p>father (Aramaic)</p>');"
    "INSERT INTO dictionary VALUES (10, 'H10', '<p>ten</p>');"
    "INSERT INTO dictionary VALUES (430, 'H430', '<p>God, <a href=''#dH433''>H433</a></p>');"
    "INSERT INTO dictionary VALUES (431, 'H431', '<p>lo</p>');"
    "INSERT INTO dictionary VALUES (5000, 'G26', '<p>love</p>');";

/* kjvd: no relativeorder, words in any case. */
static const char kjvd_sql[] = DICT_DETAILS
    "INSERT INTO details VALUES ('King James Dictionary', 'KJVD', NULL, 0, 0, NULL);"
    "CREATE TABLE dictionary(word TEXT primary key collate nocase, data TEXT);"
    "INSERT INTO dictionary VALUES ('Abase', 'To bring low');"
    "INSERT INTO dictionary VALUES ('abate', 'To lessen');"
    "INSERT INTO dictionary VALUES ('Abba', 'Father');"
    "INSERT INTO dictionary VALUES ('Zeal', 'Ardour');";

/* OpenGNTd: no "dictionary" table; words, clauses, annotations. */
static const char ognt_sql[] =
    "CREATE TABLE Details(Title TEXT, Abbreviation TEXT, Strong INT);"
    "INSERT INTO Details VALUES ('Open Greek New Testament Dictionary', 'OGNTd', 1);"
    "CREATE TABLE ogntc(clauseid TEXT primary key, data TEXT);"
    "CREATE TABLE ogntd(word TEXT primary key, data TEXT, clauseid TEXT);"
    "CREATE TABLE ogntl(lgntdfid TEXT primary key, data TEXT);"
    "INSERT INTO ogntd VALUES ('000001', '<h2>WORD ID: 000001</h2>', 'c1');"
    "INSERT INTO ogntc VALUES ('c1', '<h2>Clause ID: 1</h2>');"
    "INSERT INTO ogntl VALUES ('D.40.1.2!1', '<h2>LGNTDF</h2>');";

static const char cmt_sql[] =
    "CREATE TABLE details(title TEXT, abbreviation TEXT, righttoleft INT);"
    "INSERT INTO details VALUES ('Treasury of Scripture Knowledge', 'TSK', 0);"
    "CREATE TABLE commentary(id INTEGER primary key autoincrement, book INTEGER, chapter INTEGER, "
    "fromverse INTEGER, toverse INTEGER, data TEXT);"
    "CREATE INDEX idx_commentary on commentary(book, chapter, fromverse);"
    "INSERT INTO commentary (book, chapter, fromverse, toverse, data) VALUES (43, 3, 16, 16, 'so loved');"
    "INSERT INTO commentary (book, chapter, fromverse, toverse, data) VALUES (43, 3, 0, 0, 'chapter');"
    "INSERT INTO commentary (book, chapter, fromverse, toverse, data) VALUES (43, 3, 17, 18, 'sent');"
    "INSERT INTO commentary (book, chapter, fromverse, toverse, data) VALUES (43, 4, 1, 1, 'other');";

#define JOURNAL_TABLES                                                                                 \
    "CREATE TABLE data(id TEXT primary key, description TEXT, date TEXT, filename TEXT, content BLOB, " \
    "thumbnail BLOB);"

static const char book_sql[] = JOURNAL_TABLES
    "CREATE TABLE details(name TEXT, title TEXT, abbreviation TEXT, righttoleft INT);"
    "INSERT INTO details VALUES ('Grammar', 'A Grammar', 'Grammar', 0);"
    "CREATE TABLE journal(id TEXT primary key, title TEXT, date TEXT, tags TEXT, content TEXT, "
    "relativeorder INTEGER, hidden INTEGER);"
    "INSERT INTO journal VALUES ('Nouns', 'Nouns', NULL, NULL, '<p>about nouns</p>', 2, 0);"
    "INSERT INTO journal VALUES ('Contents', 'Table of Contents', NULL, NULL, '<a href=''j Nouns''>N</a>', 1, 0);"
    "INSERT INTO journal VALUES ('Secret', 'Secret', NULL, NULL, 'x', 3, 1);"
    "INSERT INTO journal VALUES ('Verbs', '', NULL, NULL, '<img src=''1.png''>', 4, NULL);"
    "INSERT INTO data VALUES ('1.png', '1.png', NULL, '1.png', x'89504e47', NULL);";

/* A journal: no relativeorder, ordered by date. */
static const char jor_sql[] = JOURNAL_TABLES
    "CREATE TABLE details(name TEXT, title TEXT, abbreviation TEXT);"
    "INSERT INTO details VALUES ('J', 'My journal', 'J');"
    "CREATE TABLE journal(id TEXT primary key, title TEXT, date TEXT, tags TEXT, content TEXT);"
    "INSERT INTO journal VALUES ('b', 'Later', '2007-10-17 06:00:00', '', 'later');"
    "INSERT INTO journal VALUES ('a', 'Earlier', '2006-09-02 06:00:00', '', 'earlier');";

static const char enc_sql[] =
    "CREATE TABLE details(title TEXT, abbreviation TEXT, encryption INT);"
    "INSERT INTO details VALUES ('Locked', 'L', 2);"
    "CREATE TABLE dictionary(word TEXT primary key, data BLOB);"
    "INSERT INTO dictionary VALUES ('H1', x'f3a66284');";

static KsRes *open_ok(const char *path)
{
    GError *err = NULL;
    KsRes *r = ks_res_probe(path, &err);

    if (!r || !ks_res_open(r, &err)) {
        printf("FAIL open %s: %s\n", path, err ? err->message : "?");
        failures++;
        g_clear_error(&err);
        ks_res_free(r);
        return NULL;
    }
    return r;
}

static char *join_words(GPtrArray *w)
{
    GString *g = g_string_new(NULL);

    for (guint i = 0; i < w->len; i++)
        g_string_append_printf(g, "%s%s", i ? " " : "", (char *)g_ptr_array_index(w, i));
    g_ptr_array_free(w, TRUE);
    return g_string_free(g, FALSE);
}

static void expect_words(KsRes *r, const char *from, gboolean after, int limit, const char *want)
{
    char *got = join_words(ks_res_words(r, from, after, limit));

    if (strcmp(got, want) != 0) {
        printf("FAIL words %s from [%s]%s: want [%s] got [%s]\n", ks_res_label(r), from, after ? " after" : "", want,
               got);
        failures++;
    }
    g_free(got);
}

static void comment_cb(int from, int to, const char *html, void *data)
{
    g_string_append_printf(data, "%d-%d:%s;", from, to, html);
}

static void count_cb(const char *where, const char *html, gboolean valid, void *data)
{
    (void)where;
    (void)html;
    *(int *)data += valid ? 1 : 100;
}

int main(void)
{
    char *dir = g_dir_make_tmp("kisword-res-XXXXXX", NULL);
    char *p_strong = g_build_filename(dir, "strong.dct.mybible", NULL);
    char *p_kjvd = g_build_filename(dir, "kjvd.dct.mybible", NULL);
    char *p_ognt = g_build_filename(dir, "OpenGNTd.dct.mybible", NULL);
    char *p_cmt = g_build_filename(dir, "tsk.cmt.mybible", NULL);
    char *p_book = g_build_filename(dir, "Grammar.bok.mybible", NULL);
    char *p_jor = g_build_filename(dir, "j.jor.mybible", NULL);
    char *p_enc = g_build_filename(dir, "locked.dct.mybible", NULL);
    char *p_bad = g_build_filename(dir, "bad.dct.mybible", NULL);
    KsRes *r;
    KsResKind kind;
    char *s, *key;
    GError *err = NULL;

    make_db(p_strong, strong_sql);
    make_db(p_kjvd, kjvd_sql);
    make_db(p_ognt, ognt_sql);
    make_db(p_cmt, cmt_sql);
    make_db(p_book, book_sql);
    make_db(p_jor, jor_sql);
    make_db(p_enc, enc_sql);
    g_file_set_contents(p_bad, "not a database", -1, NULL);

    CHECK(ks_res_kind_of("/x/A.DCT.MYBIBLE", &kind) && kind == KS_RES_DICT);
    CHECK(ks_res_kind_of("tsk.cmt.mybible", &kind) && kind == KS_RES_COMMENTARY);
    CHECK(!ks_res_kind_of("kjv.bbl.mybible", &kind));

    /* Dictionary in its own order. */
    if ((r = open_ok(p_strong))) {
        CHECK(r->kind == KS_RES_DICT && r->strong && r->ordered && strcmp(ks_res_label(r), "Strong") == 0);
        CHECK(ks_res_named(r, "strong"));
        s = ks_res_lookup(r, "h430", &key); /* case ignored, as the column says */
        CHECK(s && strstr(s, "God") && key && strcmp(key, "H430") == 0);
        g_free(s);
        g_free(key);
        CHECK(!ks_res_lookup(r, "H9999", NULL));
        expect_words(r, "", FALSE, 3, "H1 H2 H10");
        expect_words(r, "H43", FALSE, 3, "H430 H431 G26"); /* "H43" falls before H430 */
        expect_words(r, "H2", TRUE, 2, "H10 H430");
        expect_words(r, "zzz", FALSE, 3, "");
        ks_res_free(r);
    }
    /* Alphabetical. */
    if ((r = open_ok(p_kjvd))) {
        CHECK(!r->ordered && !r->strong);
        expect_words(r, "ab", FALSE, 10, "Abase abate Abba Zeal");
        expect_words(r, "ABATE", FALSE, 2, "abate Abba");
        expect_words(r, "abate", TRUE, 5, "Abba Zeal");
        s = ks_res_lookup(r, "ABBA", NULL);
        CHECK(s && strcmp(s, "Father") == 0);
        g_free(s);
        ks_res_free(r);
    }
    /* Keys in more than one table. */
    if ((r = open_ok(p_ognt))) {
        expect_words(r, "", FALSE, 5, "000001");
        s = ks_res_lookup(r, "c1", NULL);
        CHECK(s && strstr(s, "Clause"));
        g_free(s);
        s = ks_res_lookup(r, "D.40.1.2!1", NULL);
        CHECK(s && strstr(s, "LGNTDF"));
        g_free(s);
        ks_res_free(r);
    }
    /* Commentary: one chapter, in verse order, verse 0 first. */
    if ((r = open_ok(p_cmt))) {
        GString *g = g_string_new(NULL);
        int n = 0;

        CHECK(ks_res_comments(r, 43, 3, comment_cb, g) == 3);
        CHECK(strcmp(g->str, "0-0:chapter;16-16:so loved;17-18:sent;") == 0);
        CHECK(ks_res_comments(r, 1, 1, comment_cb, g) == 0);
        CHECK(ks_res_foreach(r, count_cb, &n) == 4 && n == 4);
        g_string_free(g, TRUE);
        ks_res_free(r);
    }
    /* Book: order, hidden entries, titles, images. */
    if ((r = open_ok(p_book))) {
        GPtrArray *e = ks_res_entries(r);
        GBytes *img;
        char *id;

        CHECK(r->kind == KS_RES_BOOK && r->ordered && r->hidden && r->images);
        CHECK(e->len == 3);
        if (e->len == 3) {
            CHECK(strcmp(((KsEntry *)g_ptr_array_index(e, 0))->title, "Table of Contents") == 0);
            CHECK(strcmp(((KsEntry *)g_ptr_array_index(e, 1))->id, "Nouns") == 0);
            CHECK(strcmp(((KsEntry *)g_ptr_array_index(e, 2))->title, "Verbs") == 0); /* no title: the id */
        }
        g_ptr_array_free(e, TRUE);
        s = ks_res_entry(r, "table of contents", &id); /* by title, any case */
        CHECK(s && strstr(s, "j Nouns") && id && strcmp(id, "Contents") == 0);
        g_free(s);
        g_free(id);
        CHECK(!ks_res_entry(r, "Missing", NULL));
        img = ks_res_image(r, "images/1.png");
        CHECK(img && g_bytes_get_size(img) == 4);
        if (img)
            g_bytes_unref(img);
        CHECK(!ks_res_image(r, "2.png"));
        img = ks_res_image(r, "data:image/gif;base64,R0lGODlh"); /* "GIF89a" */
        CHECK(img && g_bytes_get_size(img) == 6);
        if (img)
            g_bytes_unref(img);
        ks_res_free(r);
    }
    /* Journal: by date. */
    if ((r = open_ok(p_jor))) {
        GPtrArray *e = ks_res_entries(r);

        CHECK(r->kind == KS_RES_JOURNAL && e->len == 2 &&
              strcmp(((KsEntry *)g_ptr_array_index(e, 0))->title, "Earlier") == 0);
        g_ptr_array_free(e, TRUE);
        ks_res_free(r);
    }
    /* Encrypted: listed, not opened. */
    r = ks_res_probe(p_enc, &err);
    CHECK(r && !r->supported && r->unsupported_why);
    CHECK(r && !ks_res_open(r, NULL));
    ks_res_free(r);
    /* Not a database at all. */
    r = ks_res_probe(p_bad, &err);
    CHECK(!r && err);
    g_clear_error(&err);

    /* Scanning: every kind found, the broken file left out. */
    {
        const char *dirs[] = { dir, NULL };
        GPtrArray *all = ks_res_scan(dirs, NULL);

        CHECK(all->len == 7);
        g_ptr_array_free(all, TRUE);
    }

    g_remove(p_strong);
    g_remove(p_kjvd);
    g_remove(p_ognt);
    g_remove(p_cmt);
    g_remove(p_book);
    g_remove(p_jor);
    g_remove(p_enc);
    g_remove(p_bad);
    g_rmdir(dir);
    g_free(p_strong);
    g_free(p_kjvd);
    g_free(p_ognt);
    g_free(p_cmt);
    g_free(p_book);
    g_free(p_jor);
    g_free(p_enc);
    g_free(p_bad);
    g_free(dir);

    if (failures)
        printf("test_res: %d failure(s)\n", failures);
    else
        printf("test_res: OK\n");
    return failures != 0;
}
