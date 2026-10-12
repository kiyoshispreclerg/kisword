/* module.c - discovering and reading MySword Bible modules (see module.h).
 *
 * A module is a plain SQLite 3 database. Two tables matter: Bible(Book,
 * Chapter, Verse, Scripture) and Details, whose columns differ from module
 * to module (KISWORD_PLAN.md 1.2) -- so Details is read with SELECT * and
 * matched by column name, never by position. Files are only ever opened
 * read-only. */
#include "module.h"

#include "ks_i18n.h"

#include <stdlib.h>
#include <string.h>

G_DEFINE_QUARK(ks-module-error-quark, ks_module_error)

static void db_error(GError **err, const char *path, sqlite3 *db)
{
    g_set_error(err, KS_MODULE_ERROR, 0, "%s: %s", path, db ? sqlite3_errmsg(db) : "out of memory");
}

static gboolean col_is(sqlite3_stmt *st, int i, const char *name)
{
    const char *c = sqlite3_column_name(st, i);

    return c && g_ascii_strcasecmp(c, name) == 0;
}

static char *col_text(sqlite3_stmt *st, int i)
{
    const char *t = (const char *)sqlite3_column_text(st, i);
    char *s;

    if (!t)
        return NULL;
    s = g_utf8_validate(t, -1, NULL) ? g_strdup(t) : g_utf8_make_valid(t, -1);
    g_strstrip(s);
    if (!*s) {
        g_free(s);
        return NULL;
    }
    return s;
}

static gboolean col_bool(sqlite3_stmt *st, int i)
{
    if (sqlite3_column_type(st, i) == SQLITE_TEXT) {
        const char *t = (const char *)sqlite3_column_text(st, i);

        if (!t)
            return FALSE;
        if (*t == 't' || *t == 'T' || *t == 'y' || *t == 'Y')
            return TRUE;
        return atoi(t) != 0;
    }
    return sqlite3_column_int(st, i) != 0;
}

static void read_details(KsModule *m, sqlite3 *db, int *encryption, int *compressed,
                         char **description)
{
    sqlite3_stmt *st;

    if (sqlite3_prepare_v2(db, "SELECT * FROM Details LIMIT 1", -1, &st, NULL) != SQLITE_OK)
        return; /* no Details at all: everything falls back */
    if (sqlite3_step(st) == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(st); i++) {
            if (col_is(st, i, "Title"))
                m->title = col_text(st, i);
            else if (col_is(st, i, "Description"))
                *description = col_text(st, i);
            else if (col_is(st, i, "Abbreviation"))
                m->abbrev = col_text(st, i);
            else if (col_is(st, i, "Language"))
                m->language = col_text(st, i);
            else if (col_is(st, i, "Font"))
                m->font = col_text(st, i);
            else if (col_is(st, i, "VerseRules"))
                m->verse_rules = col_text(st, i);
            else if (col_is(st, i, "CustomCSS"))
                m->custom_css = col_text(st, i);
            else if (col_is(st, i, "RightToLeft"))
                m->rtl = col_bool(st, i);
            else if (col_is(st, i, "RightToLeftOT"))
                m->rtl_ot = col_bool(st, i);
            else if (col_is(st, i, "RightToLeftNT"))
                m->rtl_nt = col_bool(st, i);
            else if (col_is(st, i, "OT"))
                m->ot = col_bool(st, i);
            else if (col_is(st, i, "NT"))
                m->nt = col_bool(st, i);
            else if (col_is(st, i, "Strong"))
                m->strong = col_bool(st, i);
            else if (col_is(st, i, "encryption"))
                *encryption = sqlite3_column_int(st, i);
            else if (col_is(st, i, "compressed"))
                *compressed = sqlite3_column_int(st, i);
        }
    }
    sqlite3_finalize(st);
}

/* A title is one line: some modules have the whole Description, several
 * lines of it, where the title would go. Keeps the first line, with runs of
 * whitespace as one space. */
static char *one_line(char *s)
{
    char *nl = strpbrk(s, "\r\n"), *d = s;

    if (nl)
        *nl = '\0';
    for (char *p = s; *p; p++)
        if (!(g_ascii_isspace(*p) && (d == s || d[-1] == ' ')))
            *d++ = g_ascii_isspace(*p) ? ' ' : *p;
    *d = '\0';
    return g_strchomp(s);
}

static char *title_from_path(const char *path)
{
    char *base = g_path_get_basename(path);
    size_t n = strlen(base), sfx = strlen(".bbl.mybible");

    if (n > sfx && g_ascii_strcasecmp(base + n - sfx, ".bbl.mybible") == 0)
        base[n - sfx] = '\0';
    return base;
}

KsModule *ks_module_probe(const char *path, GError **err)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *st;
    KsModule *m;
    gboolean blob = FALSE;
    int encryption = 0, compressed = 0;
    char *description = NULL;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        db_error(err, path, db);
        sqlite3_close(db);
        return NULL;
    }
    /* Also the first real read of the file: anything that isn't SQLite fails
     * here with "file is not a database". */
    if (sqlite3_prepare_v2(db, "SELECT typeof(Scripture) FROM Bible LIMIT 1", -1, &st, NULL) != SQLITE_OK) {
        g_set_error(err, KS_MODULE_ERROR, 0, _("%s is not a MySword Bible module (%s)"), path,
                    sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *type = (const char *)sqlite3_column_text(st, 0);

        blob = type && strcmp(type, "blob") == 0;
    }
    sqlite3_finalize(st);

    m = g_new0(KsModule, 1);
    m->path = g_strdup(path);
    m->real = realpath(path, NULL);
    if (!m->real)
        m->real = g_strdup(path);
    m->supported = TRUE;
    m->rtl_ot = m->rtl_nt = -1;

    read_details(m, db, &encryption, &compressed, &description);
    sqlite3_close(db);

    if (m->font && g_ascii_strcasecmp(m->font, "DEFAULT") == 0)
        g_clear_pointer(&m->font, g_free);
    if (!m->title)
        m->title = description ? g_strdup(description) : m->abbrev ? g_strdup(m->abbrev) : title_from_path(path);
    g_free(description);
    one_line(m->title);
    if (m->abbrev)
        one_line(m->abbrev);

    /* MySword's own module protection; kisword does not get around it. */
    if (encryption) {
        m->supported = FALSE;
        m->unsupported_why = g_strdup(_("This is an encrypted MySword module. Encrypted modules are not supported."));
    } else if (compressed) {
        m->supported = FALSE;
        m->unsupported_why = g_strdup(_("This module is compressed in a format kisword does not read yet."));
    } else if (blob) {
        m->supported = FALSE;
        m->unsupported_why = g_strdup(_("The verse text in this module is binary (probably encrypted or compressed)."));
    }
    return m;
}

GHashTable *ks_module_details(const KsModule *m)
{
    GHashTable *t;
    sqlite3 *db = NULL;
    sqlite3_stmt *st;

    if (sqlite3_open_v2(m->path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "SELECT * FROM Details LIMIT 1", -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }
    t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (sqlite3_step(st) == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(st); i++) {
            char *v = sqlite3_column_type(st, i) == SQLITE_BLOB ? NULL : col_text(st, i);

            if (v)
                g_hash_table_insert(t, g_ascii_strdown(sqlite3_column_name(st, i), -1), v);
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return t;
}

void ks_module_free(KsModule *m)
{
    if (!m)
        return;
    ks_module_close(m);
    g_free(m->path);
    free(m->real);
    g_free(m->title);
    g_free(m->abbrev);
    g_free(m->language);
    g_free(m->font);
    g_free(m->verse_rules);
    g_free(m->custom_css);
    g_free(m->unsupported_why);
    g_free(m);
}

gboolean ks_module_rtl(const KsModule *m, int book)
{
    int t = book >= 40 && book <= 66 ? m->rtl_nt : m->rtl_ot;

    return t >= 0 ? t : m->rtl;
}

const char *ks_module_label(const KsModule *m)
{
    return m->abbrev ? m->abbrev : m->title;
}

static void add_module(GPtrArray *out, GHashTable *seen, const char *path)
{
    char *real = realpath(path, NULL);
    KsModule *m;

    if (!real)
        return;
    if (g_hash_table_contains(seen, real)) {
        free(real);
        return;
    }
    g_hash_table_add(seen, g_strdup(real));
    free(real);
    m = ks_module_probe(path, NULL);
    if (m)
        g_ptr_array_add(out, m);
}

static gint by_label(gconstpointer a, gconstpointer b)
{
    const KsModule *ma = *(KsModule *const *)a, *mb = *(KsModule *const *)b;

    return g_utf8_collate(ks_module_label(ma), ks_module_label(mb));
}

GPtrArray *ks_module_scan(const char *const *dirs, const char *const *files)
{
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)ks_module_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (int i = 0; dirs && dirs[i]; i++) {
        GDir *d = g_dir_open(dirs[i], 0, NULL);
        const char *name;

        if (!d)
            continue;
        while ((name = g_dir_read_name(d))) {
            char *lower = g_ascii_strdown(name, -1);

            if (g_str_has_suffix(lower, ".bbl.mybible")) {
                char *path = g_build_filename(dirs[i], name, NULL);

                add_module(out, seen, path);
                g_free(path);
            }
            g_free(lower);
        }
        g_dir_close(d);
    }
    for (int i = 0; files && files[i]; i++)
        add_module(out, seen, files[i]);

    g_ptr_array_sort(out, by_label);
    g_hash_table_destroy(seen);
    return out;
}

gboolean ks_module_open(KsModule *m, GError **err)
{
    sqlite3_stmt *st;
    gboolean any = FALSE;

    if (m->db)
        return TRUE;
    if (!m->supported) {
        g_set_error_literal(err, KS_MODULE_ERROR, 0, m->unsupported_why);
        return FALSE;
    }
    if (sqlite3_open_v2(m->path, &m->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        goto fail;
    if (sqlite3_prepare_v2(m->db,
                           "SELECT Verse, Scripture FROM Bible WHERE Book = ?1 AND Chapter = ?2 ORDER BY Verse",
                           -1, &m->st_chapter, NULL) != SQLITE_OK)
        goto fail;

    memset(m->nchapters, 0, sizeof m->nchapters);
    if (sqlite3_prepare_v2(m->db, "SELECT Book, MAX(Chapter) FROM Bible GROUP BY Book", -1, &st, NULL) != SQLITE_OK)
        goto fail;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int book = sqlite3_column_int(st, 0), chapters = sqlite3_column_int(st, 1);

        if (book >= 1 && book <= KS_MAX_BOOK && chapters > 0) {
            m->nchapters[book] = (guint16)MIN(chapters, G_MAXUINT16);
            any = TRUE;
        }
    }
    sqlite3_finalize(st);
    if (!any) {
        g_set_error(err, KS_MODULE_ERROR, 0, _("%s has no verses."), m->path);
        ks_module_close(m);
        return FALSE;
    }
    m->rules = ks_rules_new(m->verse_rules, &m->rules_invalid);
    m->css = ks_style_sheet_new(m->custom_css);
    return TRUE;

fail:
    db_error(err, m->path, m->db);
    ks_module_close(m);
    return FALSE;
}

void ks_module_close(KsModule *m)
{
    if (m->st_chapter)
        sqlite3_finalize(m->st_chapter);
    m->st_chapter = NULL;
    if (m->db)
        sqlite3_close(m->db);
    m->db = NULL;
    g_clear_pointer(&m->rules, ks_rules_free);
    g_clear_pointer(&m->css, ks_style_sheet_free);
}

/* Column 'col' of the current row as valid UTF-8 (fixed up if needed)
 * with the VerseRules applied; *fixed is set to a string to g_free() when
 * a copy was made. */
static const char *row_text(const KsModule *m, sqlite3_stmt *st, int col, char **fixed, gboolean *was_valid)
{
    const char *t = (const char *)sqlite3_column_text(st, col);
    int n = sqlite3_column_bytes(st, col);
    char *ruled;

    *fixed = NULL;
    *was_valid = TRUE;
    if (!t)
        return "";
    if (!g_utf8_validate(t, n, NULL)) {
        *was_valid = FALSE;
        *fixed = g_utf8_make_valid(t, n);
        t = *fixed;
    }
    ruled = m->rules ? ks_rules_apply(m->rules, t) : NULL;
    if (ruled) {
        g_free(*fixed);
        *fixed = ruled;
        t = ruled;
    }
    return t;
}

int ks_module_chapter(KsModule *m, int book, int chapter, KsVerseCb cb, void *data)
{
    int n = 0;

    if (!m->st_chapter)
        return -1;
    sqlite3_reset(m->st_chapter);
    sqlite3_bind_int(m->st_chapter, 1, book);
    sqlite3_bind_int(m->st_chapter, 2, chapter);
    while (sqlite3_step(m->st_chapter) == SQLITE_ROW) {
        char *fixed;
        gboolean valid;
        const char *text = row_text(m, m->st_chapter, 1, &fixed, &valid);

        cb(sqlite3_column_int(m->st_chapter, 0), text, data);
        g_free(fixed);
        n++;
    }
    sqlite3_reset(m->st_chapter); /* releases the read lock */
    return n;
}

char *ks_module_verse(KsModule *m, int book, int chapter, int verse)
{
    sqlite3_stmt *st;
    char *out = NULL;

    if (!m->db || sqlite3_prepare_v2(m->db, "SELECT Scripture FROM Bible WHERE Book = ?1 AND Chapter = ?2 "
                                            "AND Verse = ?3", -1, &st, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_int(st, 1, book);
    sqlite3_bind_int(st, 2, chapter);
    sqlite3_bind_int(st, 3, verse);
    if (sqlite3_step(st) == SQLITE_ROW) {
        char *fixed;
        gboolean valid;
        const char *text = row_text(m, st, 0, &fixed, &valid);

        out = fixed ? fixed : g_strdup(text);
    }
    sqlite3_finalize(st);
    return out;
}

struct KsVerseIter {
    KsModule *m;
    sqlite3_stmt *st;
    char *fixed;
};

KsVerseIter *ks_module_iter_new(KsModule *m, const char *like)
{
    KsVerseIter *it;
    sqlite3_stmt *st;

    if (!m->db || sqlite3_prepare_v2(m->db, like ? "SELECT Book, Chapter, Verse, Scripture FROM Bible "
                                                   "WHERE Scripture LIKE ?1 ORDER BY Book, Chapter, Verse"
                                                 : "SELECT Book, Chapter, Verse, Scripture FROM Bible "
                                                   "ORDER BY Book, Chapter, Verse",
                                     -1, &st, NULL) != SQLITE_OK)
        return NULL;
    if (like)
        sqlite3_bind_text(st, 1, like, -1, SQLITE_TRANSIENT);
    it = g_new0(KsVerseIter, 1);
    it->m = m;
    it->st = st;
    return it;
}

gboolean ks_module_iter_next(KsVerseIter *it, int *book, int *chapter, int *verse, const char **text,
                             gboolean *was_valid)
{
    gboolean valid;

    g_clear_pointer(&it->fixed, g_free);
    if (sqlite3_step(it->st) != SQLITE_ROW)
        return FALSE;
    *book = sqlite3_column_int(it->st, 0);
    *chapter = sqlite3_column_int(it->st, 1);
    *verse = sqlite3_column_int(it->st, 2);
    *text = row_text(it->m, it->st, 3, &it->fixed, &valid);
    if (was_valid)
        *was_valid = valid;
    return TRUE;
}

void ks_module_iter_free(KsVerseIter *it)
{
    if (!it)
        return;
    sqlite3_finalize(it->st);
    g_free(it->fixed);
    g_free(it);
}

int ks_module_verse_count(KsModule *m)
{
    sqlite3_stmt *st;
    int n = -1;

    if (!m->db || sqlite3_prepare_v2(m->db, "SELECT COUNT(*) FROM Bible", -1, &st, NULL) != SQLITE_OK)
        return -1;
    if (sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

int ks_module_foreach(KsModule *m, KsAllCb cb, void *data)
{
    KsVerseIter *it = ks_module_iter_new(m, NULL);
    int n = 0, book, chapter, verse;
    const char *text;
    gboolean valid;

    if (!it)
        return -1;
    while (ks_module_iter_next(it, &book, &chapter, &verse, &text, &valid)) {
        cb(book, chapter, verse, text, valid, data);
        n++;
    }
    ks_module_iter_free(it);
    return n;
}

int ks_module_next_book(const KsModule *m, int book)
{
    for (int b = MAX(book, 0) + 1; b <= KS_MAX_BOOK; b++)
        if (m->nchapters[b])
            return b;
    return 0;
}

int ks_module_prev_book(const KsModule *m, int book)
{
    for (int b = MIN(book, KS_MAX_BOOK + 1) - 1; b >= 1; b--)
        if (m->nchapters[b])
            return b;
    return 0;
}
