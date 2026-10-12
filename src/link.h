/* link.h - the links inside MySword modules (<a href='...'>).
 *
 * A link is a type letter and a target, optionally after a '#':
 *
 *   b  Bible:       "#b43.3.16", "#b1.8.3-1.8.4", "bGen 1:1", "#bProv 1:20-23"
 *   s  Strong:      "sH430", "#sG2316"
 *   d  dictionary:  "dH1" (the same dictionary), "d-HL H1" (by abbreviation),
 *                   "d-*word" (any), "d[About:Abbr]#BM1643" (with an anchor)
 *   m  morphology:  "mprep", "m/lἈαρών" (a lemma)
 *   c  commentary:  "c-TSK 1.1.1"
 *   j  entry of the same book or journal: "#j Table of Contents"
 *   k  book:        "k-HebrewGrammar Adjective"
 *   p  picture:     "p3.jpg?mod=k-Timeline" (an image of a book, full size)
 *   r  a note shown on its own: "rEtymology", "rHewlett, Bible difficulties"
 *
 * plus ordinary web addresses and MyBible's "B:290 23:5" (Isaiah: MyBible
 * numbers books 10, 20, 30...). No GTK here. */
#ifndef KS_LINK_H
#define KS_LINK_H

#include <glib.h>

#include "ref.h"

typedef enum {
    KS_LINK_NONE,
    KS_LINK_BIBLE,
    KS_LINK_STRONG,
    KS_LINK_DICT,
    KS_LINK_MORPH,
    KS_LINK_COMMENTARY,
    KS_LINK_ENTRY,   /* j: book or journal entry, by title */
    KS_LINK_BOOK,    /* k: book entry, by title */
    KS_LINK_PICTURE, /* p: key = image name, module = the book */
    KS_LINK_POPUP,   /* r: key = the text to show */
    KS_LINK_URL,
} KsLinkKind;

typedef struct {
    KsLinkKind kind;
    char *module; /* abbreviation given with '-', NULL: the same module or any */
    char *key;    /* word, entry title, normalized Strong's number, URL */
    char *anchor; /* after '#' in a dictionary key, or NULL */
    KsRef ref;    /* KS_LINK_BIBLE, KS_LINK_COMMENTARY */
} KsLink;

/* FALSE (kind NONE) if href is not a link kisword understands. Free the
 * strings with ks_link_clear() either way. */
gboolean ks_link_parse(const char *href, KsLink *out);
void ks_link_clear(KsLink *l);

/* "H0430", "h430", "WH430" -> "H430"; a letter after the digits stays
 * ("H1254a"). NULL if s is not a Strong's number. */
char *ks_strong_normalize(const char *s);

/* "43.3.16", "1.8.3-1.8.4", "Gen 1:1", "John 3:16 KJV", "I Timothy 6:11"
 * -> a reference. */
gboolean ks_link_parse_ref(const char *s, KsRef *out);

#endif /* KS_LINK_H */
