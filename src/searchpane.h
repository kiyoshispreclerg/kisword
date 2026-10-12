/* searchpane.h - the search side panel (Ctrl+F): query, options, results.
 *
 * Owns the module's search index, built in idle steps the first time a
 * text search runs and dropped when the module changes. Selecting a
 * result calls `go`; the window decides what that means. */
#ifndef KS_SEARCHPANE_H
#define KS_SEARCHPANE_H

#include <gtk/gtk.h>

#include "module.h"

typedef struct KsSearchPane KsSearchPane;

typedef void (*KsSearchGoCb)(int book, int chapter, int verse, void *data);

KsSearchPane *ks_search_pane_new(KsSearchGoCb go, void *data);
GtkWidget *ks_search_pane_widget(KsSearchPane *p);

/* The module to search (NULL: none). Drops the index and the results; if
 * the panel is showing a search, it runs again on the new module. */
void ks_search_pane_set_module(KsSearchPane *p, KsModule *m);
/* The book shown, for the "current book" range. */
void ks_search_pane_set_book(KsSearchPane *p, int book);

/* Shows the panel and puts the cursor in the query. */
void ks_search_pane_focus(KsSearchPane *p);
gboolean ks_search_pane_visible(KsSearchPane *p);

#endif /* KS_SEARCHPANE_H */
