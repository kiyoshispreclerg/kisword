/* boxes.h - a verse as a tree of boxes, for modules laid out with them.
 *
 * Interlinears put each word in a column (the word over its Strong's
 * number, morphology, transliteration and gloss); linguistic annotations
 * nest bordered boxes. MySword gets that from HTML and CSS; here the verse
 * parser reports the boxes (ks_gbf_parse_boxes) and this builds the tree
 * that versebox.c lays out and draws. No GTK here. */
#ifndef KS_BOXES_H
#define KS_BOXES_H

#include "gbf.h"

typedef enum {
    KS_ITEM_RUN,   /* text, Strong's number, morphology, note marker... */
    KS_ITEM_SPACE, /* where a line may wrap */
    KS_ITEM_BREAK, /* a new line (in a word box: the next layer) */
    KS_ITEM_BOX,
} KsItemType;

typedef struct KsBox KsBox;

typedef struct {
    KsItemType type;
    KsSpanKind kind;   /* RUN: KS_SPAN_TEXT, _STRONG, _MORPH, _NOTE, _VERSENUM, _XREF */
    unsigned style;    /* KS_ST_* */
    const char *color; /* interned, or NULL */
    char *text;        /* RUN: what is shown (a note's marker) */
    char *extra;       /* NOTE: its content */
    KsBox *box;        /* BOX */
} KsItem;

struct KsBox {
    unsigned flags; /* KS_BOX_*; the root is a KS_BOX_BLOCK */
    GArray *items;  /* KsItem */
};

/* Whether a verse of this module is worth laying out in boxes: it has an
 * interlinear block, or the module's CSS has inline-blocks or tables. */
gboolean ks_boxes_wanted(const char *verse, const KsStyleSheet *css);

/* The tree for a verse; NULL if it has no box at all. In a word box the
 * Strong's numbers and morphology of the word itself become layers of
 * their own under it, as MySword shows them. */
KsBox *ks_boxes_build(const char *verse, const KsStyleSheet *css);
void ks_box_free(KsBox *b);

/* Debug form, for tests: [cell ...], {word ...}, <block ...>, | for a
 * break, _ for a space, S:H430 / M:... / N:... for runs that aren't text. */
char *ks_box_dump(const KsBox *b);

#endif /* KS_BOXES_H */
