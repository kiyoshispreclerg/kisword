/* gbf.c - parser for the tags inside MySword verse text (see gbf.h). */
#include "gbf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    KsSpanCb cb;
    void *data;
    unsigned style;
    int indent;
} Parser;

static void emit(Parser *ps, KsSpanKind kind, const char *text, int len)
{
    KsSpan sp = { kind, ps->style, ps->indent, text, len, NULL, 0 };

    if (kind == KS_SPAN_TEXT && len <= 0)
        return;
    ps->cb(&sp, ps->data);
}

/* "&amp;" -> "&" into out; returns the bytes consumed, 0 if p is not a
 * recognized entity (it is then plain text). */
static int entity(const char *p, char out[8])
{
    static const struct { const char *name; gunichar c; } named[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' },
        { "apos", '\'' }, { "nbsp", 0x00A0 },
    };
    const char *semi = NULL;
    gunichar c = 0;
    int n;

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
        for (size_t i = 0; i < G_N_ELEMENTS(named); i++)
            if ((int)strlen(named[i].name) == n && strncmp(p + 1, named[i].name, n) == 0)
                c = named[i].c;
    }
    if (c == 0 || !g_unichar_validate(c))
        return 0;
    out[g_unichar_to_utf8(c, out)] = '\0';
    return semi - p + 1;
}

/* Content tags (<RF>...<Rf>, <v>...</v>): emits the span and returns where
 * parsing continues. An unclosed one runs to the end of the verse. */
static const char *content_span(Parser *ps, KsSpanKind kind, const char *after, const char *close,
                                const char *mark, int mark_len)
{
    const char *end = strstr(after, close);
    const char *stop = end ? end : after + strlen(after);
    KsSpan sp = { kind, ps->style, ps->indent, after, (int)(stop - after), mark, mark_len };

    ps->cb(&sp, ps->data);
    return end ? end + strlen(close) : stop;
}

static void set_style(Parser *ps, unsigned bit, gboolean on)
{
    if (on)
        ps->style |= bit;
    else
        ps->style &= ~bit;
}

static gboolean html_tag(Parser *ps, const char *name, int n)
{
    static const struct { const char *tag; unsigned bit; } styles[] = {
        { "i", KS_ST_ITALIC }, { "em", KS_ST_ITALIC }, { "b", KS_ST_BOLD },
        { "strong", KS_ST_BOLD }, { "u", KS_ST_UNDERLINE }, { "sup", KS_ST_SUP },
        { "sub", KS_ST_SUB },
    };
    /* Accepted and ignored: their content is kept, the markup itself has no
     * equivalent here (yet -- span classes come with VerseRules). */
    static const char *const ignored[] = { "span", "font", "a", "div", "small", "big", "h1", "h2", "h3", "h4" };
    gboolean closing = FALSE;
    char tag[12];
    int tn = 0;

    if (n > 0 && *name == '/') {
        closing = TRUE;
        name++;
        n--;
    }
    while (tn < n && g_ascii_isalnum(name[tn]))
        tn++;
    if (tn == 0 || tn >= (int)sizeof tag)
        return FALSE;
    for (int i = 0; i < tn; i++)
        tag[i] = g_ascii_tolower(name[i]);
    tag[tn] = '\0';

    for (size_t i = 0; i < G_N_ELEMENTS(styles); i++) {
        if (strcmp(tag, styles[i].tag) == 0) {
            set_style(ps, styles[i].bit, !closing);
            return TRUE;
        }
    }
    if (strcmp(tag, "br") == 0) {
        emit(ps, KS_SPAN_LINE, NULL, 0);
        return TRUE;
    }
    if (strcmp(tag, "p") == 0) {
        if (closing) {
            emit(ps, KS_SPAN_PARA, NULL, 0);
            ps->indent = 0;
        }
        return TRUE;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(ignored); i++)
        if (strcmp(tag, ignored[i]) == 0)
            return TRUE;
    return FALSE;
}

#define IS(lit) (n == (int)sizeof(lit) - 1 && memcmp(name, lit, n) == 0)

/* One tag, name = between '<' and '>'. Returns where parsing continues. */
static const char *tag(Parser *ps, const char *name, int n, const char *after)
{
    if (n >= 3 && name[0] == 'W' && (name[1] == 'H' || name[1] == 'G') && g_ascii_isdigit(name[2])) {
        emit(ps, KS_SPAN_STRONG, name + 1, n - 1);
        return after;
    }
    if (n >= 2 && name[0] == 'W' && name[1] == 'T') {
        emit(ps, KS_SPAN_MORPH, name + 2, n - 2);
        return after;
    }
    if (n == 2 && name[0] == 'F') {
        unsigned bit = 0;

        switch (g_ascii_toupper(name[1])) {
        case 'I': bit = KS_ST_ITALIC; break;
        case 'R': bit = KS_ST_RED; break;
        case 'O': bit = KS_ST_OTQUOTE; break;
        case 'U': bit = KS_ST_UNDERLINE; break;
        case 'B': bit = KS_ST_BOLD; break;
        case 'S': bit = KS_ST_SUP; break;
        }
        if (bit) {
            set_style(ps, bit, g_ascii_isupper(name[1]));
            return after;
        }
    }
    if (IS("TS") || IS("Ts")) {
        set_style(ps, KS_ST_TITLE, name[1] == 'S');
        return after;
    }
    if (IS("CM")) {
        emit(ps, KS_SPAN_PARA, NULL, 0);
        ps->indent = 0;
        return after;
    }
    if (IS("CL")) {
        emit(ps, KS_SPAN_LINE, NULL, 0);
        return after;
    }
    if (n >= 2 && name[0] == 'P' && (name[1] == 'I' || name[1] == 'F')) {
        gboolean digits = TRUE;

        for (int i = 2; i < n; i++)
            digits = digits && g_ascii_isdigit(name[i]);
        if (digits) {
            ps->indent = n > 2 ? CLAMP(atoi(name + 2), 0, 9) : 0;
            return after;
        }
    }
    if (n >= 2 && name[0] == 'R' && name[1] == 'F' && (n == 2 || name[2] == ' ')) {
        const char *mark = NULL;
        int mark_len = 0;

        for (int i = 2; i + 2 < n; i++) {
            if (name[i] == 'q' && name[i + 1] == '=') {
                mark = name + i + 2;
                while (mark + mark_len < name + n && mark[mark_len] != ' ')
                    mark_len++;
                if (mark_len >= 2 && (*mark == '"' || *mark == '\'')) {
                    mark++;
                    mark_len -= 2;
                }
                break;
            }
        }
        return content_span(ps, KS_SPAN_NOTE, after, "<Rf>", mark_len > 0 ? mark : NULL, mark_len);
    }
    if (IS("Rf"))
        return after; /* stray close */
    if (IS("v"))
        return content_span(ps, KS_SPAN_VERSENUM, after, "</v>", NULL, 0);
    if (!html_tag(ps, name, n))
        emit(ps, KS_SPAN_UNKNOWN, name, n);
    return after;
}

void ks_gbf_parse(const char *s, KsSpanCb cb, void *data)
{
    Parser ps = { cb, data, 0, 0 };
    const char *text = s, *p = s;

    if (!s)
        return;
    while (*p) {
        const char *end;

        if (*p == '&') {
            char buf[8];
            int n = entity(p, buf);

            if (n) {
                emit(&ps, KS_SPAN_TEXT, text, p - text);
                emit(&ps, KS_SPAN_TEXT, buf, strlen(buf));
                p += n;
                text = p;
            } else {
                p++;
            }
            continue;
        }
        if (*p != '<') {
            p++;
            continue;
        }
        end = p + 1;
        while (*end && *end != '>' && *end != '<')
            end++;
        if (*end != '>') { /* stray '<': literal text */
            p++;
            continue;
        }
        emit(&ps, KS_SPAN_TEXT, text, p - text);
        p = tag(&ps, p + 1, end - (p + 1), end + 1);
        text = p;
    }
    emit(&ps, KS_SPAN_TEXT, text, p - text);
}

static void plain_cb(const KsSpan *sp, void *data)
{
    GString *g = data;

    switch (sp->kind) {
    case KS_SPAN_TEXT:
        if (!(sp->style & KS_ST_TITLE))
            g_string_append_len(g, sp->text, sp->len);
        break;
    case KS_SPAN_PARA:
    case KS_SPAN_LINE:
        if (g->len && g->str[g->len - 1] != ' ')
            g_string_append_c(g, ' ');
        break;
    default:
        break;
    }
}

char *ks_gbf_plain(const char *src)
{
    GString *g = g_string_new(NULL);
    char *s, *d;

    ks_gbf_parse(src, plain_cb, g);
    /* Removed tags can leave two spaces side by side ("Belém. <v>17:59</v> Vive"). */
    for (s = d = g->str; *s; s++)
        if (!(*s == ' ' && d > g->str && d[-1] == ' '))
            *d++ = *s;
    *d = '\0';
    return g_strstrip(g_string_free(g, FALSE));
}
