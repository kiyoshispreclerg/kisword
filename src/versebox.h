/* versebox.h - one verse laid out in boxes (see boxes.h), as a widget the
 * chapter view anchors in its text.
 *
 * Words of an interlinear become columns that flow and wrap like text,
 * right to left for Hebrew; annotation boxes nest with their borders.
 * Strong's numbers and note markers are clickable; what a click does is
 * up to the caller. */
#ifndef KS_VERSEBOX_H
#define KS_VERSEBOX_H

#include <gtk/gtk.h>

#include "boxes.h"

/* Theme colors, owned by the view and shared by its verse boxes. */
typedef struct {
    GdkColor text, base, dim, hl;
} KsPalette;

typedef struct {
    int verse;
    const char *label;  /* module label before the verse number (interleaved), or NULL */
    gboolean rtl;
    gboolean show_strong, show_notes, red_letters;
    gboolean highlight;
    const PangoFontDescription *font; /* copied */
    const KsPalette *palette;         /* not copied: must outlive the widget */
} KsVerseBoxOptions;

/* it: the Strong's number or note clicked (its kind and text; a note's
 * content in extra). */
typedef void (*KsVerseBoxLinkCb)(GtkWidget *w, const KsItem *it, int x_root, int y_root, void *data);

/* Takes ownership of box. */
GtkWidget *ks_verse_box_new(KsBox *box, const KsVerseBoxOptions *opt, KsVerseBoxLinkCb cb, void *data);

/* Lays the verse out for that width and asks for the height it needs. */
void ks_verse_box_set_width(GtkWidget *w, int width);

#endif /* KS_VERSEBOX_H */
