/* module.h - MySword Bible modules (*.bbl.mybible). */
#ifndef KS_MODULE_H
#define KS_MODULE_H

#include <glib.h>
#include <sqlite3.h>

#include "gbf.h"
#include "rules.h"

#define KS_MAX_BOOK 99

#define KS_MODULE_ERROR (ks_module_error_quark())
GQuark ks_module_error_quark(void);

typedef struct {
    char *path;            /* as found or given */
    char *real;            /* realpath(), for de-duplication */
    char *title;           /* never NULL: Title -> Description -> Abbreviation -> file name */
    char *abbrev;          /* may be NULL */
    char *language;        /* may be NULL */
    char *font;            /* Details.Font, NULL for "DEFAULT"/absent */
    char *verse_rules;     /* Details.VerseRules, NULL if absent */
    char *custom_css;      /* Details.CustomCSS, NULL if absent */
    gboolean rtl, ot, nt, strong;
    int rtl_ot, rtl_nt;    /* RightToLeftOT/NT (-1: absent), for modules mixing both */

    gboolean supported;    /* FALSE: encrypted/compressed/binary text */
    char *unsupported_why; /* translated reason when !supported */

    /* Set by ks_module_open(). */
    sqlite3 *db;
    sqlite3_stmt *st_chapter;
    KsRules *rules;        /* compiled verse_rules, NULL if none */
    int rules_invalid;     /* rules that did not compile */
    KsStyleSheet *css;     /* built-in classes + custom_css */
    guint16 nchapters[KS_MAX_BOOK + 1]; /* per book; 0 = book absent */
} KsModule;

/* Reads Details and checks the text format, then closes the file. NULL (with
 * err) only if the file is not a readable MySword Bible at all; unsupported
 * modules come back with supported = FALSE. */
KsModule *ks_module_probe(const char *path, GError **err);
void ks_module_free(KsModule *m);

/* Probes every *.bbl.mybible in dirs (see ks_scan_files()) plus the
 * explicit files, skipping duplicates and anything that isn't a Bible
 * module, sorted by label. Either list may be NULL. */
GPtrArray *ks_module_scan(const char *const *dirs, const char *const *files);

/* Calls cb for every *.mybible file in dirs, and in their subfolders named
 * as MySword names them (bibles, commentaries, dictionaries, books,
 * journals; also mysword/ and those inside it). */
typedef void (*KsFileCb)(const char *path, void *data);
void ks_scan_files(const char *const *dirs, KsFileCb cb, void *data);

/* Shared with res.c: Details columns by name, as text (valid UTF-8,
 * stripped, NULL if empty) or as a boolean ("1", "true", 1). */
gboolean ks_col_is(sqlite3_stmt *st, int i, const char *name);
char *ks_col_text(sqlite3_stmt *st, int i);
gboolean ks_col_bool(sqlite3_stmt *st, int i);
/* Keeps the first line of s, with whitespace runs as one space. */
char *ks_one_line(char *s);

/* Every non-empty text column of Details, lower-case name -> value (valid
 * UTF-8), read from the file now; works on unsupported modules too. NULL
 * if the file can't be read. */
GHashTable *ks_module_details(const KsModule *m);

/* Whether that book reads right to left. */
gboolean ks_module_rtl(const KsModule *m, int book);

/* Abbreviation, or the title if there is none. */
const char *ks_module_label(const KsModule *m);

gboolean ks_module_open(KsModule *m, GError **err);
void ks_module_close(KsModule *m);

/* Verses of one chapter in order; text is valid UTF-8 (fixed up if the
 * module's wasn't), has the VerseRules applied, and is only valid during
 * the callback. Returns the number of verses, -1 if the module is not
 * open. */
typedef void (*KsVerseCb)(int verse, const char *text, void *data);
int ks_module_chapter(KsModule *m, int book, int chapter, KsVerseCb cb, void *data);

/* One verse (VerseRules applied), NULL if absent. */
char *ks_module_verse(KsModule *m, int book, int chapter, int verse);

/* Walks verses in Bible order, a step at a time (search indexing runs from
 * an idle handler). like: SQL LIKE pattern on the stored text to only get
 * the verses that may match, or NULL for all. The module must stay open
 * while the iterator lives. */
typedef struct KsVerseIter KsVerseIter;
KsVerseIter *ks_module_iter_new(KsModule *m, const char *like);
/* FALSE at the end. text (rules applied) is valid until the next call;
 * was_valid (may be NULL): the stored text was valid UTF-8. */
gboolean ks_module_iter_next(KsVerseIter *it, int *book, int *chapter, int *verse, const char **text,
                             gboolean *was_valid);
void ks_module_iter_free(KsVerseIter *it);

/* Number of verses in the module (for progress), -1 if not open. */
int ks_module_verse_count(KsModule *m);

/* Every verse, for --check. was_valid: the stored text was valid UTF-8. */
typedef void (*KsAllCb)(int book, int chapter, int verse, const char *text, gboolean was_valid,
                        void *data);
int ks_module_foreach(KsModule *m, KsAllCb cb, void *data);

/* Next/previous book present in the module, 0 if none. next(m, 0) = first. */
int ks_module_next_book(const KsModule *m, int book);
int ks_module_prev_book(const KsModule *m, int book);

#endif /* KS_MODULE_H */
