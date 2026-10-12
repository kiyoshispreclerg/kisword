/* studypane.h - the study panel beside the Bible: a tab each for
 * commentaries, dictionaries, books and journals.
 *
 * The commentary follows the verse shown or clicked in the Bible; a
 * Strong's number clicked there opens its definition; links inside the
 * modules go to other entries, other modules, or back to the Bible
 * through `go`. */
#ifndef KS_STUDYPANE_H
#define KS_STUDYPANE_H

#include <gtk/gtk.h>

#include "config.h"
#include "ref.h"
#include "res.h"

typedef struct KsStudy KsStudy;

typedef struct {
    void (*go)(const KsRef *ref, void *data);        /* a Bible reference was clicked */
    void (*status)(const char *message, void *data); /* something to tell the user */
} KsStudyCallbacks;

/* mods: every KsRes found (any kind), owned by the caller, who may add to
 * it later (see ks_study_added()). */
KsStudy *ks_study_new(KsConfig *cfg, GPtrArray *mods, const KsStudyCallbacks *cb, void *data);
GtkWidget *ks_study_widget(KsStudy *s);
void ks_study_set_font(KsStudy *s, const PangoFontDescription *font);

/* The Bible's place, for the commentary. */
void ks_study_set_verse(KsStudy *s, int book, int chapter, int verse);

/* Shows that Strong's number ("H430") from the dictionary that has it;
 * FALSE if none does. */
gboolean ks_study_strong(KsStudy *s, const char *number);
/* Looks up a word in the dictionary tab (and puts it in its entry). */
void ks_study_lookup(KsStudy *s, const char *word);
/* Puts that tab in front (and the cursor in the dictionary's entry). */
void ks_study_show_tab(KsStudy *s, KsResKind kind);
KsResKind ks_study_current_tab(KsStudy *s);

/* m was added to mods (File > Open): list it and show it. */
void ks_study_added(KsStudy *s, KsRes *m);

/* Modules shown and their places, into the config. */
void ks_study_save(KsStudy *s);

#endif /* KS_STUDYPANE_H */
