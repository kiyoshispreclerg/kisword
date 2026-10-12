/* config.c - kisword.conf load/save (see config.h). */
#include "config.h"

#include <glib/gstdio.h>
#include <string.h>

#define GROUP "kisword"

static char *config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "kisword", "kisword.conf", NULL);
}

char *ks_config_modules_dir(void)
{
    char *dir = g_build_filename(g_get_user_data_dir(), "kisword", "modules", NULL);

    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static void get_bool(GKeyFile *kf, const char *key, gboolean *out)
{
    GError *e = NULL;
    gboolean v = g_key_file_get_boolean(kf, GROUP, key, &e);

    if (e)
        g_error_free(e);
    else
        *out = v;
}

static void get_int(GKeyFile *kf, const char *key, int *out)
{
    GError *e = NULL;
    int v = g_key_file_get_integer(kf, GROUP, key, &e);

    if (e)
        g_error_free(e);
    else
        *out = v;
}

static void get_str(GKeyFile *kf, const char *key, char **out)
{
    char *v = g_key_file_get_string(kf, GROUP, key, NULL);

    if (v && *v) {
        g_free(*out);
        *out = v;
    } else {
        g_free(v);
    }
}

static void get_list(GKeyFile *kf, const char *key, char ***out)
{
    char **v = g_key_file_get_string_list(kf, GROUP, key, NULL, NULL);

    if (v) {
        g_strfreev(*out);
        *out = v;
    }
}

KsConfig *ks_config_load(void)
{
    KsConfig *c = g_new0(KsConfig, 1);
    GKeyFile *kf = g_key_file_new();
    char *path = config_path();

    c->modules_dirs = g_new0(char *, 1);
    c->module_files = g_new0(char *, 1);
    c->parallel = g_new0(char *, 1);
    c->rules_off = g_new0(char *, 1);
    c->book = 1;
    c->chapter = 1;
    c->font = g_strdup(KS_DEFAULT_FONT);
    c->show_strong = TRUE;
    c->show_notes = TRUE;
    c->red_letters = TRUE;
    c->width = 760;
    c->height = 680;

    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        get_list(kf, "modules_dirs", &c->modules_dirs);
        get_list(kf, "module_files", &c->module_files);
        get_str(kf, "module", &c->module);
        get_list(kf, "parallel", &c->parallel);
        get_bool(kf, "interleave", &c->interleave);
        get_list(kf, "rules_off", &c->rules_off);
        get_int(kf, "book", &c->book);
        get_int(kf, "chapter", &c->chapter);
        get_int(kf, "verse", &c->verse);
        get_str(kf, "font", &c->font);
        get_str(kf, "book_names", &c->book_names);
        get_bool(kf, "show_strong", &c->show_strong);
        get_bool(kf, "show_notes", &c->show_notes);
        get_bool(kf, "red_letters", &c->red_letters);
        get_bool(kf, "paragraphs", &c->paragraphs);
        get_int(kf, "width", &c->width);
        get_int(kf, "height", &c->height);
    }
    g_key_file_free(kf);
    g_free(path);
    return c;
}

void ks_config_save(const KsConfig *c)
{
    GKeyFile *kf = g_key_file_new();
    char *path = config_path();
    char *dir = g_path_get_dirname(path);
    char *data;
    gsize len;

    g_key_file_set_string_list(kf, GROUP, "modules_dirs", (const char *const *)c->modules_dirs,
                               g_strv_length(c->modules_dirs));
    g_key_file_set_string_list(kf, GROUP, "module_files", (const char *const *)c->module_files,
                               g_strv_length(c->module_files));
    g_key_file_set_string(kf, GROUP, "module", c->module ? c->module : "");
    g_key_file_set_string_list(kf, GROUP, "parallel", (const char *const *)c->parallel,
                               g_strv_length(c->parallel));
    g_key_file_set_boolean(kf, GROUP, "interleave", c->interleave);
    g_key_file_set_string_list(kf, GROUP, "rules_off", (const char *const *)c->rules_off,
                               g_strv_length(c->rules_off));
    g_key_file_set_integer(kf, GROUP, "book", c->book);
    g_key_file_set_integer(kf, GROUP, "chapter", c->chapter);
    g_key_file_set_integer(kf, GROUP, "verse", c->verse);
    g_key_file_set_string(kf, GROUP, "font", c->font ? c->font : KS_DEFAULT_FONT);
    g_key_file_set_string(kf, GROUP, "book_names", c->book_names ? c->book_names : "");
    g_key_file_set_boolean(kf, GROUP, "show_strong", c->show_strong);
    g_key_file_set_boolean(kf, GROUP, "show_notes", c->show_notes);
    g_key_file_set_boolean(kf, GROUP, "red_letters", c->red_letters);
    g_key_file_set_boolean(kf, GROUP, "paragraphs", c->paragraphs);
    g_key_file_set_integer(kf, GROUP, "width", c->width);
    g_key_file_set_integer(kf, GROUP, "height", c->height);

    data = g_key_file_to_data(kf, &len, NULL);
    g_mkdir_with_parents(dir, 0755);
    g_file_set_contents(path, data, len, NULL);
    g_free(data);
    g_free(dir);
    g_free(path);
    g_key_file_free(kf);
}

void ks_config_free(KsConfig *c)
{
    if (!c)
        return;
    g_strfreev(c->modules_dirs);
    g_strfreev(c->module_files);
    g_strfreev(c->parallel);
    g_strfreev(c->rules_off);
    g_free(c->module);
    g_free(c->font);
    g_free(c->book_names);
    g_free(c);
}

/* "kjv.bbl.mybible|Show Hebrew vowels": by file name, so the setting
 * follows the module to another folder. */
static char *rule_key(const char *module_path, const char *option)
{
    char *base = g_path_get_basename(module_path);
    char *key = g_strdup_printf("%s|%s", base, option);

    g_free(base);
    return key;
}

gboolean ks_config_rule_on(const KsConfig *c, const char *module_path, const char *option)
{
    char *key = rule_key(module_path, option);
    gboolean on = !g_strv_contains((const char *const *)c->rules_off, key);

    g_free(key);
    return on;
}

void ks_config_set_rule(KsConfig *c, const char *module_path, const char *option, gboolean on)
{
    char *key = rule_key(module_path, option);
    GPtrArray *a = g_ptr_array_new();

    for (int i = 0; c->rules_off[i]; i++)
        if (strcmp(c->rules_off[i], key) != 0)
            g_ptr_array_add(a, g_strdup(c->rules_off[i]));
    if (!on)
        g_ptr_array_add(a, g_strdup(key));
    g_ptr_array_add(a, NULL);
    g_strfreev(c->rules_off);
    c->rules_off = (char **)g_ptr_array_free(a, FALSE);
    g_free(key);
}

void ks_config_add_module_file(KsConfig *c, const char *path)
{
    guint n = g_strv_length(c->module_files);

    for (guint i = 0; i < n; i++)
        if (strcmp(c->module_files[i], path) == 0)
            return;
    c->module_files = g_renew(char *, c->module_files, n + 2);
    c->module_files[n] = g_strdup(path);
    c->module_files[n + 1] = NULL;
}
