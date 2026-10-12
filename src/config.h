/* config.h - ~/.config/kisword/kisword.conf (GKeyFile, group [kisword]). */
#ifndef KS_CONFIG_H
#define KS_CONFIG_H

#include <glib.h>

#define KS_DEFAULT_FONT "Serif 13"

typedef struct {
    char **modules_dirs;  /* extra module directories (besides the default one) */
    char **module_files;  /* modules opened with File > Open */
    char *module;         /* path of the last module used */
    char **parallel;      /* paths of the parallel Bibles shown with it */
    gboolean interleave;  /* parallels verse by verse in one view, not in columns */
    char **rules_off;     /* module options switched off: "file name|option name" */
    int book, chapter, verse;
    char *font;           /* Pango font description string */
    char *book_names;     /* "pt", "en", or NULL = from the locale */
    gboolean show_strong, show_notes, red_letters, paragraphs;
    int width, height;
} KsConfig;

KsConfig *ks_config_load(void);
void ks_config_save(const KsConfig *c);
void ks_config_free(KsConfig *c);

/* ~/.local/share/kisword/modules (created if missing). */
char *ks_config_modules_dir(void);

/* Module options (named VerseRules), on unless switched off. */
gboolean ks_config_rule_on(const KsConfig *c, const char *module_path, const char *option);
void ks_config_set_rule(KsConfig *c, const char *module_path, const char *option, gboolean on);

/* Appends path to c->module_files if it isn't there yet. */
void ks_config_add_module_file(KsConfig *c, const char *path);

#endif /* KS_CONFIG_H */
