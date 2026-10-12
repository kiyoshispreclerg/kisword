/* test_module.c - module discovery and reading, against small modules built
 * here with the schemas seen in real ones (KJV-style and RVP-style Details,
 * an encrypted one, and a file that isn't a database). */
#include "module.h"

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

#define BIBLE_TABLE                                                                          \
    "CREATE TABLE Bible (Book INT, Chapter INT, Verse INT, Scripture TEXT);"                 \
    "CREATE UNIQUE INDEX bible_key ON Bible (Book, Chapter, Verse);"

static const char full_sql[] = BIBLE_TABLE
    "CREATE TABLE Details (Title NVARCHAR(255), Description TEXT, Abbreviation NVARCHAR(50),"
    " Language NVARCHAR(3), RightToLeft BOOL, OT BOOL, NT BOOL, Strong BOOL, VerseRules TEXT);"
    "INSERT INTO Details VALUES ('Full Test Bible', 'desc', 'FTB', 'eng', 0, 1, 1, 1, NULL);"
    "INSERT INTO Bible VALUES (1, 1, 1, 'In the beginning<WH7225>');"
    "INSERT INTO Bible VALUES (1, 1, 2, 'And the earth');"
    "INSERT INTO Bible VALUES (1, 2, 1, 'Thus');"
    "INSERT INTO Bible VALUES (43, 3, 16, 'For God');";

/* RVP-style: no Title, no Language, a Font column, booleans as text. */
static const char old_sql[] = BIBLE_TABLE
    "CREATE TABLE Details (Description NVARCHAR(250), Abbreviation NVARCHAR(50), Comments TEXT,"
    " Version INT, Font TEXT, RightToLeft BOOL, OT BOOL, NT BOOL, Apocrypha BOOL, Strong BOOL);"
    "INSERT INTO Details VALUES ('Old Style', 'AAA', NULL, 1, 'DEFAULT', 'true', '1', '0', '0', '0');"
    "INSERT INTO Bible VALUES (19, 23, 1, 'O SENHOR é o meu pastor');";

static const char enc_sql[] = BIBLE_TABLE
    "CREATE TABLE Details (Title TEXT, Abbreviation TEXT, encryption INT default 0, compressed INT default 0);"
    "INSERT INTO Details VALUES ('Encrypted', 'ENC', 2, 1);"
    "INSERT INTO Bible VALUES (1, 1, 1, x'f3a66284');";

/* VerseRules (one broken) and CustomCSS, as the Open* modules have them. */
static const char rules_sql[] = BIBLE_TABLE
    "CREATE TABLE Details (Title TEXT, Abbreviation TEXT, VerseRules TEXT, CustomCSS TEXT);"
    "INSERT INTO Details VALUES ('Rules', 'RUL', '<G>(.*?)<g>' || char(9) || '<span class=''c''>$1</span>'"
    " || char(10) || '((' || char(9) || 'x', 'span.c {color: @darken(@green,5%);}');"
    "INSERT INTO Bible VALUES (43, 1, 1, '<G>logos<g> word');";

static const char nodetails_sql[] = BIBLE_TABLE
    "INSERT INTO Bible VALUES (1, 1, 1, x'4f6c61ff');"; /* "Ola" + an invalid UTF-8 byte, as TEXT below */

static void verse_cb(int verse, const char *text, void *data)
{
    GString *g = data;

    g_string_append_printf(g, "%d=%s;", verse, text);
}

int main(void)
{
    char *dir = g_dir_make_tmp("kisword-test-XXXXXX", NULL);
    char *full = g_build_filename(dir, "full.bbl.mybible", NULL);
    char *old = g_build_filename(dir, "old.BBL.MYBIBLE", NULL);
    char *enc = g_build_filename(dir, "enc.bbl.mybible", NULL);
    char *nod = g_build_filename(dir, "nodetails.bbl.mybible", NULL);
    char *rul = g_build_filename(dir, "rules.bbl.mybible", NULL);
    char *junk = g_build_filename(dir, "junk.bbl.mybible", NULL);
    char *other = g_build_filename(dir, "readme.txt", NULL);
    const char *dirs[] = { dir, NULL };
    const char *files[] = { full, NULL }; /* duplicate of one found in dir */
    GPtrArray *mods;
    GError *err = NULL;
    KsModule *m;
    GString *g;

    make_db(full, full_sql);
    make_db(old, old_sql);
    make_db(enc, enc_sql);
    make_db(nod, nodetails_sql);
    make_db(rul, rules_sql);
    {
        /* Store the invalid bytes as TEXT, as a badly converted module would. */
        sqlite3 *db;

        sqlite3_open(nod, &db);
        sqlite3_exec(db, "UPDATE Bible SET Scripture = CAST(Scripture AS TEXT)", NULL, NULL, NULL);
        sqlite3_close(db);
    }
    g_file_set_contents(junk, "this is not a database, just text that is long enough", -1, NULL);
    g_file_set_contents(other, "x", -1, NULL);

    m = ks_module_probe(junk, &err);
    CHECK(m == NULL && err != NULL);
    g_clear_error(&err);

    mods = ks_module_scan(dirs, files);
    CHECK(mods->len == 5); /* full, old, enc, nodetails, rules; junk and readme skipped, full once */
    for (guint i = 0; i < mods->len; i++) {
        KsModule *x = g_ptr_array_index(mods, i);

        if (strcmp(ks_module_label(x), "FTB") == 0) {
            CHECK(x->supported);
            CHECK(strcmp(x->title, "Full Test Bible") == 0);
            CHECK(x->strong && x->ot && x->nt && !x->rtl);
            CHECK(x->language && strcmp(x->language, "eng") == 0);
            CHECK(x->verse_rules == NULL);
        } else if (strcmp(ks_module_label(x), "AAA") == 0) {
            CHECK(x->supported);
            CHECK(strcmp(x->title, "Old Style") == 0); /* from Description */
            CHECK(x->font == NULL);                    /* "DEFAULT" dropped */
            CHECK(x->rtl && x->ot && !x->nt);          /* 'true', '1', '0' */
            CHECK(x->language == NULL);
        } else if (strcmp(ks_module_label(x), "ENC") == 0) {
            CHECK(!x->supported && x->unsupported_why);
            CHECK(!ks_module_open(x, &err) && err);
            g_clear_error(&err);
        } else if (strcmp(ks_module_label(x), "nodetails") == 0) {
            CHECK(x->supported); /* title from the file name */
            CHECK(ks_module_open(x, NULL));
            g = g_string_new(NULL);
            ks_module_chapter(x, 1, 1, verse_cb, g);
            CHECK(g_utf8_validate(g->str, -1, NULL));
            CHECK(g_str_has_prefix(g->str, "1=Ola"));
            g_string_free(g, TRUE);
        } else if (strcmp(ks_module_label(x), "RUL") == 0) {
            const KsClassStyle *cs;

            CHECK(x->verse_rules && x->custom_css);
            CHECK(ks_module_open(x, NULL));
            CHECK(ks_rules_count(x->rules) == 1 && x->rules_invalid == 1);
            cs = ks_style_sheet_class(x->css, "span", "c", -1);
            CHECK(cs && cs->color && strcmp(cs->color, "#2e7d32") == 0);
            g = g_string_new(NULL);
            ks_module_chapter(x, 43, 1, verse_cb, g);
            CHECK(strcmp(g->str, "1=<span class='c'>logos</span> word;") == 0);
            g_string_free(g, TRUE);
            ks_module_close(x);
            CHECK(x->rules == NULL && x->css == NULL);
        } else {
            printf("unexpected module %s\n", ks_module_label(x));
            failures++;
        }
    }
    CHECK(strcmp(ks_module_label(g_ptr_array_index(mods, 0)), "AAA") == 0); /* sorted */
    g_ptr_array_free(mods, TRUE);

    m = ks_module_probe(full, NULL);
    if (!m || !ks_module_open(m, NULL)) {
        printf("FAIL cannot open %s\n", full);
        return 1;
    }
    CHECK(m->nchapters[1] == 2 && m->nchapters[43] == 3 && m->nchapters[2] == 0);
    CHECK(ks_module_next_book(m, 0) == 1 && ks_module_next_book(m, 1) == 43 && ks_module_next_book(m, 43) == 0);
    CHECK(ks_module_prev_book(m, 43) == 1 && ks_module_prev_book(m, 1) == 0);
    g = g_string_new(NULL);
    CHECK(ks_module_chapter(m, 1, 1, verse_cb, g) == 2);
    CHECK(strcmp(g->str, "1=In the beginning<WH7225>;2=And the earth;") == 0);
    g_string_truncate(g, 0);
    CHECK(ks_module_chapter(m, 1, 1, verse_cb, g) == 2); /* statement reused */
    CHECK(ks_module_chapter(m, 5, 1, verse_cb, g) == 0);
    g_string_free(g, TRUE);
    ks_module_free(m);

    g_remove(full);
    g_remove(old);
    g_remove(enc);
    g_remove(nod);
    g_remove(rul);
    g_remove(junk);
    g_remove(other);
    g_rmdir(dir);
    g_free(full);
    g_free(old);
    g_free(enc);
    g_free(nod);
    g_free(rul);
    g_free(junk);
    g_free(other);
    g_free(dir);

    if (failures)
        printf("test_module: %d failure(s)\n", failures);
    else
        printf("test_module: OK\n");
    return failures != 0;
}
