/* render.h - the chapter view: verse text rendered into a GtkTextView.
 *
 * No WebKit: each tag of the verse text becomes a GtkTextTag. Strong's
 * numbers and footnote markers are clickable and open a small popup;
 * Ctrl+C copies whole verses as plain text with their reference. */
#ifndef KS_RENDER_H
#define KS_RENDER_H

#include <gtk/gtk.h>

#include "module.h"

typedef struct {
    gboolean show_strong;
    gboolean show_notes;
    gboolean red_letters;
    gboolean paragraphs; /* paragraph layout when the chapter has <CM> marks */
    gboolean stacked;    /* interlinears and annotations in boxes (versebox.c) */
} KsViewOptions;

typedef struct KsView KsView;

/* Modules one view can interleave. */
#define KS_VIEW_MAX_MODS 8

KsView *ks_view_new(const KsViewOptions *opt);
GtkWidget *ks_view_widget(KsView *v); /* the scrolled window to pack */
GtkWidget *ks_view_text(KsView *v);   /* the GtkTextView inside it */

/* Re-renders the current chapter with new options, keeping the verse at
 * the top of the view. */
void ks_view_set_options(KsView *v, const KsViewOptions *opt);
void ks_view_set_font(KsView *v, const PangoFontDescription *font);

/* Shows one chapter. With several modules they are interleaved: each verse
 * once per module, labelled, mods[0] first. scroll_verse: verse to put at
 * the top (0 = start); hl_from..hl_to: verses to highlight (0 = none;
 * hl_to 0 = just hl_from). */
void ks_view_show(KsView *v, KsModule *const *mods, int nmods, int book, int chapter, int scroll_verse,
                  int hl_from, int hl_to);

/* Replaces the content with a plain message (no module shown). */
void ks_view_message(KsView *v, const char *text);

/* Verse at the top of the visible area, 0 if nothing is shown. */
int ks_view_top_verse(KsView *v);

/* For views side by side: the verse at the top and how far into it the
 * view is scrolled (0..1), and scrolling another view to the same place. */
gboolean ks_view_get_anchor(KsView *v, int *verse, double *frac);
void ks_view_set_anchor(KsView *v, int verse, double frac);
GtkAdjustment *ks_view_vadjustment(KsView *v);

/* Destroys the widget too. */
void ks_view_free(KsView *v);

#endif /* KS_RENDER_H */
