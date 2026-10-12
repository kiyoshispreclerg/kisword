/* htmlview.h - module HTML (dictionary entries, comments, book pages) in a
 * GtkTextView: the runs of html.c as text tags, links that call back,
 * images that open full size when clicked. No WebKit. */
#ifndef KS_HTMLVIEW_H
#define KS_HTMLVIEW_H

#include <gtk/gtk.h>

#include "gbf.h"

typedef struct KsHtmlView KsHtmlView;

/* A link was clicked; x_root/y_root: where, for a popup. */
typedef void (*KsHtmlLinkCb)(KsHtmlView *hv, const char *href, int x_root, int y_root, void *data);
/* The bytes of an image the page shows, NULL if there are none. */
typedef GBytes *(*KsHtmlImageCb)(KsHtmlView *hv, const char *src, void *data);

/* Freed when its widget is destroyed. */
KsHtmlView *ks_html_view_new(void);
GtkWidget *ks_html_view_widget(KsHtmlView *hv); /* the scrolled window to pack */
GtkWidget *ks_html_view_text(KsHtmlView *hv);   /* the GtkTextView inside it */
void ks_html_view_set_callbacks(KsHtmlView *hv, KsHtmlLinkCb link, KsHtmlImageCb image, void *data);
void ks_html_view_set_font(KsHtmlView *hv, const PangoFontDescription *font);

/* A page is built off screen between begin and end, then shown. rtl: the
 * module's text runs right to left. */
void ks_html_view_begin(KsHtmlView *hv, gboolean rtl);
/* A heading of kisword's own ("Gn 1:3", the word looked up); mark (may be
 * NULL) names the place, to scroll to it. */
void ks_html_view_heading(KsHtmlView *hv, const char *text, const char *mark);
void ks_html_view_html(KsHtmlView *hv, const char *html, const KsStyleSheet *css);
/* A line of kisword's own, dimmed: "No comment on this chapter." */
void ks_html_view_note(KsHtmlView *hv, const char *text);
/* Shows the page, scrolled to that mark (NULL: the top). */
void ks_html_view_end(KsHtmlView *hv, const char *scroll_mark);

/* begin + note + end. */
void ks_html_view_message(KsHtmlView *hv, const char *text);

/* Scrolls to a mark given to ks_html_view_heading() or an anchor of the
 * HTML (<a name>, id); FALSE if there is none. */
gboolean ks_html_view_scroll_to(KsHtmlView *hv, const char *mark);
gboolean ks_html_view_scroll_to_anchor(KsHtmlView *hv, const char *anchor);

/* A small window with text at the pointer; any click or key closes it. */
void ks_html_view_popup(KsHtmlView *hv, const char *text, int x_root, int y_root);

/* An image in a window of its own, at most the screen's size. */
void ks_image_window(GtkWidget *parent, GBytes *bytes, const char *title);

#endif /* KS_HTMLVIEW_H */
