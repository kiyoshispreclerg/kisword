/* search.h - full-text and Strong's number search in one module.
 *
 * Text search runs on an index built on first use: each verse as plain
 * text, folded (case, accents and other combining marks, punctuation), in
 * one block of memory, about the size of the Bible's text. It is built a
 * step at a time so the window can stay responsive while a heavy module
 * (several seconds for the big interlinears) is read. Strong's numbers are
 * searched in the module itself instead, since the plain text drops them.
 * No GTK here. */
#ifndef KS_SEARCH_H
#define KS_SEARCH_H

#include "module.h"

typedef enum {
    KS_SEARCH_ALL_WORDS, /* every word, anywhere in the verse */
    KS_SEARCH_PHRASE,    /* the words together, in order */
    KS_SEARCH_ANY_WORD,
} KsSearchMode;

typedef struct {
    KsSearchMode mode;
    gboolean whole_words; /* "amor" does not match "amores" */
    int book_from, book_to; /* inclusive; 0, 0 = everything */
} KsSearchOptions;

typedef struct {
    guint8 book;
    guint16 chapter, verse;
} KsHit;

/* " no principio criou deus " for "No princípio, criou Deus": lower case,
 * no combining marks, every run of non-alphanumerics one space, with a
 * space at both ends. */
char *ks_search_fold(const char *s);

/* "H430", "g2316", "H0430" -> 'H'/'G' and the number; FALSE if s is not a
 * Strong's number. */
gboolean ks_search_parse_strong(const char *s, char *testament, int *number);

typedef struct KsSearchIndex KsSearchIndex;

/* m must be open and stay open while the index is being built. */
KsSearchIndex *ks_search_index_new(KsModule *m);
void ks_search_index_free(KsSearchIndex *idx);
/* Indexes up to max_verses more; TRUE once complete. */
gboolean ks_search_index_step(KsSearchIndex *idx, int max_verses);
/* 0.0 .. 1.0 */
double ks_search_index_progress(const KsSearchIndex *idx);
gboolean ks_search_index_done(const KsSearchIndex *idx);

/* Hits in Bible order (an empty query matches nothing). The index must be
 * complete. */
GArray *ks_search_text(const KsSearchIndex *idx, const char *query, const KsSearchOptions *opt);

/* Verses carrying that Strong's number (a "<WH430>" tag, or a number link
 * made by VerseRules). Needs no index. */
GArray *ks_search_strong(KsModule *m, char testament, int number, const KsSearchOptions *opt);

#endif /* KS_SEARCH_H */
