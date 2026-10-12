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

/* Probes every *.bbl.mybible in dirs plus the explicit files, skipping
 * duplicates and anything that isn't a Bible module, sorted by label.
 * Either list may be NULL. */
GPtrArray *ks_module_scan(const char *const *dirs, const char *const *files);

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

/* Every verse, for --check. was_valid: the stored text was valid UTF-8. */
typedef void (*KsAllCb)(int book, int chapter, int verse, const char *text, gboolean was_valid,
                        void *data);
int ks_module_foreach(KsModule *m, KsAllCb cb, void *data);

/* Next/previous book present in the module, 0 if none. next(m, 0) = first. */
int ks_module_next_book(const KsModule *m, int book);
int ks_module_prev_book(const KsModule *m, int book);

#endif /* KS_MODULE_H */
