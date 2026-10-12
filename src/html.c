/* html.c - module HTML as styled text runs (see html.h). */
#include "html.h"

#include <stdlib.h>
#include <string.h>

/* HTML 4's Latin-1 entities, U+00A0 to U+00FF in order. */
static const char *const latin1[96] = {
    "nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo",
    "not", "shy", "reg", "macr", "deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot",
    "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest", "Agrave", "Aacute", "Acirc",
    "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute",
    "Icirc", "Iuml", "ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash",
    "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig", "agrave", "aacute", "acirc", "atilde",
    "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc",
    "iuml", "eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave",
    "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml",
};

static const struct { const char *name; gunichar c; } others[] = {
    { "amp", '&' },       { "lt", '<' },        { "gt", '>' },        { "quot", '"' },
    { "apos", '\'' },     { "ndash", 0x2013 },  { "mdash", 0x2014 },  { "lsquo", 0x2018 },
    { "rsquo", 0x2019 },  { "sbquo", 0x201A },  { "ldquo", 0x201C },  { "rdquo", 0x201D },
    { "bdquo", 0x201E },  { "dagger", 0x2020 }, { "Dagger", 0x2021 }, { "bull", 0x2022 },
    { "hellip", 0x2026 }, { "permil", 0x2030 }, { "prime", 0x2032 },  { "Prime", 0x2033 },
    { "lsaquo", 0x2039 }, { "rsaquo", 0x203A }, { "euro", 0x20AC },   { "trade", 0x2122 },
    { "larr", 0x2190 },   { "uarr", 0x2191 },   { "rarr", 0x2192 },   { "darr", 0x2193 },
    { "harr", 0x2194 },   { "OElig", 0x0152 },  { "oelig", 0x0153 },  { "Scaron", 0x0160 },
    { "scaron", 0x0161 }, { "Yuml", 0x0178 },   { "fnof", 0x0192 },   { "circ", 0x02C6 },
    { "tilde", 0x02DC },  { "ensp", 0x2002 },   { "emsp", 0x2003 },   { "thinsp", 0x2009 },
    { "zwnj", 0x200C },   { "zwj", 0x200D },    { "lrm", 0x200E },    { "rlm", 0x200F },
};

int ks_html_entity(const char *p, char out[8])
{
    const char *semi = NULL;
    gunichar c = 0;
    int n;

    if (*p != '&')
        return 0;
    for (int i = 1; i < 12 && p[i]; i++) {
        if (p[i] == ';') {
            semi = p + i;
            break;
        }
    }
    if (!semi)
        return 0;
    n = semi - (p + 1);
    if (p[1] == '#') {
        gboolean hex = p[2] == 'x' || p[2] == 'X';
        const char *digits = p + (hex ? 3 : 2);
        char *end;

        c = (gunichar)strtoul(digits, &end, hex ? 16 : 10);
        if (end != semi || end == digits)
            return 0;
    } else {
        for (size_t i = 0; i < G_N_ELEMENTS(others) && !c; i++)
            if ((int)strlen(others[i].name) == n && strncmp(p + 1, others[i].name, n) == 0)
                c = others[i].c;
        for (int i = 0; i < 96 && !c; i++)
            if ((int)strlen(latin1[i]) == n && strncmp(p + 1, latin1[i], n) == 0)
                c = 0xA0 + i;
    }
    if (c == 0 || !g_unichar_validate(c))
        return 0;
    out[g_unichar_to_utf8(c, out)] = '\0';
    return semi - p + 1;
}

/* ---- parser ------------------------------------------------------------- */

/* Open elements: what each adds to the text inside it. Elements are closed
 * the way browsers do it, more or less: a close tag pops up to the nearest
 * element of that name (stray ones are ignored), and a new <p>, <li>, <tr>
 * or cell closes the previous one. */
#define MAX_DEPTH 64

typedef struct {
    char name[16];
    unsigned style;
    const char *color;
    char *href;
    char list; /* <ol>: '1', 'a', 'A', 'i', 'I'; <ul>: '*'; else 0 */
    int counter;
    int indent;
    int heading;
    int align;
    gboolean hidden;
    gboolean block; /* display:block from CSS: a line break after it */
} Elem;

typedef struct {
    KsHtmlRunCb cb;
    void *data;
    const KsStyleSheet *css;
    Elem st[MAX_DEPTH];
    int depth;
    GString *run;   /* text not yet passed to cb, all with the attributes in ra */
    KsHtmlRun ra;
    char *ra_href;
    KsHtmlRun last; /* attributes of the last text, for the space after it */
    char *last_href;
    gboolean any;   /* some text out already */
    gboolean space; /* whitespace seen since the last text */
    int pending;    /* line breaks to put before the next text */
    gboolean first_cell;
} Html;

/* What the text here looks like; FALSE if it is hidden. */
static gboolean cur_attrs(const Html *h, KsHtmlRun *a)
{
    memset(a, 0, sizeof *a);
    for (int i = 0; i < h->depth; i++) {
        const Elem *e = &h->st[i];

        if (e->hidden)
            return FALSE;
        a->style |= e->style;
        if (e->color)
            a->color = e->color;
        if (e->href)
            a->href = e->href;
        a->indent += e->indent;
        if (e->heading)
            a->heading = e->heading;
        if (e->align)
            a->align = e->align;
    }
    return TRUE;
}

static gboolean same_attrs(const KsHtmlRun *a, const char *a_href, const KsHtmlRun *b)
{
    return a->style == b->style && a->heading == b->heading && a->indent == b->indent && a->color == b->color &&
           a->align == b->align && g_strcmp0(a_href, b->href) == 0;
}

static void flush(Html *h)
{
    if (h->run->len) {
        KsHtmlRun r = h->ra;

        r.text = h->run->str;
        r.len = h->run->len;
        r.href = h->ra_href;
        h->cb(&r, h->data);
    }
    g_string_truncate(h->run, 0);
}

static void out(Html *h, const char *t, int len, const KsHtmlRun *a)
{
    if (len <= 0)
        return;
    if (h->run->len && !same_attrs(&h->ra, h->ra_href, a))
        flush(h);
    if (!h->run->len) {
        h->ra = *a;
        h->ra.text = h->ra.href = h->ra.image = h->ra.anchor = NULL;
        g_free(h->ra_href);
        h->ra_href = g_strdup(a->href);
    }
    g_string_append_len(h->run, t, len);
}

/* Before any text: the pending line breaks, or the pending space (styled
 * like what both sides share, so a link's underline doesn't reach it). */
static void before_text(Html *h, const KsHtmlRun *a)
{
    if (h->any && h->pending) {
        KsHtmlRun none = { 0 };

        out(h, "\n\n", h->pending, &none);
    } else if (h->any && h->space) {
        KsHtmlRun sp = *a;

        sp.style &= h->last.style;
        if (g_strcmp0(a->href, h->last_href) != 0) {
            sp.href = NULL;
            sp.style &= ~KS_HTML_LINK;
        }
        if (a->color != h->last.color)
            sp.color = NULL;
        out(h, " ", 1, &sp);
    }
    h->pending = 0;
    h->space = FALSE;
    h->any = TRUE;
    h->last = *a;
    g_free(h->last_href);
    h->last_href = g_strdup(a->href);
}

/* Text: whitespace runs become one space, none at the start of a line. */
static void text(Html *h, const char *t, int len)
{
    KsHtmlRun a;

    if (len <= 0 || !cur_attrs(h, &a))
        return;
    for (int i = 0; i < len;) {
        int j = i;

        if (g_ascii_isspace(t[i])) {
            h->space = TRUE;
            i++;
            continue;
        }
        while (j < len && !g_ascii_isspace(t[j]))
            j++;
        before_text(h, &a);
        out(h, t + i, j - i, &a);
        i = j;
    }
}

/* Text that is not collapsed: entities (&nbsp;), bullets, tabs. */
static void literal(Html *h, const char *t, int len)
{
    KsHtmlRun a;

    if (!cur_attrs(h, &a))
        return;
    before_text(h, &a);
    out(h, t, len, &a);
}

/* An image or an anchor: a run of its own, with no text. */
static void special(Html *h, const char *image, const char *anchor)
{
    KsHtmlRun a;

    if (!cur_attrs(h, &a))
        return;
    if (image)
        before_text(h, &a);
    flush(h);
    a.image = image;
    a.anchor = anchor;
    a.text = "";
    h->cb(&a, h->data);
    if (image)
        h->last = a;
}

/* At least n line breaks here (1: new line, 2: blank line). They only come
 * out before more text: none at the start or the end. */
static void brk(Html *h, int n)
{
    h->space = FALSE;
    h->pending = MAX(h->pending, n);
}

static gboolean name_is(const char *tag, const char *const *list)
{
    for (int i = 0; list[i]; i++)
        if (strcmp(tag, list[i]) == 0)
            return TRUE;
    return FALSE;
}

/* ---- attributes ---- */

static char *decode(const char *s, int len)
{
    GString *g = g_string_sized_new(len);

    for (int i = 0; i < len;) {
        char buf[8];
        int n = s[i] == '&' ? ks_html_entity(s + i, buf) : 0;

        if (n && i + n <= len) {
            g_string_append(g, buf);
            i += n;
        } else {
            g_string_append_c(g, s[i++]);
        }
    }
    return g_string_free(g, FALSE);
}

/* The value of attribute `want` in the tag text [s, e) (after the tag
 * name), entities decoded; "" if it has no value, NULL if absent. */
static char *get_attr(const char *s, const char *e, const char *want)
{
    size_t wl = strlen(want);
    const char *p = s;

    while (p < e) {
        const char *n, *v = NULL, *ve = NULL;
        size_t nl;

        while (p < e && (g_ascii_isspace(*p) || *p == '/'))
            p++;
        n = p;
        while (p < e && !g_ascii_isspace(*p) && *p != '=' && *p != '/')
            p++;
        nl = p - n;
        if (!nl) {
            p++;
            continue;
        }
        while (p < e && g_ascii_isspace(*p))
            p++;
        if (p < e && *p == '=') {
            p++;
            while (p < e && g_ascii_isspace(*p))
                p++;
            if (p < e && (*p == '\'' || *p == '"')) {
                char q = *p++;

                v = p;
                while (p < e && *p != q)
                    p++;
                ve = p;
                if (p < e)
                    p++;
            } else {
                v = p;
                while (p < e && !g_ascii_isspace(*p))
                    p++;
                ve = p;
            }
        }
        if (nl == wl && g_ascii_strncasecmp(n, want, wl) == 0)
            return v ? decode(v, ve - v) : g_strdup("");
    }
    return NULL;
}

/* A CSS length as indent steps: "0.9em" -> 1, "3.6em" -> 2, "40px" -> 2. */
static int indent_steps(const char *v)
{
    char *end;
    double n = g_ascii_strtod(v, &end);

    if (n <= 0.2)
        return 0;
    if (g_str_has_prefix(end, "px"))
        n /= 20;
    else if (g_str_has_prefix(end, "pt"))
        n /= 15;
    else if (g_str_has_prefix(end, "%"))
        n /= 10;
    else
        n /= 1.8; /* em, or no unit */
    return CLAMP((int)(n + 0.5), 1, 4);
}

/* Font size relative to the text: -1 smaller, 1 larger, 0 about the same. */
static int size_class(const char *v)
{
    char *end;
    double n;

    if (strstr(v, "small") || strstr(v, "smaller"))
        return -1;
    if (strstr(v, "large") || strstr(v, "larger"))
        return 1;
    n = g_ascii_strtod(v, &end);
    if (end == v)
        return 0;
    if (g_str_has_prefix(end, "%"))
        n /= 100;
    else if (g_str_has_prefix(end, "px") || g_str_has_prefix(end, "pt"))
        n /= 16;
    return n < 0.95 ? -1 : n > 1.15 ? 1 : 0;
}

static void size_style(Elem *el, int size)
{
    if (size < 0)
        el->style |= KS_HTML_SMALL;
    else if (size > 0)
        el->style |= KS_HTML_BIG;
}

static int align_of(const char *v)
{
    if (g_ascii_strncasecmp(v, "center", 6) == 0 || g_ascii_strncasecmp(v, "middle", 6) == 0)
        return KS_HTML_ALIGN_CENTER;
    if (g_ascii_strncasecmp(v, "right", 5) == 0)
        return KS_HTML_ALIGN_RIGHT;
    return KS_HTML_ALIGN_NONE;
}

/* style="color: red; font-weight: bold; margin-left: 1.8em" */
static void inline_style(Elem *el, const char *css)
{
    char **decls = g_strsplit(css, ";", -1);

    for (int i = 0; decls[i]; i++) {
        char *colon = strchr(decls[i], ':'), *prop, *val;

        if (!colon)
            continue;
        *colon = '\0';
        prop = g_strstrip(g_ascii_strdown(decls[i], -1));
        val = g_strstrip(g_ascii_strdown(colon + 1, -1));
        if (strcmp(prop, "color") == 0) {
            const char *c = ks_style_color(val, -1);

            if (c)
                el->color = c;
        } else if (strcmp(prop, "font-weight") == 0) {
            if (strstr(val, "bold") || atoi(val) >= 600)
                el->style |= KS_HTML_BOLD;
        } else if (strcmp(prop, "font-style") == 0) {
            if (strstr(val, "italic") || strstr(val, "oblique"))
                el->style |= KS_HTML_ITALIC;
        } else if (strcmp(prop, "text-decoration") == 0) {
            if (strstr(val, "underline"))
                el->style |= KS_HTML_UNDERLINE;
            if (strstr(val, "line-through"))
                el->style |= KS_HTML_STRIKE;
        } else if (strcmp(prop, "display") == 0) {
            if (strcmp(val, "none") == 0)
                el->hidden = TRUE;
            else if (strcmp(val, "block") == 0)
                el->block = TRUE;
        } else if (strcmp(prop, "vertical-align") == 0) {
            if (strcmp(val, "super") == 0)
                el->style |= KS_HTML_SUP;
            else if (strcmp(val, "sub") == 0)
                el->style |= KS_HTML_SUB;
        } else if (strcmp(prop, "margin-left") == 0 || strcmp(prop, "padding-left") == 0) {
            el->indent = MAX(el->indent, indent_steps(val));
        } else if (strcmp(prop, "font-size") == 0) {
            size_style(el, size_class(val));
        } else if (strcmp(prop, "text-align") == 0) {
            el->align = align_of(val);
        }
        g_free(prop);
        g_free(val);
    }
    g_strfreev(decls);
}

/* What a class or a custom element of the module's CSS does. */
static void apply_class(Elem *el, const KsClassStyle *cs)
{
    static const struct { unsigned st, html; } map[] = {
        { KS_ST_ITALIC, KS_HTML_ITALIC },  { KS_ST_BOLD, KS_HTML_BOLD },     { KS_ST_UNDERLINE, KS_HTML_UNDERLINE },
        { KS_ST_SUP, KS_HTML_SUP },        { KS_ST_SUB, KS_HTML_SUB },       { KS_ST_STRIKE, KS_HTML_STRIKE },
        { KS_ST_SMALL, KS_HTML_SMALL },    { KS_ST_GLOSS, KS_HTML_SMALL },   { KS_ST_TITLE, KS_HTML_BOLD | KS_HTML_BIG },
        { KS_ST_TRANSLIT, KS_HTML_SMALL | KS_HTML_ITALIC },
    };

    if (!cs)
        return;
    for (size_t i = 0; i < G_N_ELEMENTS(map); i++)
        if (cs->style & map[i].st)
            el->style |= map[i].html;
    if (cs->style & KS_ST_HIDDEN)
        el->hidden = TRUE;
    if (cs->color)
        el->color = cs->color;
    else if (cs->style & KS_ST_RED)
        el->color = ks_style_color("red", 3);
    if (cs->block || cs->display == KS_DISPLAY_BLOCK || cs->display == KS_DISPLAY_TABLE)
        el->block = TRUE;
}

static void classes(Html *h, Elem *el, const char *list)
{
    for (const char *p = list; *p;) {
        const char *q;

        while (*p == ' ' || *p == '\t')
            p++;
        q = p;
        while (*q && *q != ' ' && *q != '\t')
            q++;
        if (q > p)
            apply_class(el, ks_style_sheet_class(h->css, el->name, p, q - p));
        p = q;
    }
}

/* ---- lists ---- */

static void roman(GString *g, int n, gboolean upper)
{
    static const struct { int v; const char *s; } r[] = {
        { 1000, "m" }, { 900, "cm" }, { 500, "d" }, { 400, "cd" }, { 100, "c" }, { 90, "xc" }, { 50, "l" },
        { 40, "xl" },  { 10, "x" },   { 9, "ix" },  { 5, "v" },    { 4, "iv" },  { 1, "i" },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(r) && n > 0 && n < 4000; i++)
        for (; n >= r[i].v; n -= r[i].v)
            for (const char *c = r[i].s; *c; c++)
                g_string_append_c(g, upper ? g_ascii_toupper(*c) : *c);
}

/* "1. ", "b. ", "iv. " or a bullet, for a new <li>. */
static void list_marker(Html *h)
{
    Elem *l = NULL;
    int ul_depth = 0;
    GString *g = g_string_new(NULL);

    for (int i = h->depth - 1; i >= 0; i--) {
        if (h->st[i].list && !l)
            l = &h->st[i];
        if (h->st[i].list == '*')
            ul_depth++;
    }
    if (!l || l->list == '*') {
        static const char *const bullets[] = { "\xe2\x80\xa2 ", "\xe2\x97\xa6 ", "\xe2\x96\xaa " };

        g_string_append(g, bullets[MAX(ul_depth - 1, 0) % 3]);
    } else {
        int n = ++l->counter;

        if (l->list == 'a' || l->list == 'A') {
            char s[8];
            int k = 0;

            for (; n > 0 && k < 7; n = (n - 1) / 26)
                s[k++] = (l->list == 'a' ? 'a' : 'A') + (n - 1) % 26;
            while (k)
                g_string_append_c(g, s[--k]);
        } else if (l->list == 'i' || l->list == 'I') {
            roman(g, n, l->list == 'I');
        } else {
            g_string_append_printf(g, "%d", n);
        }
        g_string_append(g, ". ");
    }
    literal(h, g->str, g->len);
    g_string_free(g, TRUE);
}

/* ---- tags ---- */

static void pop_to(Html *h, int i)
{
    while (h->depth > i) {
        Elem *e = &h->st[--h->depth];

        if (e->block)
            brk(h, 1);
        g_free(e->href);
        e->href = NULL;
    }
}

static int find_open(const Html *h, const char *name, const char *const *scope)
{
    for (int i = h->depth - 1; i >= 0; i--) {
        if (strcmp(h->st[i].name, name) == 0)
            return i;
        if (scope && name_is(h->st[i].name, scope))
            break;
    }
    return -1;
}

/* A new <p>, <li>, <tr>, cell, <dt>/<dd> closes the open one, unless it is
 * inside something else (a <li> in a nested list). */
static void implicit_close(Html *h, const char *name)
{
    static const char *const p_scope[] = { "div", "td", "th", "li", "blockquote", "table", "ul", "ol", "dd", NULL };
    static const char *const li_scope[] = { "ul", "ol", NULL };
    static const char *const cell_scope[] = { "tr", "table", NULL };
    static const char *const tr_scope[] = { "table", NULL };
    static const char *const d_scope[] = { "dl", NULL };
    int i = -1;

    if (strcmp(name, "p") == 0) {
        i = find_open(h, "p", p_scope);
    } else if (strcmp(name, "li") == 0) {
        i = find_open(h, "li", li_scope);
    } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
        i = find_open(h, "td", cell_scope);
        if (i < 0)
            i = find_open(h, "th", cell_scope);
    } else if (strcmp(name, "tr") == 0) {
        i = find_open(h, "tr", tr_scope);
    } else if (strcmp(name, "dt") == 0 || strcmp(name, "dd") == 0) {
        i = find_open(h, "dt", d_scope);
        if (i < 0)
            i = find_open(h, "dd", d_scope);
    }
    if (i >= 0)
        pop_to(h, i);
}

static const char *const paragraph_tags[] = { "p", "blockquote", "table", "pre", NULL };
static const char *const line_tags[] = { "div", "center", "article", "section", "header", "footer", "ul", "ol",
                                         "dl", "dt", "dd", "tr", "thead", "tbody", "address", "nav", "figure",
                                         "figcaption", "caption", NULL };
static const char *const void_tags[] = { "br", "hr", "img", "meta", "link", "input", "col", "wbr", "area",
                                         "base", "param", "source", "basefont", NULL };
/* Elements CustomCSS is not asked about (see style.c). */
static const char *const known_tags[] = { "a", "abbr", "address", "b", "big", "blockquote", "body", "caption",
                                          "center", "cite", "code", "dd", "del", "dfn", "div", "dl", "dt",
                                          "em", "font", "h1", "h2", "h3", "h4", "h5", "h6", "html", "i", "ins",
                                          "kbd", "li", "ol", "p", "pre", "s", "samp", "small", "span", "strike",
                                          "strong", "sub", "sup", "table", "tbody", "td", "th", "thead", "tr",
                                          "tt", "u", "ul", "var", NULL };

/* Line breaks that go with a tag name, open or close. */
static void tag_breaks(Html *h, const char *name, gboolean closing)
{
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
        brk(h, 2);
    } else if (strcmp(name, "br") == 0) {
        h->space = FALSE;
        h->pending = MIN(h->pending + 1, 2);
    } else if (strcmp(name, "hr") == 0) {
        brk(h, 1);
        literal(h, "\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94", 9);
        brk(h, 1);
    } else if (strcmp(name, "li") == 0) {
        brk(h, 1);
    } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
        if (!closing && !h->first_cell) {
            brk(h, 0);
            literal(h, "\t", 1);
        }
        h->first_cell = FALSE;
    } else if (name_is(name, paragraph_tags)) {
        brk(h, 2);
    } else if (name_is(name, line_tags)) {
        brk(h, 1);
    }
    if (strcmp(name, "tr") == 0)
        h->first_cell = TRUE;
}

/* A link standing as a word of its own. */
static void link_word(Html *h, const char *text, const char *href)
{
    KsHtmlRun a;

    if (!cur_attrs(h, &a))
        return;
    a.href = href;
    a.style |= KS_HTML_LINK;
    h->space = TRUE;
    before_text(h, &a);
    out(h, text, strlen(text), &a);
    h->space = TRUE;
}

/* <WH430>, <WG2316>, <WH25a>: a Strong's number, as verse text writes them. */
static gboolean strong_tag(Html *h, const char *name)
{
    char text[16], href[18];
    int i = 2;

    if (name[0] != 'w' || (name[1] != 'h' && name[1] != 'g') || !g_ascii_isdigit(name[2]))
        return FALSE;
    while (g_ascii_isdigit(name[i]))
        i++;
    while (g_ascii_isalpha(name[i]))
        i++;
    if (name[i])
        return FALSE;
    g_snprintf(text, sizeof text, "%c%s", g_ascii_toupper(name[1]), name + 2);
    g_snprintf(href, sizeof href, "s%s", text);
    link_word(h, text, href);
    return TRUE;
}

/* <BDB.a.ae.ab>, <TWOT.26b> (HL): an entry of another dictionary, named by
 * its abbreviation. */
static void ref_tag(Html *h, const char *name, int name_len, const char *key, int key_len)
{
    char *text = g_strdup_printf("%.*s %.*s", name_len, name, key_len, key);
    char *href = g_strdup_printf("d-%s", text);

    link_word(h, text, href);
    g_free(href);
    g_free(text);
}

static gboolean all_upper(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (!g_ascii_isupper(s[i]) && !g_ascii_isdigit(s[i]))
            return FALSE;
    return n > 0;
}

static void open_tag(Html *h, const char *name, const char *attrs, const char *end, gboolean self_closing)
{
    static const struct { const char *tag; unsigned style; } inline_tags[] = {
        { "b", KS_HTML_BOLD },        { "strong", KS_HTML_BOLD },  { "th", KS_HTML_BOLD },
        { "i", KS_HTML_ITALIC },      { "em", KS_HTML_ITALIC },    { "cite", KS_HTML_ITALIC },
        { "var", KS_HTML_ITALIC },    { "dfn", KS_HTML_ITALIC },   { "u", KS_HTML_UNDERLINE },
        { "ins", KS_HTML_UNDERLINE }, { "sup", KS_HTML_SUP },      { "sub", KS_HTML_SUB },
        { "s", KS_HTML_STRIKE },      { "strike", KS_HTML_STRIKE }, { "del", KS_HTML_STRIKE },
        { "small", KS_HTML_SMALL },   { "big", KS_HTML_BIG },
    };
    Elem el = { 0 };
    char *v;

    if (strong_tag(h, name))
        return;
    implicit_close(h, name);
    tag_breaks(h, name, FALSE);
    g_strlcpy(el.name, name, sizeof el.name);

    for (size_t i = 0; i < G_N_ELEMENTS(inline_tags); i++)
        if (strcmp(name, inline_tags[i].tag) == 0)
            el.style |= inline_tags[i].style;
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
        el.style |= KS_HTML_HEADING | KS_HTML_BOLD;
        el.heading = name[1] - '0';
    } else if (strcmp(name, "a") == 0) {
        if ((v = get_attr(attrs, end, "href"))) {
            el.style |= KS_HTML_LINK;
            el.href = v;
        }
        if ((v = get_attr(attrs, end, "name"))) {
            if (*v)
                special(h, NULL, v);
            g_free(v);
        }
    } else if (strcmp(name, "font") == 0) {
        if ((v = get_attr(attrs, end, "color"))) {
            const char *c = ks_style_color(v, -1);

            if (c)
                el.color = c;
            g_free(v);
        }
        if ((v = get_attr(attrs, end, "size"))) {
            int n = atoi(v);

            size_style(&el, *v == '-' || (*v != '+' && n > 0 && n < 3) ? -1 : *v == '+' || n > 3 ? 1 : 0);
            g_free(v);
        }
    } else if (strcmp(name, "ol") == 0 || strcmp(name, "ul") == 0) {
        el.indent = 1;
        el.list = '*';
        if (name[0] == 'o') {
            el.list = '1';
            if ((v = get_attr(attrs, end, "type"))) {
                if (strchr("aAiI", *v) && *v)
                    el.list = *v;
                g_free(v);
            }
            if ((v = get_attr(attrs, end, "start"))) {
                el.counter = MAX(atoi(v), 1) - 1;
                g_free(v);
            }
        }
    } else if (strcmp(name, "blockquote") == 0 || strcmp(name, "dd") == 0) {
        el.indent = 1;
    } else if (strcmp(name, "center") == 0) {
        el.align = KS_HTML_ALIGN_CENTER;
    } else if (strcmp(name, "img") == 0) {
        if ((v = get_attr(attrs, end, "src"))) {
            if (*v)
                special(h, v, NULL);
            g_free(v);
        }
    }

    if (name_is(name, paragraph_tags) || name_is(name, line_tags) || el.heading) {
        if ((v = get_attr(attrs, end, "align"))) {
            el.align = align_of(v);
            g_free(v);
        }
    }
    if ((v = get_attr(attrs, end, "id"))) {
        if (*v)
            special(h, NULL, v);
        g_free(v);
    }
    if (!name_is(name, known_tags))
        apply_class(&el, ks_style_sheet_element(h->css, name, -1));
    if ((v = get_attr(attrs, end, "class"))) {
        classes(h, &el, v);
        g_free(v);
    }
    if ((v = get_attr(attrs, end, "style"))) {
        inline_style(&el, v);
        g_free(v);
    }

    if (self_closing || name_is(name, void_tags) || h->depth >= MAX_DEPTH) {
        g_free(el.href);
        return;
    }
    if (el.block)
        brk(h, 1);
    h->st[h->depth++] = el;
    if (strcmp(name, "li") == 0)
        list_marker(h);
}

static void close_tag(Html *h, const char *name)
{
    int i = find_open(h, name, NULL);

    tag_breaks(h, name, TRUE);
    if (i >= 0)
        pop_to(h, i);
}

/* Raw-text elements whose content is never shown. */
static const char *skip_raw(const char *after, const char *tag)
{
    static const char *const raw[] = { "style", "script", "head", "title", NULL };
    char close[16];
    const char *p;

    if (!name_is(tag, raw))
        return NULL;
    g_snprintf(close, sizeof close, "</%s", tag);
    for (p = after; *p; p++)
        if (*p == '<' && g_ascii_strncasecmp(p, close, strlen(close)) == 0)
            break;
    if (*p) {
        const char *gt = strchr(p, '>');

        return gt ? gt + 1 : p + strlen(p);
    }
    return p;
}

/* The '>' ending the tag at p, skipping quoted attribute values. */
static const char *tag_end(const char *p)
{
    for (const char *q = p + 1; *q; q++) {
        if (*q == '>')
            return q;
        if (*q == '\'' || *q == '"') {
            const char *b = q - 1, *c;

            while (b > p && *b == ' ')
                b--;
            if (*b == '=' && (c = strchr(q + 1, *q)))
                q = c;
        }
    }
    return NULL;
}

static gboolean has_tags(const char *s)
{
    for (const char *p = strchr(s, '<'); p; p = strchr(p + 1, '<'))
        if (g_ascii_isalpha(p[1]) || p[1] == '/' || p[1] == '!')
            return TRUE;
    return FALSE;
}

void ks_html_render(const char *src, const KsStyleSheet *css, KsHtmlRunCb cb, void *data)
{
    Html h = { 0 };
    const char *p, *t;

    if (!src)
        return;
    h.cb = cb;
    h.data = data;
    h.css = css;
    h.run = g_string_new(NULL);
    h.first_cell = TRUE;

    if (!has_tags(src)) {
        /* Plain text: keep its line breaks, drop the CRs. */
        for (p = src; *p; p++)
            if (*p != '\r')
                g_string_append_c(h.run, *p);
        flush(&h);
        g_string_free(h.run, TRUE);
        return;
    }

    for (p = t = src; *p;) {
        if (*p == '&') {
            char buf[8];
            int n = ks_html_entity(p, buf);

            if (n) {
                text(&h, t, p - t);
                /* &nbsp; is not collapsible whitespace. */
                literal(&h, buf, strlen(buf));
                p += n;
                t = p;
            } else {
                p++;
            }
            continue;
        }
        if (*p != '<' || !(g_ascii_isalpha(p[1]) || p[1] == '/' || p[1] == '!')) {
            p++;
            continue;
        }
        text(&h, t, p - t);
        if (strncmp(p, "<!--", 4) == 0) {
            const char *e = strstr(p + 4, "-->");

            p = e ? e + 3 : p + strlen(p);
        } else {
            const char *gt = tag_end(p), *s = p + 1, *skip;
            gboolean closing = *s == '/';
            char name[16];
            int n = 0;

            if (!gt)
                gt = p + strlen(p) - 1;
            if (closing)
                s++;
            while (s + n < gt && n < (int)sizeof name - 1 && g_ascii_isalnum(s[n])) {
                name[n] = g_ascii_tolower(s[n]);
                n++;
            }
            name[n] = '\0';
            p = gt + 1;
            if (!closing && n && s[n] == '.' && all_upper(s, n) && gt > s + n + 1)
                ref_tag(&h, s, n, s + n + 1, gt - (s + n + 1));
            else if (!closing && (skip = skip_raw(p, name)))
                p = skip;
            else if (n && closing)
                close_tag(&h, name);
            else if (n)
                open_tag(&h, name, s + n, gt, gt > s && gt[-1] == '/');
        }
        t = p;
    }
    text(&h, t, p - t);
    flush(&h);
    pop_to(&h, 0);
    g_free(h.ra_href);
    g_free(h.last_href);
    g_string_free(h.run, TRUE);
}

/* ---- the plain interface ---- */

typedef struct {
    KsHtmlCb cb;
    void *data;
} Simple;

static void simple_cb(const KsHtmlRun *r, void *data)
{
    Simple *s = data;

    if (r->len)
        s->cb(r->text, r->len, r->style, s->data);
}

void ks_html_parse(const char *src, KsHtmlCb cb, void *data)
{
    Simple s = { cb, data };

    ks_html_render(src, NULL, simple_cb, &s);
}

static void plain_cb(const char *text, int len, unsigned style, void *data)
{
    (void)style;
    g_string_append_len(data, text, len);
}

char *ks_html_plain(const char *src)
{
    GString *g = g_string_new(NULL);

    ks_html_parse(src, plain_cb, g);
    while (g->len && g->str[g->len - 1] == '\n')
        g_string_truncate(g, g->len - 1);
    return g_string_free(g, FALSE);
}
