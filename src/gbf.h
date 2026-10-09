/* gbf.h - parser for the tags inside MySword verse text.
 *
 * The text mixes GBF-style tags inherited from e-Sword/theWord (<FI>...<Fi>,
 * <WH430>, <RF>...<Rf>, <CM>) with a little HTML (<i>, <br>). Tags overlap
 * freely ("<FR>For<Fr><WG1063><FR> God<Fr>"), so this is a single pass with
 * on/off style flags, not a tree. No GTK here: the same parser feeds the
 * renderer, plain-text copy and (later) search. */
#ifndef KS_GBF_H
#define KS_GBF_H

#include <glib.h>

typedef enum {
    KS_SPAN_TEXT,     /* text with `style` */
    KS_SPAN_STRONG,   /* Strong's number without the W: "H430", "G2316" */
    KS_SPAN_MORPH,    /* <WT...> morphology code, without the WT */
    KS_SPAN_NOTE,     /* <RF>...<Rf> footnote; text = raw content (may hold HTML) */
    KS_SPAN_VERSENUM, /* <v>17:59</v>: verse number inside a merged verse */
    KS_SPAN_PARA,     /* <CM>, </p>: end of paragraph */
    KS_SPAN_LINE,     /* <CL>, <br>: line break */
    KS_SPAN_UNKNOWN,  /* tag not understood; text = the tag name */
} KsSpanKind;

enum {
    KS_ST_ITALIC = 1 << 0,    /* <FI>, <i> */
    KS_ST_RED = 1 << 1,       /* <FR>: words of Jesus */
    KS_ST_OTQUOTE = 1 << 2,   /* <FO>: OT quotation */
    KS_ST_UNDERLINE = 1 << 3, /* <FU>, <u> */
    KS_ST_BOLD = 1 << 4,      /* <FB>, <b> */
    KS_ST_SUP = 1 << 5,       /* <FS>, <sup> */
    KS_ST_SUB = 1 << 6,       /* <sub> */
    KS_ST_TITLE = 1 << 7,     /* <TS>...<Ts> */
};

typedef struct {
    KsSpanKind kind;
    unsigned style;   /* KS_ST_* active at this span */
    int indent;       /* last <PI#>/<PF#>, until the next paragraph end */
    const char *text; /* NOT NUL-terminated; valid only during the callback */
    int len;
    const char *mark; /* KS_SPAN_NOTE: marker from <RF q=x>, or NULL */
    int mark_len;
} KsSpan;

typedef void (*KsSpanCb)(const KsSpan *span, void *data);

void ks_gbf_parse(const char *src, KsSpanCb cb, void *data);

/* Verse text without tags, notes, Strong's numbers or titles, for copying
 * and searching. Paragraph/line breaks become single spaces. */
char *ks_gbf_plain(const char *src);

#endif /* KS_GBF_H */
