/* html.c - module description HTML as styled text runs (see html.h). */
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

enum { F_BOLD, F_ITALIC, F_UNDERLINE, F_SUP, F_SUB, F_HEADING, F_LINK, F_SKIP, N_FLAGS };

typedef struct {
    KsHtmlCb cb;
    void *data;
    GString *run;      /* text not yet passed to cb, all in run_style */
    unsigned run_style;
    int count[N_FLAGS]; /* open tags per flag: nesting and stray closes both work */
    gboolean any;       /* some text out already */
    gboolean space;     /* whitespace seen since the last text */
    unsigned last;      /* style of the last text */
    int pending;        /* line breaks to put before the next text */
    gboolean first_cell;
} Html;

static unsigned cur_style(const Html *h)
{
    unsigned st = 0;

    for (int i = 0; i < F_SKIP; i++)
        if (h->count[i] > 0)
            st |= 1u << i;
    return st;
}

static void flush(Html *h)
{
    if (h->run->len)
        h->cb(h->run->str, h->run->len, h->run_style, h->data);
    g_string_truncate(h->run, 0);
}

static void out(Html *h, const char *t, int len, unsigned style)
{
    if (len <= 0)
        return;
    if (h->run->len && style != h->run_style)
        flush(h);
    h->run_style = style;
    g_string_append_len(h->run, t, len);
}

/* Before any text: the pending line breaks, or the pending space (styled
 * like what both sides share, so a link's underline doesn't reach it). */
static void before_text(Html *h, unsigned st)
{
    if (h->any && h->pending) {
        out(h, "\n\n", h->pending, 0);
    } else if (h->any && h->space) {
        out(h, " ", 1, st & h->last);
    }
    h->pending = 0;
    h->space = FALSE;
    h->any = TRUE;
    h->last = st;
}

/* Text: whitespace runs become one space, none at the start of a line. */
static void text(Html *h, const char *t, int len)
{
    unsigned st = cur_style(h);

    if (h->count[F_SKIP] > 0)
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
        before_text(h, st);
        out(h, t + i, j - i, st);
        i = j;
    }
}

/* Text that is not collapsed: entities (&nbsp;), bullets, tabs. */
static void literal(Html *h, const char *t, int len)
{
    unsigned st = cur_style(h);

    if (h->count[F_SKIP] > 0)
        return;
    before_text(h, st);
    out(h, t, len, st);
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

static void count(Html *h, int flag, gboolean closing)
{
    if (closing) {
        if (h->count[flag] > 0)
            h->count[flag]--;
    } else {
        h->count[flag]++;
    }
}

static void tag(Html *h, const char *tag, gboolean closing)
{
    static const char *const paragraph[] = { "p", "blockquote", "table", "pre", NULL };
    static const char *const line[] = { "div", "center", "article", "section", "header", "footer", "ul",
                                        "ol", "dl", "dt", "dd", "tr", "thead", "tbody", "address", NULL };
    static const struct { const char *tag; int flag; } inline_tags[] = {
        { "b", F_BOLD }, { "strong", F_BOLD }, { "th", F_BOLD }, { "i", F_ITALIC }, { "em", F_ITALIC },
        { "cite", F_ITALIC }, { "var", F_ITALIC }, { "u", F_UNDERLINE }, { "ins", F_UNDERLINE },
        { "sup", F_SUP }, { "sub", F_SUB }, { "a", F_LINK },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(inline_tags); i++)
        if (strcmp(tag, inline_tags[i].tag) == 0)
            count(h, inline_tags[i].flag, closing);

    if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2]) {
        brk(h, 2);
        count(h, F_HEADING, closing);
        count(h, F_BOLD, closing);
    } else if (strcmp(tag, "br") == 0) {
        h->space = FALSE;
        h->pending = MIN(h->pending + 1, 2);
    } else if (strcmp(tag, "hr") == 0) {
        brk(h, 1);
        literal(h, "\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94", 9);
        brk(h, 1);
    } else if (strcmp(tag, "li") == 0) {
        brk(h, 1);
        if (!closing)
            literal(h, "\xe2\x80\xa2 ", 4);
    } else if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
        if (!closing && !h->first_cell) {
            brk(h, 0);
            literal(h, "\t", 1);
        }
        h->first_cell = FALSE;
    } else if (name_is(tag, paragraph)) {
        brk(h, 2);
    } else if (name_is(tag, line)) {
        brk(h, 1);
    }
    if (strcmp(tag, "tr") == 0)
        h->first_cell = TRUE;
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

static gboolean has_tags(const char *s)
{
    for (const char *p = strchr(s, '<'); p; p = strchr(p + 1, '<'))
        if (g_ascii_isalpha(p[1]) || p[1] == '/' || p[1] == '!')
            return TRUE;
    return FALSE;
}

void ks_html_parse(const char *src, KsHtmlCb cb, void *data)
{
    Html h = { 0 };
    const char *p, *t;

    if (!src)
        return;
    h.cb = cb;
    h.data = data;
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
            const char *gt = strchr(p, '>'), *s = p + 1, *skip;
            gboolean closing = *s == '/';
            char name[12];
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
            if (!closing && (skip = skip_raw(p, name)))
                p = skip;
            else if (n)
                tag(&h, name, closing);
        }
        t = p;
    }
    text(&h, t, p - t);
    flush(&h);
    g_string_free(h.run, TRUE);
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
