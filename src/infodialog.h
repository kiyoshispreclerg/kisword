/* infodialog.h - "About the module": what a module's Details says, with
 * its description (Comments, EditorialComments) as rich text. For Bibles
 * and the other kinds alike. */
#ifndef KS_INFODIALOG_H
#define KS_INFODIALOG_H

#include <gtk/gtk.h>

/* details: lower-case column -> value (may be NULL); contents: a line on
 * what the module has ("66 books, Strong's numbers"), may be NULL or "". */
void ks_info_dialog(GtkWindow *parent, const char *title, GHashTable *details, const char *contents,
                    const char *path);

#endif /* KS_INFODIALOG_H */
