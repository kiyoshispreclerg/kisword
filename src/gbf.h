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
    KS_SPAN_XREF,     /* <RX 43.1.1-5>: cross reference; text = "43.1.1-5" */
    KS_SPAN_BOX_OPEN, /* ks_gbf_parse_boxes() only: a box starts; flags in `box` */
    KS_SPAN_BOX_CLOSE,
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
    KS_ST_STRIKE = 1 << 9,    /* <s>, <strike>, <del> */
    KS_ST_SMALL = 1 << 10,    /* <small>, <font size=-1> */
    KS_ST_GLOSS = 1 << 11,    /* interlinear <E>/<T>: translation of the word */
    KS_ST_TRANSLIT = 1 << 12, /* interlinear <X>: transliteration or lexeme */
};

/* KsSpan.box */
enum {
    KS_BOX_CELL = 1 << 0,   /* inline-block: flows with the text */
    KS_BOX_BLOCK = 1 << 1,  /* a block of its own */
    KS_BOX_BORDER = 1 << 2,
    KS_BOX_CENTER = 1 << 3, /* lines centered (text-align) */
    KS_BOX_END = 1 << 4,    /* lines to the end side */
    KS_BOX_RTL = 1 << 5,    /* direction set by the CSS */
    KS_BOX_LTR = 1 << 6,
    KS_BOX_WORD = 1 << 7,   /* interlinear <Q> block: a word over its layers */
};

typedef enum {
    KS_DISPLAY_INLINE,
    KS_DISPLAY_BLOCK,
    KS_DISPLAY_CELL,  /* inline-block, table-cell */
    KS_DISPLAY_TABLE, /* a block, for our purposes */
} KsDisplay;

typedef enum { KS_ALIGN_NONE, KS_ALIGN_START, KS_ALIGN_CENTER, KS_ALIGN_END } KsAlign;

/* What a class (<span class='x'>) or a custom element (<gloss>) does. */
typedef struct {
    unsigned style;    /* KS_ST_* bits added */
    const char *color; /* interned "#rrggbb", NULL = unchanged */
    gboolean block;    /* display block or table: line break before and after */
    KsDisplay display;
    gboolean border;   /* a full border (border: / border-style:) */
    KsAlign align;     /* text-align */
    int dir;           /* direction: 1 rtl, -1 ltr, 0 not set */
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

/* Some class or element is an inline-block or a table: the module lays its
 * verses out in boxes (interlinears, linguistic annotations). */
gboolean ks_style_sheet_has_boxes(const KsStyleSheet *css);

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
    unsigned box;      /* KS_SPAN_BOX_OPEN: KS_BOX_* */
} KsSpan;

typedef void (*KsSpanCb)(const KsSpan *span, void *data);

/* css may be NULL (built-in classes only). */
void ks_gbf_parse(const char *src, const KsStyleSheet *css, KsSpanCb cb, void *data);

/* The same, but boxes are reported instead of approximated: <Q> blocks,
 * inline-block and table/block elements open and close KS_SPAN_BOX_*
 * spans, and inside an interlinear block each <X>, <E>, <T> starts a line
 * (a layer). Every box opened is closed. */
void ks_gbf_parse_boxes(const char *src, const KsStyleSheet *css, KsSpanCb cb, void *data);

/* Verse text without tags, notes, Strong's numbers, titles or hidden
 * classes, for copying and searching. Paragraph/line breaks become single
 * spaces. css may be NULL. */
char *ks_gbf_plain(const char *src, const KsStyleSheet *css);

/* Pango markup for HTML-ish text (notes, module comments): italic, bold,
 * underline, sup/sub, colors and line breaks. */
char *ks_gbf_markup(const char *src);

#endif /* KS_GBF_H */
