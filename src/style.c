/* style.c - classes for <span class='x'> and custom elements (see gbf.h).
 *
 * MySword shows verses in a WebView, so modules style what their VerseRules
 * produce with CSS (Details.CustomCSS), using MySword's own color functions
 * ("@darken(@red,5%)"). Only the part that maps to a text tag is read:
 * color, italic, bold, underline, super/subscript, display:block/none.
 * Anything else (borders, :hover, fonts, the odd <script>) is skipped. */
#include "gbf.h"

#include <string.h>

struct KsStyleSheet {
    GHashTable *classes;  /* lower-case "class" or "element.class" -> KsClassStyle */
    GHashTable *elements; /* lower-case element name -> KsClassStyle */
};

/* Close to MySword's palette, readable on light and dark backgrounds. */
static const struct { const char *name, *hex; } colors[] = {
    { "red", "#cc2222" },    { "darkred", "#a01818" },  { "maroon", "#a01818" },
    { "blue", "#2a5db0" },   { "navy", "#2a4a90" },     { "darkblue", "#2a4a90" },
    { "green", "#2e7d32" },  { "darkgreen", "#2e6b2e" }, { "olive", "#6b6b1e" },
    { "brown", "#8b5a2b" },  { "gray", "#7a7a7a" },     { "grey", "#7a7a7a" },
    { "silver", "#9a9a9a" }, { "orange", "#d2691e" },   { "purple", "#7b3fa0" },
    { "violet", "#7b3fa0" }, { "magenta", "#b0308f" },  { "fuchsia", "#b0308f" },
    { "pink", "#c2185b" },   { "teal", "#00838f" },     { "bluegreen", "#00838f" },
    { "cyan", "#00838f" },   { "aqua", "#00838f" },     { "gold", "#b8860b" },
    { "yellow", "#b8860b" }, { "yellowgreen", "#6b8e23" }, { "lime", "#6b8e23" },
};

/* Plain HTML elements are never restyled from CustomCSS: "a {color:...}"
 * would recolor every link the VerseRules make. */
static const char *const html_elements[] = {
    "a", "b", "body", "br", "div", "em", "font", "h1", "h2", "h3", "h4", "html", "i", "img",
    "li", "ol", "p", "small", "span", "strong", "sub", "sup", "table", "td", "tr", "u", "ul",
};

const char *ks_style_color(const char *value, int len)
{
    char buf[32];
    int n = 0;

    if (len < 0)
        len = strlen(value);
    while (len > 0 && g_ascii_isspace(*value)) {
        value++;
        len--;
    }
    /* MySword's functions: @darken(@red,5%), @adjust(@blue,-30,...) -> the
     * base color is close enough. */
    while (len > 0 && *value == '@') {
        const char *paren = memchr(value, '(', len), *at;

        if (!paren) {
            value++;
            len--;
            break;
        }
        at = memchr(paren, '@', len - (paren - value));
        if (!at)
            return NULL;
        len -= at - value;
        value = at;
    }
    while (n < len && n < (int)sizeof buf - 1 && (g_ascii_isalnum(value[n]) || value[n] == '#'))
        n++;
    memcpy(buf, value, n);
    buf[n] = '\0';

    if (buf[0] == '#') {
        if (n == 4) /* #c22 -> #cc2222 */
            g_snprintf(buf, sizeof buf, "#%c%c%c%c%c%c", value[1], value[1], value[2], value[2], value[3], value[3]);
        else if (n != 7)
            return NULL;
        for (int i = 1; i < 7; i++)
            if (!g_ascii_isxdigit(buf[i]))
                return NULL;
        for (int i = 1; i < 7; i++)
            buf[i] = g_ascii_tolower(buf[i]);
        if (strcmp(buf, "#000000") == 0 || strcmp(buf, "#ffffff") == 0)
            return NULL;
        return g_intern_string(buf);
    }
    for (size_t i = 0; i < G_N_ELEMENTS(colors); i++)
        if (g_ascii_strcasecmp(buf, colors[i].name) == 0)
            return g_intern_static_string(colors[i].hex);
    return NULL;
}

static KsClassStyle *entry(GHashTable *t, const char *name, int len)
{
    char *key = g_ascii_strdown(name, len);
    KsClassStyle *cs = g_hash_table_lookup(t, key);

    if (cs) {
        g_free(key);
        return cs;
    }
    cs = g_new0(KsClassStyle, 1);
    g_hash_table_insert(t, key, cs);
    return cs;
}

static gboolean prop_is(const char *p, int n, const char *name)
{
    return n == (int)strlen(name) && g_ascii_strncasecmp(p, name, n) == 0;
}

static gboolean value_has(const char *v, int n, const char *word)
{
    char *s = g_ascii_strdown(v, n);
    gboolean found = strstr(s, word) != NULL;

    g_free(s);
    return found;
}

/* "color: red; font-style: italic" into cs. */
static void apply_decls(KsClassStyle *cs, const char *d, const char *end)
{
    while (d < end) {
        const char *semi = memchr(d, ';', end - d), *stop = semi ? semi : end;
        const char *colon = memchr(d, ':', stop - d);

        if (colon) {
            const char *p = d, *pe = colon, *v = colon + 1, *ve = stop;

            while (p < pe && g_ascii_isspace(*p))
                p++;
            while (pe > p && g_ascii_isspace(pe[-1]))
                pe--;
            while (v < ve && g_ascii_isspace(*v))
                v++;
            while (ve > v && g_ascii_isspace(ve[-1]))
                ve--;
            if (prop_is(p, pe - p, "color")) {
                const char *c = ks_style_color(v, ve - v);

                if (c)
                    cs->color = c;
            } else if (prop_is(p, pe - p, "font-style") && value_has(v, ve - v, "italic")) {
                cs->style |= KS_ST_ITALIC;
            } else if (prop_is(p, pe - p, "font-weight") && (value_has(v, ve - v, "bold") || g_ascii_strtoll(v, NULL, 10) >= 600)) {
                cs->style |= KS_ST_BOLD;
            } else if (prop_is(p, pe - p, "text-decoration") && value_has(v, ve - v, "underline")) {
                cs->style |= KS_ST_UNDERLINE;
            } else if (prop_is(p, pe - p, "vertical-align")) {
                if (value_has(v, ve - v, "super"))
                    cs->style |= KS_ST_SUP;
                else if (value_has(v, ve - v, "sub"))
                    cs->style |= KS_ST_SUB;
            } else if (prop_is(p, pe - p, "display")) {
                if (value_has(v, ve - v, "none"))
                    cs->style |= KS_ST_HIDDEN;
                else if (value_has(v, ve - v, "block") && !value_has(v, ve - v, "inline"))
                    cs->block = TRUE;
            }
        }
        d = stop + 1;
    }
}

static gboolean is_html_element(const char *s, int n)
{
    for (size_t i = 0; i < G_N_ELEMENTS(html_elements); i++)
        if ((int)strlen(html_elements[i]) == n && g_ascii_strncasecmp(s, html_elements[i], n) == 0)
            return TRUE;
    return FALSE;
}

/* One selector of a list ("span.c", ".red", "gloss", "a.nd:link"). Only
 * the last compound counts; pseudo-classes other than :link make it a
 * state (hover...) that never applies here. */
static void apply_selector(KsStyleSheet *css, const char *s, const char *end, const char *d, const char *dend)
{
    const char *dot, *colon, *p;

    while (end > s && g_ascii_isspace(end[-1]))
        end--;
    for (p = end; p > s && !g_ascii_isspace(p[-1]) && p[-1] != '>' && p[-1] != '+' && p[-1] != '~'; p--)
        ;
    s = p;
    if (s >= end)
        return;
    colon = memchr(s, ':', end - s);
    if (colon) {
        if (end - colon != 5 || g_ascii_strncasecmp(colon, ":link", 5) != 0)
            return;
        end = colon;
    }
    for (p = s; p < end; p++)
        if (!(g_ascii_isalnum(*p) || *p == '.' || *p == '-' || *p == '_'))
            return; /* [attr], #id, *: not supported */
    dot = memchr(s, '.', end - s);
    if (dot) {
        const char *name = dot + 1;

        /* "a.c" only for <a class='c'>; ".c" for any element. */
        if (name < end && !memchr(name, '.', end - name))
            apply_decls(entry(css->classes, dot == s ? name : s, end - (dot == s ? name : s)), d, dend);
    } else if (!is_html_element(s, end - s)) {
        apply_decls(entry(css->elements, s, end - s), d, dend);
    }
}

static void parse_css(KsStyleSheet *css, const char *src)
{
    char *text = g_strdup(src), *s, *d;
    const char *p;

    /* Comments out. */
    for (s = d = text; *s;) {
        if (s[0] == '/' && s[1] == '*') {
            char *e = strstr(s + 2, "*/");

            if (!e)
                break;
            s = e + 2;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';

    p = text;
    while (*p) {
        const char *open = strchr(p, '{'), *close, *sel;

        if (!open)
            break;
        sel = p;
        while (sel < open && (g_ascii_isspace(*sel) || *sel == '}' || *sel == ';'))
            sel++;
        if (*sel == '@') { /* @media, @font-face: skip the whole block */
            int depth = 0;

            for (close = open; *close; close++) {
                if (*close == '{')
                    depth++;
                else if (*close == '}' && --depth == 0)
                    break;
            }
            p = *close ? close + 1 : close;
            continue;
        }
        close = strchr(open, '}');
        if (!close)
            break;
        for (const char *a = sel; a < open;) {
            const char *comma = memchr(a, ',', open - a), *e = comma ? comma : open;

            while (a < e && g_ascii_isspace(*a))
                a++;
            apply_selector(css, a, e, open + 1, close);
            a = e + 1;
        }
        p = close + 1;
    }
    g_free(text);
}

KsStyleSheet *ks_style_sheet_new(const char *custom_css)
{
    KsStyleSheet *css = g_new0(KsStyleSheet, 1);

    css->classes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    css->elements = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (size_t i = 0; i < G_N_ELEMENTS(colors); i++)
        entry(css->classes, colors[i].name, -1)->color = g_intern_static_string(colors[i].hex);
    if (custom_css)
        parse_css(css, custom_css);
    return css;
}

void ks_style_sheet_free(KsStyleSheet *css)
{
    if (!css)
        return;
    g_hash_table_destroy(css->classes);
    g_hash_table_destroy(css->elements);
    g_free(css);
}

static const KsClassStyle *lookup(GHashTable *t, const char *name, int len)
{
    char key[64];

    if (len < 0)
        len = strlen(name);
    if (len <= 0 || len >= (int)sizeof key)
        return NULL;
    for (int i = 0; i < len; i++)
        key[i] = g_ascii_tolower(name[i]);
    key[len] = '\0';
    return g_hash_table_lookup(t, key);
}

const KsClassStyle *ks_style_sheet_class(const KsStyleSheet *css, const char *element, const char *name, int len)
{
    static KsStyleSheet *builtin;
    const KsClassStyle *cs = NULL;

    if (!css) {
        if (!builtin)
            builtin = ks_style_sheet_new(NULL);
        css = builtin;
    }
    if (len < 0)
        len = strlen(name);
    if (element) {
        char *key = g_strdup_printf("%s.%.*s", element, len, name);

        cs = lookup(css->classes, key, -1);
        g_free(key);
    }
    return cs ? cs : lookup(css->classes, name, len);
}

const KsClassStyle *ks_style_sheet_element(const KsStyleSheet *css, const char *name, int len)
{
    return css ? lookup(css->elements, name, len) : NULL;
}
