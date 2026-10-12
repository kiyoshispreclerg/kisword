/* res.c - dictionaries, commentaries, books and journals (see res.h). */
#include "res.h"

#include "ks_i18n.h"
#include "module.h"

#include <stdlib.h>
#include <string.h>

struct KsResTable {
    char *name;
    char *key;            /* the column looked up: "word", "clauseid"... */
    sqlite3_stmt *lookup; /* key, data WHERE key = ?1 */
};

static const char *const suffixes[KS_RES_N] = { ".cmt.mybible", ".dct.mybible", ".bok.mybible", ".jor.mybible" };

gboolean ks_res_kind_of(const char *path, KsResKind *kind)
{
    char *lower = g_ascii_strdown(path, -1);
    gboolean found = FALSE;

    for (int k = 0; k < KS_RES_N && !found; k++) {
        if (g_str_has_suffix(lower, suffixes[k])) {
            *kind = (KsResKind)k;
            found = TRUE;
        }
    }
    g_free(lower);
    return found;
}

const char *ks_res_suffix(KsResKind kind)
{
    return suffixes[kind];
}

/* Valid UTF-8 copy of a text column, "" for NULL. */
static char *col_utf8(sqlite3_stmt *st, int i)
{
    const char *t = (const char *)sqlite3_column_text(st, i);
    int n = sqlite3_column_bytes(st, i);

    if (!t)
        return g_strdup("");
    return g_utf8_validate(t, n, NULL) ? g_strndup(t, n) : g_utf8_make_valid(t, n);
}

static char *quote_ident(const char *name)
{
    GString *g = g_string_new("\"");

    for (const char *p = name; *p; p++) {
        if (*p == '"')
            g_string_append_c(g, '"');
        g_string_append_c(g, *p);
    }
    g_string_append_c(g, '"');
    return g_string_free(g, FALSE);
}

/* The columns of a table, lower-case. */
static GPtrArray *table_columns(sqlite3 *db, const char *table)
{
    GPtrArray *cols = g_ptr_array_new_with_free_func(g_free);
    char *q = quote_ident(table), *sql = g_strdup_printf("PRAGMA table_info(%s)", q);
    sqlite3_stmt *st;

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW)
            g_ptr_array_add(cols, g_ascii_strdown((const char *)sqlite3_column_text(st, 1), -1));
        sqlite3_finalize(st);
    }
    g_free(sql);
    g_free(q);
    return cols;
}

static gboolean has_column(GPtrArray *cols, const char *name)
{
    for (guint i = 0; i < cols->len; i++)
        if (strcmp(g_ptr_array_index(cols, i), name) == 0)
            return TRUE;
    return FALSE;
}

static gboolean has_table(sqlite3 *db, const char *name)
{
    sqlite3_stmt *st;
    gboolean found = FALSE;

    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 COLLATE NOCASE",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        found = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    return found;
}

static gboolean is_blob(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st;
    gboolean blob = FALSE;

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW)
            blob = sqlite3_column_type(st, 0) == SQLITE_BLOB;
        sqlite3_finalize(st);
    }
    return blob;
}

/* Dictionary tables: every table with a data column and a key column
 * ("word" when there is one), the word list's first. */
static GPtrArray *dict_tables(sqlite3 *db)
{
    GPtrArray *tables = g_ptr_array_new();
    sqlite3_stmt *st;

    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY "
                               "name <> 'dictionary' COLLATE NOCASE, rowid",
                           -1, &st, NULL) != SQLITE_OK)
        return tables;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        GPtrArray *cols;
        const char *key = NULL;

        if (!name || g_ascii_strcasecmp(name, "details") == 0 || g_str_has_prefix(name, "sqlite_"))
            continue;
        cols = table_columns(db, name);
        if (has_column(cols, "data")) {
            if (has_column(cols, "word"))
                key = "word";
            for (guint i = 0; !key && i < cols->len; i++) {
                const char *c = g_ptr_array_index(cols, i);

                if (strcmp(c, "data") != 0 && strcmp(c, "rowid") != 0)
                    key = c;
            }
        }
        if (key) {
            KsResTable *t = g_new0(KsResTable, 1);

            t->name = g_strdup(name);
            t->key = g_strdup(key);
            /* The word list's table goes first. */
            if (strcmp(key, "word") == 0 && tables->len &&
                strcmp(((KsResTable *)g_ptr_array_index(tables, 0))->key, "word") != 0)
                g_ptr_array_insert(tables, 0, t);
            else
                g_ptr_array_add(tables, t);
        }
        g_ptr_array_free(cols, TRUE);
    }
    sqlite3_finalize(st);
    return tables;
}

static void table_free(gpointer p)
{
    KsResTable *t = p;

    if (t->lookup)
        sqlite3_finalize(t->lookup);
    g_free(t->name);
    g_free(t->key);
    g_free(t);
}

static void read_details(KsRes *r, sqlite3 *db, int *encryption, char **description)
{
    sqlite3_stmt *st;

    if (sqlite3_prepare_v2(db, "SELECT * FROM Details LIMIT 1", -1, &st, NULL) != SQLITE_OK)
        return;
    if (sqlite3_step(st) == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(st); i++) {
            if (ks_col_is(st, i, "Title"))
                r->title = ks_col_text(st, i);
            else if (ks_col_is(st, i, "Description"))
                *description = ks_col_text(st, i);
            else if (ks_col_is(st, i, "Abbreviation"))
                r->abbrev = ks_col_text(st, i);
            else if (ks_col_is(st, i, "CustomCSS"))
                r->custom_css = ks_col_text(st, i);
            else if (ks_col_is(st, i, "RightToLeft"))
                r->rtl = ks_col_bool(st, i);
            else if (ks_col_is(st, i, "Strong"))
                r->strong = ks_col_bool(st, i);
            else if (ks_col_is(st, i, "Morphology"))
                r->morph = ks_col_bool(st, i);
            else if (ks_col_is(st, i, "encryption"))
                *encryption = sqlite3_column_int(st, i);
        }
    }
    sqlite3_finalize(st);
}

static char *name_from_path(const char *path)
{
    char *base = g_path_get_basename(path);
    char *lower = g_ascii_strdown(base, -1);

    for (int k = 0; k < KS_RES_N; k++)
        if (g_str_has_suffix(lower, suffixes[k]))
            base[strlen(base) - strlen(suffixes[k])] = '\0';
    g_free(lower);
    return base;
}

KsRes *ks_res_probe(const char *path, GError **err)
{
    static const char *const what[KS_RES_N] = { N_("%s is not a MySword commentary (%s)"),
                                                N_("%s is not a MySword dictionary (%s)"),
                                                N_("%s is not a MySword book (%s)"),
                                                N_("%s is not a MySword journal (%s)") };
    KsResKind kind;
    sqlite3 *db = NULL;
    KsRes *r;
    gboolean ok, blob = FALSE;
    int encryption = 0;
    char *description = NULL;

    if (!ks_res_kind_of(path, &kind)) {
        g_set_error(err, KS_MODULE_ERROR, 0, _("%s is not a MySword module"), path);
        return NULL;
    }
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        g_set_error(err, KS_MODULE_ERROR, 0, "%s: %s", path, db ? sqlite3_errmsg(db) : "out of memory");
        sqlite3_close(db);
        return NULL;
    }
    /* The first real read: anything that isn't SQLite fails here. */
    if (sqlite3_exec(db, "SELECT 1 FROM sqlite_master LIMIT 1", NULL, NULL, NULL) != SQLITE_OK) {
        g_set_error(err, KS_MODULE_ERROR, 0, _(what[kind]), path, sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }
    switch (kind) {
    case KS_RES_COMMENTARY:
        ok = has_table(db, "commentary");
        blob = ok && is_blob(db, "SELECT data FROM commentary LIMIT 1");
        break;
    case KS_RES_DICT: {
        GPtrArray *t = dict_tables(db);

        ok = t->len > 0;
        if (ok) {
            char *q = quote_ident(((KsResTable *)g_ptr_array_index(t, 0))->name);
            char *sql = g_strdup_printf("SELECT data FROM %s LIMIT 1", q);

            blob = is_blob(db, sql);
            g_free(sql);
            g_free(q);
        }
        g_ptr_array_set_free_func(t, table_free);
        g_ptr_array_free(t, TRUE);
        break;
    }
    default:
        ok = has_table(db, "journal");
        blob = ok && is_blob(db, "SELECT content FROM journal LIMIT 1");
        break;
    }
    if (!ok) {
        g_set_error(err, KS_MODULE_ERROR, 0, _(what[kind]), path, _("no table of entries"));
        sqlite3_close(db);
        return NULL;
    }

    r = g_new0(KsRes, 1);
    r->kind = kind;
    r->path = g_strdup(path);
    r->real = realpath(path, NULL);
    if (!r->real)
        r->real = g_strdup(path);
    r->supported = TRUE;
    read_details(r, db, &encryption, &description);
    sqlite3_close(db);

    if (!r->title)
        r->title = description ? g_strdup(description) : r->abbrev ? g_strdup(r->abbrev) : name_from_path(path);
    g_free(description);
    ks_one_line(r->title);
    if (r->abbrev)
        ks_one_line(r->abbrev);
    if (encryption) {
        r->supported = FALSE;
        r->unsupported_why = g_strdup(_("This is an encrypted MySword module. Encrypted modules are not supported."));
    } else if (blob) {
        r->supported = FALSE;
        r->unsupported_why = g_strdup(_("The text in this module is binary (probably encrypted or compressed)."));
    }
    return r;
}

void ks_res_free(KsRes *r)
{
    if (!r)
        return;
    ks_res_close(r);
    g_free(r->path);
    free(r->real);
    g_free(r->title);
    g_free(r->abbrev);
    g_free(r->custom_css);
    g_free(r->unsupported_why);
    g_free(r);
}

const char *ks_res_label(const KsRes *r)
{
    return r->abbrev ? r->abbrev : r->title;
}

gboolean ks_res_named(const KsRes *r, const char *name)
{
    char *base;
    gboolean eq;

    if (r->abbrev && g_ascii_strcasecmp(r->abbrev, name) == 0)
        return TRUE;
    base = name_from_path(r->path);
    eq = g_ascii_strcasecmp(base, name) == 0;
    g_free(base);
    return eq;
}

GHashTable *ks_res_details(const KsRes *r)
{
    GHashTable *t;
    sqlite3 *db = NULL;
    sqlite3_stmt *st;

    if (sqlite3_open_v2(r->path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "SELECT * FROM Details LIMIT 1", -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }
    t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (sqlite3_step(st) == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(st); i++) {
            char *v = sqlite3_column_type(st, i) == SQLITE_BLOB ? NULL : ks_col_text(st, i);

            if (v)
                g_hash_table_insert(t, g_ascii_strdown(sqlite3_column_name(st, i), -1), v);
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return t;
}

/* ---- scanning ---- */

typedef struct {
    GPtrArray *out;
    GHashTable *seen;
} Scan;

static void scan_cb(const char *path, void *data)
{
    Scan *s = data;
    KsResKind kind;
    char *real;
    KsRes *r;

    if (!ks_res_kind_of(path, &kind) || !(real = realpath(path, NULL)))
        return;
    if (g_hash_table_contains(s->seen, real)) {
        free(real);
        return;
    }
    g_hash_table_add(s->seen, g_strdup(real));
    free(real);
    r = ks_res_probe(path, NULL);
    if (r)
        g_ptr_array_add(s->out, r);
}

static gint by_label(gconstpointer a, gconstpointer b)
{
    const KsRes *ra = *(KsRes *const *)a, *rb = *(KsRes *const *)b;

    return g_utf8_collate(ks_res_label(ra), ks_res_label(rb));
}

GPtrArray *ks_res_scan(const char *const *dirs, const char *const *files)
{
    Scan s = { g_ptr_array_new_with_free_func((GDestroyNotify)ks_res_free),
               g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL) };

    ks_scan_files(dirs, scan_cb, &s);
    for (int i = 0; files && files[i]; i++)
        scan_cb(files[i], &s);
    g_ptr_array_sort(s.out, by_label);
    g_hash_table_destroy(s.seen);
    return s.out;
}

/* ---- opening ---- */

gboolean ks_res_open(KsRes *r, GError **err)
{
    if (r->db)
        return TRUE;
    if (!r->supported) {
        g_set_error_literal(err, KS_MODULE_ERROR, 0, r->unsupported_why);
        return FALSE;
    }
    if (sqlite3_open_v2(r->path, &r->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        g_set_error(err, KS_MODULE_ERROR, 0, "%s: %s", r->path, r->db ? sqlite3_errmsg(r->db) : "out of memory");
        ks_res_close(r);
        return FALSE;
    }
    r->css = ks_style_sheet_new(r->custom_css);
    r->images = has_table(r->db, "data");
    if (r->kind == KS_RES_DICT) {
        r->tables = dict_tables(r->db);
        if (r->tables->len) {
            GPtrArray *cols = table_columns(r->db, ((KsResTable *)g_ptr_array_index(r->tables, 0))->name);

            r->ordered = has_column(cols, "relativeorder");
            g_ptr_array_free(cols, TRUE);
        }
        for (guint i = 0; i < r->tables->len; i++) {
            KsResTable *t = g_ptr_array_index(r->tables, i);
            char *qt = quote_ident(t->name), *qk = quote_ident(t->key);
            char *sql = g_strdup_printf("SELECT %s, data FROM %s WHERE %s = ?1 LIMIT 1", qk, qt, qk);

            if (sqlite3_prepare_v2(r->db, sql, -1, &t->lookup, NULL) != SQLITE_OK)
                t->lookup = NULL;
            g_free(sql);
            g_free(qk);
            g_free(qt);
        }
    } else if (r->kind != KS_RES_COMMENTARY) {
        GPtrArray *cols = table_columns(r->db, "journal");

        r->ordered = has_column(cols, "relativeorder");
        r->hidden = has_column(cols, "hidden");
        g_ptr_array_free(cols, TRUE);
    }
    return TRUE;
}

void ks_res_close(KsRes *r)
{
    if (r->tables) {
        g_ptr_array_set_free_func(r->tables, table_free);
        g_ptr_array_free(r->tables, TRUE);
        r->tables = NULL;
    }
    if (r->db)
        sqlite3_close(r->db);
    r->db = NULL;
    g_clear_pointer(&r->css, ks_style_sheet_free);
}

/* ---- dictionaries ---- */

char *ks_res_lookup(KsRes *r, const char *word, char **key)
{
    char *data = NULL;

    if (key)
        *key = NULL;
    if (!r->db || !r->tables || !word || !*word)
        return NULL;
    for (guint i = 0; i < r->tables->len && !data; i++) {
        KsResTable *t = g_ptr_array_index(r->tables, i);

        if (!t->lookup)
            continue;
        sqlite3_reset(t->lookup);
        sqlite3_bind_text(t->lookup, 1, word, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(t->lookup) == SQLITE_ROW) {
            data = col_utf8(t->lookup, 1);
            if (key)
                *key = col_utf8(t->lookup, 0);
        }
        sqlite3_reset(t->lookup);
    }
    return data;
}

static void words_from(sqlite3_stmt *st, GPtrArray *out)
{
    while (sqlite3_step(st) == SQLITE_ROW)
        g_ptr_array_add(out, col_utf8(st, 0));
    sqlite3_finalize(st);
}

GPtrArray *ks_res_words(KsRes *r, const char *from, gboolean after, int limit)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    KsResTable *t;
    char *qt, *qk, *sql;
    sqlite3_stmt *st;

    if (!r->db || !r->tables || !r->tables->len)
        return out;
    t = g_ptr_array_index(r->tables, 0);
    qt = quote_ident(t->name);
    qk = quote_ident(t->key);
    if (!from)
        from = "";

    if (r->ordered) {
        /* Where the word falls alphabetically, then onwards in the
         * module's order: "H43" in a Strong dictionary lists H43, H44... */
        sqlite3_int64 order = G_MININT64;

        if (*from || after) {
            sql = g_strdup_printf(after ? "SELECT relativeorder FROM %s WHERE %s = ?1"
                                        : "SELECT relativeorder FROM %s WHERE %s >= ?1 ORDER BY %s LIMIT 1",
                                  qt, qk, qk);
            if (sqlite3_prepare_v2(r->db, sql, -1, &st, NULL) == SQLITE_OK) {
                sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
                order = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : G_MAXINT64;
                sqlite3_finalize(st);
            }
            g_free(sql);
        }
        if (order != G_MAXINT64) {
            sql = g_strdup_printf("SELECT %s FROM %s WHERE relativeorder %s ?1 ORDER BY relativeorder, %s LIMIT ?2", qk,
                                  qt, after ? ">" : ">=", qk);
            if (sqlite3_prepare_v2(r->db, sql, -1, &st, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(st, 1, order);
                sqlite3_bind_int(st, 2, limit);
                words_from(st, out);
            }
            g_free(sql);
        }
    } else {
        sql = g_strdup_printf("SELECT %s FROM %s WHERE %s %s ?1 ORDER BY %s LIMIT ?2", qk, qt, qk, after ? ">" : ">=",
                              qk);
        if (sqlite3_prepare_v2(r->db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, limit);
            words_from(st, out);
        }
        g_free(sql);
    }
    g_free(qk);
    g_free(qt);
    return out;
}

/* ---- commentaries ---- */

int ks_res_comments(KsRes *r, int book, int chapter, KsCommentCb cb, void *data)
{
    sqlite3_stmt *st;
    int n = 0;

    if (!r->db || sqlite3_prepare_v2(r->db, "SELECT fromverse, toverse, data FROM commentary WHERE book = ?1 AND "
                                            "chapter = ?2 ORDER BY fromverse, toverse, rowid",
                                     -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(st, 1, book);
    sqlite3_bind_int(st, 2, chapter);
    while (sqlite3_step(st) == SQLITE_ROW) {
        char *html = col_utf8(st, 2);
        int from = sqlite3_column_int(st, 0), to = sqlite3_column_int(st, 1);

        cb(from, MAX(to, from), html, data);
        g_free(html);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

/* ---- books and journals ---- */

static void entry_free(KsEntry *e)
{
    g_free(e->id);
    g_free(e->title);
    g_free(e);
}

GPtrArray *ks_res_entries(KsRes *r)
{
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)entry_free);
    sqlite3_stmt *st;
    char *sql;

    if (!r->db)
        return out;
    /* Books in their order; journals without one by date, as a diary. */
    sql = g_strdup_printf("SELECT id, title FROM journal %s ORDER BY %s, title COLLATE NOCASE",
                          r->hidden ? "WHERE coalesce(hidden, 0) = 0" : "",
                          r->ordered ? "relativeorder" : r->kind == KS_RES_JOURNAL ? "date" : "rowid");
    if (sqlite3_prepare_v2(r->db, sql, -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            KsEntry *e = g_new0(KsEntry, 1);

            e->id = col_utf8(st, 0);
            e->title = col_utf8(st, 1);
            if (!*e->title) {
                g_free(e->title);
                e->title = g_strdup(e->id);
            }
            g_ptr_array_add(out, e);
        }
        sqlite3_finalize(st);
    }
    g_free(sql);
    return out;
}

char *ks_res_entry(KsRes *r, const char *id_or_title, char **id)
{
    static const char *const sql[] = { "SELECT id, content FROM journal WHERE id = ?1",
                                       "SELECT id, content FROM journal WHERE title = ?1 COLLATE NOCASE",
                                       "SELECT id, content FROM journal WHERE id = ?1 COLLATE NOCASE" };
    char *content = NULL;

    if (id)
        *id = NULL;
    for (size_t i = 0; r->db && i < G_N_ELEMENTS(sql) && !content; i++) {
        sqlite3_stmt *st;

        if (sqlite3_prepare_v2(r->db, sql[i], -1, &st, NULL) != SQLITE_OK)
            continue;
        sqlite3_bind_text(st, 1, id_or_title, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            content = col_utf8(st, 1);
            if (id)
                *id = col_utf8(st, 0);
        }
        sqlite3_finalize(st);
    }
    return content;
}

/* ---- everything ---- */

static int foreach_sql(KsRes *r, const char *sql, KsResAllCb cb, void *data)
{
    sqlite3_stmt *st;
    int n = 0;

    if (sqlite3_prepare_v2(r->db, sql, -1, &st, NULL) != SQLITE_OK)
        return 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *t = (const char *)sqlite3_column_text(st, 1);
        int len = sqlite3_column_bytes(st, 1);
        gboolean valid = !t || g_utf8_validate(t, len, NULL);
        char *where = col_utf8(st, 0), *html = col_utf8(st, 1);

        cb(where, html, valid, data);
        g_free(html);
        g_free(where);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

int ks_res_foreach(KsRes *r, KsResAllCb cb, void *data)
{
    int n = 0;

    if (!r->db)
        return -1;
    if (r->kind == KS_RES_DICT) {
        for (guint i = 0; r->tables && i < r->tables->len; i++) {
            KsResTable *t = g_ptr_array_index(r->tables, i);
            char *qt = quote_ident(t->name), *qk = quote_ident(t->key);
            char *sql = g_strdup_printf("SELECT %s, data FROM %s", qk, qt);

            n += foreach_sql(r, sql, cb, data);
            g_free(sql);
            g_free(qk);
            g_free(qt);
        }
    } else if (r->kind == KS_RES_COMMENTARY) {
        n = foreach_sql(r, "SELECT book || '.' || chapter || '.' || fromverse, data FROM commentary "
                           "ORDER BY book, chapter, fromverse", cb, data);
    } else {
        n = foreach_sql(r, "SELECT id, content FROM journal", cb, data);
    }
    return n;
}

/* ---- images ---- */

GBytes *ks_res_image(KsRes *r, const char *src)
{
    sqlite3_stmt *st;
    GBytes *bytes = NULL;
    char *base;

    if (!src)
        return NULL;
    /* Inline: "data:image/gif;base64,R0lGOD..." (AHLB). */
    if (g_str_has_prefix(src, "data:")) {
        const char *b64 = strstr(src, ";base64,");
        gsize len;
        guchar *raw;

        if (!b64)
            return NULL;
        raw = g_base64_decode(b64 + 8, &len);
        return len ? g_bytes_new_take(raw, len) : (g_free(raw), NULL);
    }
    if (!r->db || !r->images)
        return NULL;
    if (sqlite3_prepare_v2(r->db, "SELECT content FROM data WHERE id = ?1 OR filename = ?1 OR id = ?2 "
                                  "OR filename = ?2 LIMIT 1",
                           -1, &st, NULL) != SQLITE_OK)
        return NULL;
    base = g_path_get_basename(src);
    sqlite3_bind_text(st, 1, src, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, base, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_bytes(st, 0) > 0)
        bytes = g_bytes_new(sqlite3_column_blob(st, 0), sqlite3_column_bytes(st, 0));
    sqlite3_finalize(st);
    g_free(base);
    return bytes;
}
