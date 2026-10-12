/* html.h - the HTML of module descriptions (Details.Comments), as styled
 * text runs.
 *
 * Module authors paste whole web pages there: tables, <FONT SIZE=7>,
 * <style> blocks, source line breaks. This keeps the text and what reads
 * as structure -- paragraphs, headings, list bullets, bold/italic, links --
 * and drops the rest. Unlike verse text, whitespace collapses as in a
 * browser. No GTK here. */
#ifndef KS_HTML_H
#define KS_HTML_H

#include <glib.h>

enum {
    KS_HTML_BOLD = 1 << 0,
    KS_HTML_ITALIC = 1 << 1,
    KS_HTML_UNDERLINE = 1 << 2,
    KS_HTML_SUP = 1 << 3,
    KS_HTML_SUB = 1 << 4,
    KS_HTML_HEADING = 1 << 5,
    KS_HTML_LINK = 1 << 6,
};

/* One run of text with KS_HTML_* style; text is not NUL-terminated and
 * only valid during the call. Line breaks come as "\n" runs. */
typedef void (*KsHtmlCb)(const char *text, int len, unsigned style, void *data);

/* Text without any tag is taken as plain text: its line breaks are kept. */
void ks_html_parse(const char *src, KsHtmlCb cb, void *data);

/* The runs joined, styles dropped (for tests and plain-text use). */
char *ks_html_plain(const char *src);

/* "&amp;", "&copy;", "&#233;", "&#xE9;" at p -> UTF-8 in out; returns the
 * bytes consumed, 0 if p is not a known entity. */
int ks_html_entity(const char *p, char out[8]);

#endif /* KS_HTML_H */
