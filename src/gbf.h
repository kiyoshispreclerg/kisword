/* gbf.h - parser for the tags inside MySword verse text.
 *
 * The text mixes GBF-style tags inherited from e-Sword/theWord (<FI>...<Fi>,
 * <WH430>, <RF>...<Rf>, <CM>) with a little HTML (<i>, <br>). Tags overlap
 * freely ("<FR>For<Fr><WG1063><FR> God<Fr>"), so this is a single pass with
 * on/off style flags, not a tree. No GTK here: the same parser feeds the
 * renderer, plain-text copy and search.
 *
 * HTML <span>/<font>/<a> (mostly produced by a module's VerseRules) nest
 * properly, so those use a small stack; their classes come from a
 * KsStyleSheet (style.c): a few built-in color names plus what can be
 * understood of the module's CustomCSS. */
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
    KS_ST_HIDDEN = 1 << 8,    /* class with display:none */
};

/* What a class (<span class='x'>) or a custom element (<gloss>) does. */
typedef struct {
    unsigned style;    /* KS_ST_* bits added */
    const char *color; /* interned "#rrggbb", NULL = unchanged */
    gboolean block;    /* display:block: line break before and after */
} KsClassStyle;

typedef struct KsStyleSheet KsStyleSheet;

/* Built-in classes (red, blue, green, ...) plus the subset of custom_css
 * (may be NULL) that maps to KsClassStyle: color, font-style,
 * font-weight, text-decoration, vertical-align and display. */
KsStyleSheet *ks_style_sheet_new(const char *custom_css);
void ks_style_sheet_free(KsStyleSheet *css);
/* NULL if unknown. Names are case-insensitive. css NULL: built-ins only.
 * element ("a", "span"; may be NULL) picks "a.c" over ".c". */
const KsClassStyle *ks_style_sheet_class(const KsStyleSheet *css, const char *element, const char *name, int len);
const KsClassStyle *ks_style_sheet_element(const KsStyleSheet *css, const char *name, int len);

/* A CSS color ("red", "#c22", "@darken(@red,5%)") as an interned
 * "#rrggbb"; NULL for black/white/unknown (left to the theme). */
const char *ks_style_color(const char *value, int len);

typedef struct {
    KsSpanKind kind;
    unsigned style;   /* KS_ST_* active at this span */
    int indent;       /* last <PI#>/<PF#>, until the next paragraph end */
    const char *text; /* NOT NUL-terminated; valid only during the callback */
    int len;
    const char *mark; /* KS_SPAN_NOTE: marker from <RF q=x>, or NULL */
    int mark_len;
    const char *color; /* interned "#rrggbb" from a class/<font>, or NULL */
} KsSpan;

typedef void (*KsSpanCb)(const KsSpan *span, void *data);

/* css may be NULL (built-in classes only). */
void ks_gbf_parse(const char *src, const KsStyleSheet *css, KsSpanCb cb, void *data);

/* Verse text without tags, notes, Strong's numbers, titles or hidden
 * classes, for copying and searching. Paragraph/line breaks become single
 * spaces. css may be NULL. */
char *ks_gbf_plain(const char *src, const KsStyleSheet *css);

/* Pango markup for HTML-ish text (notes, module comments): italic, bold,
 * underline, sup/sub, colors and line breaks. */
char *ks_gbf_markup(const char *src);

#endif /* KS_GBF_H */
