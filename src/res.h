/* res.h - MySword modules other than Bibles: dictionaries (*.dct.mybible),
 * commentaries (*.cmt.mybible), books (*.bok.mybible) and journals
 * (*.jor.mybible). Like Bibles, they are SQLite databases with a Details
 * table whose columns vary; their text is HTML.
 *
 *   dictionary(word, data[, relativeorder])  -- plus, in some (OpenGNTd,
 *       OpenHBd), more tables of (key, data) that links point into
 *   commentary(book, chapter, fromverse, toverse, data)
 *   journal(id, title, content[, relativeorder, hidden])  -- books too
 *   data(id, filename, content)  -- images the HTML shows (<img src>)
 *
 * Files are only ever opened read-only. */
#ifndef KS_RES_H
#define KS_RES_H

#include <glib.h>
#include <sqlite3.h>

#include "gbf.h"

typedef enum {
    KS_RES_COMMENTARY,
    KS_RES_DICT,
    KS_RES_BOOK,
    KS_RES_JOURNAL,
    KS_RES_N,
} KsResKind;

typedef struct KsResTable KsResTable;

typedef struct {
    KsResKind kind;
    char *path, *real;
    char *title;     /* never NULL */
    char *abbrev;    /* may be NULL */
    char *custom_css;
    gboolean rtl;
    gboolean strong; /* a dictionary of Strong's numbers */
    gboolean morph;  /* a dictionary of morphology codes */

    gboolean supported;
    char *unsupported_why;

    /* Set by ks_res_open(). */
    sqlite3 *db;
    KsStyleSheet *css;
    GPtrArray *tables;  /* dictionary: KsResTable *, the word list's first */
    gboolean ordered;   /* relativeorder column (dictionary or journal) */
    gboolean hidden;    /* journal.hidden column */
    gboolean images;    /* a data table */
} KsRes;

/* The kind a file name says (".dct.mybible" ...); FALSE if none. */
gboolean ks_res_kind_of(const char *path, KsResKind *kind);
/* "*.dct.mybible" ... */
const char *ks_res_suffix(KsResKind kind);

/* Reads Details and checks the tables, then closes the file. NULL (with
 * err) if it is not such a module at all; unsupported ones (encrypted)
 * come back with supported = FALSE. */
KsRes *ks_res_probe(const char *path, GError **err);
void ks_res_free(KsRes *r);

/* Every module of any kind in dirs (and their usual subfolders, see
 * ks_scan_files()) plus files, without duplicates, sorted by label. */
GPtrArray *ks_res_scan(const char *const *dirs, const char *const *files);

const char *ks_res_label(const KsRes *r);
/* Abbreviation or file name (without the suffix) equal to name, ignoring case. */
gboolean ks_res_named(const KsRes *r, const char *name);
/* Lower-case column name -> value, as ks_module_details(). */
GHashTable *ks_res_details(const KsRes *r);

gboolean ks_res_open(KsRes *r, GError **err);
void ks_res_close(KsRes *r);

/* ---- dictionaries ---- */

/* The entry for word (case is ignored where the module says so), in any
 * of the module's tables; NULL if absent. *key (may be NULL) gets the word
 * as the module spells it. */
char *ks_res_lookup(KsRes *r, const char *word, char **key);

/* Up to limit words of the word list in the module's order (relativeorder
 * if it has one, else alphabetical), starting at the first one >= from
 * ("" = the beginning), or, with after, just past the word from. */
GPtrArray *ks_res_words(KsRes *r, const char *from, gboolean after, int limit);

/* ---- commentaries ---- */

typedef void (*KsCommentCb)(int from_verse, int to_verse, const char *html, void *data);
/* The comments on one chapter, in verse order; returns how many. */
int ks_res_comments(KsRes *r, int book, int chapter, KsCommentCb cb, void *data);

/* ---- books and journals ---- */

typedef struct {
    char *id;
    char *title;
} KsEntry;

/* Every visible entry in the module's order: KsEntry *, freed with the array. */
GPtrArray *ks_res_entries(KsRes *r);
/* The content of the entry with that id, or else that title; NULL if none.
 * *id (may be NULL) gets its id. */
char *ks_res_entry(KsRes *r, const char *id_or_title, char **id);

/* ---- everything ---- */

/* Every entry's HTML, for --check: where says which ("H430", "1.1.1",
 * "Tobit 1"); valid: the stored text was valid UTF-8. Returns how many. */
typedef void (*KsResAllCb)(const char *where, const char *html, gboolean valid, void *data);
int ks_res_foreach(KsRes *r, KsResAllCb cb, void *data);

/* ---- images ---- */

/* The bytes of an image the HTML refers to (<img src='1.jpg'>), NULL if
 * the module doesn't have it. */
GBytes *ks_res_image(KsRes *r, const char *src);

#endif /* KS_RES_H */
