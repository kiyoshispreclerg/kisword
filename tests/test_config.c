/* test_config.c - kisword.conf backup: kept when saving, used when loading
 * after a power cut left the file empty. */
#include "config.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void save_module(const char *module)
{
    KsConfig *c = ks_config_load();

    g_free(c->module);
    c->module = g_strdup(module);
    ks_config_save(c);
    ks_config_free(c);
}

static char *loaded_module(void)
{
    KsConfig *c = ks_config_load();
    char *m = g_strdup(c->module);

    ks_config_free(c);
    return m;
}

int main(void)
{
    char *tmp = g_dir_make_tmp("ks-conf-XXXXXX", NULL);
    char *conf, *bak, *m;

    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    conf = g_build_filename(tmp, "kisword", "kisword.conf", NULL);
    bak = g_strconcat(conf, ".backup", NULL);

    save_module("A");
    CHECK(!g_file_test(bak, G_FILE_TEST_EXISTS));
    save_module("B");
    CHECK(g_file_test(bak, G_FILE_TEST_EXISTS));
    m = loaded_module();
    CHECK(g_strcmp0(m, "B") == 0);
    g_free(m);

    /* The power cut: the config is left empty. The backup (A, the previous
     * save) is used, and saving again does not copy the empty file over it. */
    g_file_set_contents(conf, "", 0, NULL);
    m = loaded_module();
    CHECK(g_strcmp0(m, "A") == 0);
    g_free(m);
    {
        KsConfig *c = ks_config_load();

        ks_config_save(c);
        ks_config_free(c);
    }
    m = loaded_module();
    CHECK(g_strcmp0(m, "A") == 0);
    g_free(m);
    {
        char *text = NULL;

        g_file_get_contents(bak, &text, NULL, NULL);
        CHECK(text && text[0] != '\0');
        g_free(text);
    }

    g_remove(conf);
    g_remove(bak);
    {
        char *dir = g_path_get_dirname(conf);
        g_rmdir(dir);
        g_free(dir);
    }
    g_rmdir(tmp);
    g_free(conf);
    g_free(bak);
    g_free(tmp);
    if (failures)
        return 1;
    printf("test_config: all passed\n");
    return 0;
}
