/* ref.h - parsing and formatting Bible references ("Jo 3:16-18"). */
#ifndef KS_REF_H
#define KS_REF_H

#include <glib.h>

typedef struct {
    int book;      /* 0: not given (keep the current book) */
    int chapter;   /* 1 when only a book was given */
    int verse;     /* 0: whole chapter */
    int verse_end; /* 0: single verse */
} KsRef;

/* Accepts "Jo 3:16", "jo 3.16", "João 3", "1Co 13:4-7", "1 Coríntios 13",
 * "gn1", "John 3:16", "Gênesis" and, without a book, "3:16". */
gboolean ks_ref_parse(const char *s, KsRef *out);

/* "Jo 3", "Jo 3:16", "Jo 3:16-18" with the current language's abbreviation
 * (verse/verse_end 0 = absent). */
char *ks_ref_format(int book, int chapter, int verse, int verse_end);

#endif /* KS_REF_H */
