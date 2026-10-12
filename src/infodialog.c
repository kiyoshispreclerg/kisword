/* infodialog.c - the module information dialog (see infodialog.h). */
#include "infodialog.h"

#include "html.h"
#include "ks_i18n.h"

#include <string.h>

typedef struct {
    GtkTextBuffer *b;
    GtkTextTag *tags[7]; /* by KS_HTML_* bit */
} InfoText;

static void info_html_cb(const char *text, int len, unsigned style, void *data)
{
    InfoText *it = data;
    GtkTextIter end, start;
    int off;

    gtk_text_buffer_get_end_iter(it->b, &end);
    off = gtk_text_iter_get_offset(&end);
    gtk_text_buffer_insert(it->b, &end, text, len);
    gtk_text_buffer_get_iter_at_offset(it->b, &start, off);
    for (int i = 0; i < 7; i++)
        if (style & (1u << i))
            gtk_text_buffer_apply_tag(it->b, it->tags[i], &start, &end);
}

static void info_insert(GtkTextBuffer *b, const char *text, const char *tag)
{
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(b, &end);
    if (tag)
        gtk_text_buffer_insert_with_tags_by_name(b, &end, text, -1, tag, NULL);
    else
        gtk_text_buffer_insert(b, &end, text, -1);
}

static void info_field(GtkTextBuffer *b, const char *label, const char *value)
{
    char *l;

    if (!value || !*value)
        return;
    l = g_strdup_printf("%s: ", label);
    info_insert(b, l, "label");
    info_insert(b, value, NULL);
    info_insert(b, "\n", NULL);
    g_free(l);
}

void ks_info_dialog(GtkWindow *parent, const char *title, GHashTable *d, const char *contents, const char *path)
{
    static const struct { const char *key, *label; } fields[] = {
        { "description", N_("Description") }, { "abbreviation", N_("Abbreviation") },
        { "language", N_("Language") },       { "version", N_("Version") },
        { "versiondate", N_("Version date") }, { "publishdate", N_("Published") },
        { "publisher", N_("Publisher") },     { "author", N_("Author") },
        { "creator", N_("Creator") },         { "source", N_("Source") },
    };
    GtkWidget *dlg, *sw, *tv;
    GtkTextBuffer *b;
    InfoText it;
    const char *comments = d ? g_hash_table_lookup(d, "comments") : NULL;
    const char *editorial = d ? g_hash_table_lookup(d, "editorialcomments") : NULL;

    dlg = gtk_dialog_new_with_buttons(_("About the module"), parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CLOSE,
                                      GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 520);
    tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(tv), 2);
    b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_text_buffer_create_tag(b, "title", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_X_LARGE,
                               "pixels-above-lines", 8, "pixels-below-lines", 8, NULL);
    gtk_text_buffer_create_tag(b, "label", "weight", PANGO_WEIGHT_BOLD, NULL);
    it.b = b;
    it.tags[0] = gtk_text_buffer_create_tag(b, NULL, "weight", PANGO_WEIGHT_BOLD, NULL);
    it.tags[1] = gtk_text_buffer_create_tag(b, NULL, "style", PANGO_STYLE_ITALIC, NULL);
    it.tags[2] = gtk_text_buffer_create_tag(b, NULL, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    it.tags[3] = gtk_text_buffer_create_tag(b, NULL, "rise", 5 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    it.tags[4] = gtk_text_buffer_create_tag(b, NULL, "rise", -3 * PANGO_SCALE, "scale", PANGO_SCALE_SMALL, NULL);
    it.tags[5] = gtk_text_buffer_create_tag(b, NULL, "scale", PANGO_SCALE_LARGE, NULL);
    it.tags[6] = gtk_text_buffer_create_tag(b, NULL, "foreground", "#3b78c4", NULL);

    info_insert(b, title, "title");
    info_insert(b, "\n", NULL);
    for (size_t i = 0; d && i < G_N_ELEMENTS(fields); i++) {
        const char *v = g_hash_table_lookup(d, fields[i].key);
        char *date = NULL;

        if (strcmp(fields[i].key, "description") == 0 && v && strcmp(v, title) == 0)
            continue;
        /* "2013-11-07 00:00:00": dates stored as timestamps. */
        if (v && g_str_has_suffix(v, " 00:00:00"))
            v = date = g_strndup(v, strlen(v) - 9);
        info_field(b, _(fields[i].label), v);
        g_free(date);
    }
    info_field(b, _("Contents"), contents);
    info_field(b, _("File"), path);

    if (comments) {
        info_insert(b, "\n", NULL);
        ks_html_parse(comments, info_html_cb, &it);
        info_insert(b, "\n", NULL);
    }
    if (editorial) {
        info_insert(b, "\n", NULL);
        info_insert(b, _("Editorial comments"), "label");
        info_insert(b, "\n", NULL);
        ks_html_parse(editorial, info_html_cb, &it);
    }

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_set_border_width(GTK_CONTAINER(sw), 6);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), sw, TRUE, TRUE, 0);
    gtk_widget_show_all(sw);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}
