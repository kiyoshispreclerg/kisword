/* html.h - the HTML of modules as styled text runs: module descriptions
 * (Details.Comments) and the entries of dictionaries, commentaries, books
 * and journals.
 *
 * Module authors paste whole web pages there: tables, <FONT SIZE=7>,
 * <style> blocks, source line breaks. This keeps the text and what reads
 * as structure -- paragraphs, headings, lists, bold/italic, colors, links,
 * images -- and drops the rest. Unlike verse text, whitespace collapses as
 * in a browser. No GTK here. */
#ifndef KS_HTML_H
#define KS_HTML_H

#include <glib.h>

#include "gbf.h"

enum {
    KS_HTML_BOLD = 1 << 0,
    KS_HTML_ITALIC = 1 << 1,
    KS_HTML_UNDERLINE = 1 << 2,
    KS_HTML_SUP = 1 << 3,
    KS_HTML_SUB = 1 << 4,
    KS_HTML_HEADING = 1 << 5,
    KS_HTML_LINK = 1 << 6,
    KS_HTML_SMALL = 1 << 7,
    KS_HTML_BIG = 1 << 8,
    KS_HTML_STRIKE = 1 << 9,
};

enum { KS_HTML_ALIGN_NONE, KS_HTML_ALIGN_CENTER, KS_HTML_ALIGN_RIGHT };

/* One run of text with KS_HTML_* style; text is not NUL-terminated and
 * only valid during the call. Line breaks come as "\n" runs. */
typedef void (*KsHtmlCb)(const char *text, int len, unsigned style, void *data);

/* Text without any tag is taken as plain text: its line breaks are kept. */
void ks_html_parse(const char *src, KsHtmlCb cb, void *data);

/* A run with everything known about it. Line breaks come as runs of "\n"
 * (a new line) or "\n\n" (a new paragraph) with no attributes. image and
 * anchor runs have no text. Strings are only valid during the call. */
typedef struct {
    const char *text;
    int len;
    unsigned style;     /* KS_HTML_* */
    int heading;        /* 1-6 inside <hN>, else 0 */
    int indent;         /* depth of lists, blockquotes and left margins */
    int align;          /* KS_HTML_ALIGN_*: the paragraph's alignment */
    const char *href;   /* the innermost <a href>, or NULL */
    const char *color;  /* interned "#rrggbb", NULL = the theme's */
    const char *image;  /* <img src=...>: put that image here */
    const char *anchor; /* <a name=...>, id=...: a place links can point to */
} KsHtmlRun;

typedef void (*KsHtmlRunCb)(const KsHtmlRun *run, void *data);

/* Like ks_html_parse(), with classes resolved through css (the module's
 * CustomCSS plus the built-in color classes; NULL: built-ins only). */
void ks_html_render(const char *src, const KsStyleSheet *css, KsHtmlRunCb cb, void *data);

/* The runs joined, styles dropped (for tests and plain-text use). */
char *ks_html_plain(const char *src);

/* "&amp;", "&copy;", "&#233;", "&#xE9;" at p -> UTF-8 in out; returns the
 * bytes consumed, 0 if p is not a known entity. */
int ks_html_entity(const char *p, char out[8]);

#endif /* KS_HTML_H */
