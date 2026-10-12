/* gbf.c - parser for the tags inside MySword verse text (see gbf.h). */
#include "gbf.h"

#include "html.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 16

/* An open <span>/<font>/<a>/custom element and the state to go back to. */
typedef struct {
    char name[16];
    unsigned style;
    const char *color;
    gboolean block;
} Frame;

typedef struct {
    KsSpanCb cb;
    void *data;
    const KsStyleSheet *css;
    unsigned style;
    int indent;
    const char *color;
    Frame stack[MAX_DEPTH];
    int depth;
    gboolean space; /* the text so far ends in a space (or there is none) */
    gboolean in_q;  /* inside an interlinear <Q> block */
} Parser;

static void emit(Parser *ps, KsSpanKind kind, const char *text, int len)
{
    KsSpan sp = { kind, ps->style, ps->indent, text, len, NULL, 0, ps->color };

    if (kind == KS_SPAN_TEXT && len <= 0)
        return;
    /* A word of an interlinear is a column of lines in MySword (BHSE puts
     * <br> between them); here it stays on one line. */
    if (kind == KS_SPAN_LINE && ps->in_q) {
        if (ps->space)
            return;
        kind = KS_SPAN_TEXT;
        sp.kind = kind;
        sp.text = text = " ";
        sp.len = len = 1;
    }
    if (kind == KS_SPAN_TEXT)
        ps->space = text[len - 1] == ' ';
    ps->cb(&sp, ps->data);
}

/* Content tags (<RF>...<Rf>, <v>...</v>): emits the span and returns where
 * parsing continues. An unclosed one runs to the end of the verse. */
static const char *content_span(Parser *ps, KsSpanKind kind, const char *after, const char *close,
                                const char *mark, int mark_len)
{
    const char *end = strstr(after, close);
    const char *stop = end ? end : after + strlen(after);
    KsSpan sp = { kind, ps->style, ps->indent, after, (int)(stop - after), mark, mark_len, ps->color };

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

/* Value of attribute key in a tag's text ("span class='red' dir=rtl"),
 * without quotes; NULL if absent. */
static const char *attr(const char *s, int n, const char *key, int *len)
{
    int kn = strlen(key);

    for (int i = 1; i + kn < n; i++) {
        const char *v;
        char q = 0;
        int vn = 0;

        if (s[i - 1] != ' ' || g_ascii_strncasecmp(s + i, key, kn) != 0 || s[i + kn] != '=')
            continue;
        v = s + i + kn + 1;
        if (v < s + n && (*v == '\'' || *v == '"'))
            q = *v++;
        while (v + vn < s + n && (q ? v[vn] != q : (v[vn] != ' ' && v[vn] != '/')))
            vn++;
        *len = vn;
        return v;
    }
    return NULL;
}

static void push(Parser *ps, const char *tag, gboolean block)
{
    Frame *f;

    if (ps->depth >= MAX_DEPTH) /* drop the oldest: deep nesting is never meaningful */
        memmove(ps->stack, ps->stack + 1, sizeof ps->stack[0] * --ps->depth);
    f = &ps->stack[ps->depth++];
    g_strlcpy(f->name, tag, sizeof f->name);
    f->style = ps->style;
    f->color = ps->color;
    f->block = block;
    if (block)
        emit(ps, KS_SPAN_LINE, NULL, 0);
}

static void apply_class(Parser *ps, const KsClassStyle *cs)
{
    if (!cs)
        return;
    ps->style |= cs->style;
    if (cs->color)
        ps->color = cs->color;
}

/* Closes the innermost open tag of that name (and anything opened inside
 * it). A close with nothing open is ignored. */
static void pop(Parser *ps, const char *tag)
{
    for (int i = ps->depth - 1; i >= 0; i--) {
        if (strcmp(ps->stack[i].name, tag) == 0) {
            ps->style = ps->stack[i].style;
            ps->color = ps->stack[i].color;
            ps->depth = i;
            if (ps->stack[i].block)
                emit(ps, KS_SPAN_LINE, NULL, 0);
            return;
        }
    }
}

/* class='a b': every class that is known, in order. block if any is. */
static gboolean classes_block(Parser *ps, const char *tag, const char *v, int vn)
{
    gboolean block = FALSE;

    for (int i = 0; i < vn;) {
        int j = i;
        const KsClassStyle *cs;

        while (j < vn && v[j] != ' ')
            j++;
        cs = ks_style_sheet_class(ps->css, tag, v + i, j - i);
        block = block || (cs && cs->block);
        i = j + 1;
    }
    return block;
}

static void apply_classes(Parser *ps, const char *tag, const char *v, int vn)
{
    for (int i = 0; i < vn;) {
        int j = i;

        while (j < vn && v[j] != ' ')
            j++;
        apply_class(ps, ks_style_sheet_class(ps->css, tag, v + i, j - i));
        i = j + 1;
    }
}

/* <a class='strong' href='sG2316'>G2316</a> (what VerseRules make of
 * Strong's tags) is a Strong's number again; a morphology link likewise.
 * NULL if this <a> is an ordinary one. */
static const char *link_span(Parser *ps, const char *name, int n, const char *after)
{
    const char *cls, *href, *end;
    int cn = 0, hn = 0;
    KsSpanKind kind;

    cls = attr(name, n, "class", &cn);
    href = attr(name, n, "href", &hn);
    if ((cls && cn == 6 && strncmp(cls, "strong", 6) == 0) || (href && hn >= 3 && href[0] == 's' &&
                                                               (href[1] == 'G' || href[1] == 'H') &&
                                                               g_ascii_isdigit(href[2])))
        kind = KS_SPAN_STRONG;
    else if ((cls && cn == 5 && strncmp(cls, "morph", 5) == 0) || (href && hn >= 2 && href[0] == 'm' && !cls))
        kind = KS_SPAN_MORPH;
    else
        return NULL;
    end = strstr(after, "</a>");
    if (!end || memchr(after, '<', end - after))
        return NULL;
    if (kind == KS_SPAN_STRONG) {
        /* The number shown, or the one in the link if the text is a word. */
        const char *t = after;
        int tn = end - after;

        while (tn > 0 && *t == ' ')
            t++, tn--;
        while (tn > 0 && t[tn - 1] == ' ')
            tn--;
        if (!(tn >= 2 && (*t == 'G' || *t == 'H') && g_ascii_isdigit(t[1]))) {
            if (!href || hn < 3 || href[0] != 's')
                return NULL;
            emit(ps, KS_SPAN_TEXT, after, end - after);
            t = href + 1;
            tn = 1;
            while (tn < hn - 1 && g_ascii_isdigit(t[tn]))
                tn++;
        }
        emit(ps, KS_SPAN_STRONG, t, tn);
    } else {
        emit(ps, KS_SPAN_MORPH, after, end - after);
    }
    return end + 4;
}

/* HTML and custom elements. Returns where parsing continues, NULL if the
 * tag is not one of them. */
static const char *html_tag(Parser *ps, const char *name, int n, const char *after)
{
    static const struct { const char *tag; unsigned bit; } styles[] = {
        { "i", KS_ST_ITALIC }, { "em", KS_ST_ITALIC }, { "b", KS_ST_BOLD },
        { "strong", KS_ST_BOLD }, { "u", KS_ST_UNDERLINE }, { "sup", KS_ST_SUP },
        { "sub", KS_ST_SUB }, { "s", KS_ST_STRIKE }, { "strike", KS_ST_STRIKE }, { "del", KS_ST_STRIKE },
        { "small", KS_ST_SMALL },
    };
    /* Kept on the stack so a class on them applies to their content. */
    static const char *const nesting[] = { "span", "font", "a", "div" };
    /* Accepted and ignored: their content is kept. */
    static const char *const ignored[] = { "big", "h1", "h2", "h3", "h4", "hr", "img" };
    const KsClassStyle *el;
    gboolean closing = FALSE;
    char tag[16];
    int tn = 0;

    if (n > 0 && *name == '/') {
        closing = TRUE;
        name++;
        n--;
    }
    while (tn < n && (g_ascii_isalnum(name[tn]) || name[tn] == '-' || name[tn] == '_'))
        tn++;
    if (tn == 0 || tn >= (int)sizeof tag || (tn < n && name[tn] != ' ' && name[tn] != '/'))
        return NULL;
    for (int i = 0; i < tn; i++)
        tag[i] = g_ascii_tolower(name[i]);
    tag[tn] = '\0';

    for (size_t i = 0; i < G_N_ELEMENTS(styles); i++) {
        if (strcmp(tag, styles[i].tag) == 0) {
            /* <s> may also close a GBF-style <S>: only a strike if </s> follows. */
            if (strcmp(tag, "s") == 0 && !closing && !strstr(after, "</s>"))
                return NULL;
            set_style(ps, styles[i].bit, !closing);
            return after;
        }
    }
    /* Psalm superscriptions in Portuguese modules (ARC2009, LTT): a line of
     * their own, in italics. */
    if (strcmp(tag, "is") == 0 || strcmp(tag, "salmo") == 0) {
        set_style(ps, KS_ST_ITALIC, !closing);
        if (closing)
            emit(ps, KS_SPAN_LINE, NULL, 0);
        return after;
    }
    if (strcmp(tag, "br") == 0) {
        emit(ps, KS_SPAN_LINE, NULL, 0);
        return after;
    }
    if (strcmp(tag, "p") == 0) {
        if (closing) {
            emit(ps, KS_SPAN_PARA, NULL, 0);
            ps->indent = 0;
        }
        return after;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(nesting); i++) {
        const char *v;
        int vn;

        if (strcmp(tag, nesting[i]) != 0)
            continue;
        if (closing) {
            pop(ps, tag);
            return after;
        }
        if (strcmp(tag, "a") == 0) {
            const char *next = link_span(ps, name, n, after);

            if (next)
                return next;
        }
        v = attr(name, n, "class", &vn);
        push(ps, tag, v && classes_block(ps, tag, v, vn));
        if (v)
            apply_classes(ps, tag, v, vn);
        if (strcmp(tag, "font") == 0 && (v = attr(name, n, "color", &vn))) {
            const char *c = ks_style_color(v, vn);

            if (c)
                ps->color = c;
        }
        /* "S<font size=-1>ENHOR</font>": small capitals by hand. */
        if (strcmp(tag, "font") == 0 && (v = attr(name, n, "size", &vn)) && vn > 1 && *v == '-')
            ps->style |= KS_ST_SMALL;
        return after;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(ignored); i++)
        if (strcmp(tag, ignored[i]) == 0)
            return after;
    /* <gloss>...</gloss>, <hbint/>: elements styled by the CustomCSS. */
    el = ks_style_sheet_element(ps->css, tag, tn);
    if (el) {
        if (closing) {
            pop(ps, tag);
        } else if (name[n - 1] != '/') {
            push(ps, tag, el->block);
            apply_class(ps, el);
        }
        return after;
    }
    return NULL;
}

#define IS(lit) (n == (int)sizeof(lit) - 1 && memcmp(name, lit, n) == 0)

static gboolean all_digits(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (!g_ascii_isdigit(s[i]))
            return FALSE;
    return n > 0;
}

/* A space before an interlinear gloss, unless there is one already. */
static void separate(Parser *ps)
{
    if (!ps->space)
        emit(ps, KS_SPAN_TEXT, " ", 1);
}

/* Tags seen in real modules that carry nothing to show: anchors
 * (<v=Gen.1.1>, <ref=01.1.1>), cross references (<RX 43.1.1-5>), language
 * and direction spans (<heb>, <rtl>), word markers for VerseRules (<wt>),
 * leftovers of rules (<hide>, <GES.123>). Matched by their leading
 * letters, after a '/'. */
static gboolean ignored_tag(const char *name, int n)
{
    static const char *const names[] = {
        "ref", "RX", "SP", "WH", "XWG", "V", "VX", "heb", "grk", "arc", "rtl", "fnc", "NB", "Nb", "wt", "wg",
        "w", "cl", "text", "CHECK", "hide", "GES", "BHS", "KJV", "BDB", "TWOT", "prela", "qr", "QR", "KQ",
        "kq", "ht", "n", "nd", "v", "r", "C", "c", "e", "t", "CCM", "red",
    };
    int k = 0;

    if (n > 0 && *name == '/') {
        name++;
        n--;
    }
    if (n > 0 && (strspn(name, "@") == (size_t)n || strspn(name, "*") == (size_t)n))
        return TRUE;
    if (n >= 2 && name[0] == 'W' && (name[1] == 'H' || name[1] == 'G')) /* <WH>, <WHl>: no number */
        return TRUE;
    while (k < n && g_ascii_isalpha(name[k]))
        k++;
    for (size_t i = 0; k && i < G_N_ELEMENTS(names); i++)
        if ((int)strlen(names[i]) == k && memcmp(name, names[i], k) == 0)
            return TRUE;
    return FALSE;
}

/* Some modules use angle brackets as quotation marks ("<Ɔlɛpʋ́.>" in the
 * Nkonya NT). A "tag" with non-ASCII letters, ending in punctuation, or
 * that is a capitalized word ("<Yohane>"), is text. */
static gboolean looks_literal(const char *name, int n)
{
    int lower = 0;

    for (int i = 0; i < n; i++)
        if ((unsigned char)name[i] >= 0x80)
            return TRUE;
    if (n > 0 && (strchr(".!?,;:", name[n - 1]) || name[0] == ' '))
        return TRUE;
    for (int i = 1; i < n && g_ascii_islower(name[i]); i++)
        lower++;
    return n >= 3 && g_ascii_isupper(name[0]) && lower == n - 1;
}

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
    if (IS("TS") || (n > 2 && name[0] == 'T' && name[1] == 'S' && all_digits(name + 2, n - 2))) {
        set_style(ps, KS_ST_TITLE, TRUE); /* <TS1>, <TS3>: levels, all titles here */
        return after;
    }
    if (IS("Ts")) {
        /* A title is a line: two in a row ("<TS1>..<Ts><TS3>..<Ts>") stay two. */
        if (ps->style & KS_ST_TITLE)
            emit(ps, KS_SPAN_LINE, NULL, 0);
        set_style(ps, KS_ST_TITLE, FALSE);
        return after;
    }
    if (n > 3 && name[0] == 'R' && name[1] == 'X' && name[2] == ' ') {
        emit(ps, KS_SPAN_XREF, name + 3, n - 3);
        return after;
    }
    /* Interlinear blocks: <Q> <H>original<h> <X>transliteration<x>
     * <E>gloss<e> <q> (<G>/<g> for Greek, <T>/<t> for a gloss too). A text
     * view can't stack them: the gloss follows the word, smaller. */
    if (n == 1) {
        switch (name[0]) {
        case 'Q': case 'q':
            ps->in_q = name[0] == 'Q';
            return after;
        case 'H': case 'h': case 'G': case 'g':
            return after;
        case 'E': case 'T':
            separate(ps);
            ps->style |= KS_ST_GLOSS;
            return after;
        case 'e': case 't':
            ps->style &= ~KS_ST_GLOSS;
            return after;
        case 'X':
            separate(ps);
            ps->style |= KS_ST_TRANSLIT;
            return after;
        case 'x':
            ps->style &= ~KS_ST_TRANSLIT;
            return after;
        }
    }
    /* Strong's tense-voice-mood codes (<H8804>, <G5713>): morphology. */
    if (n >= 2 && (name[0] == 'H' || name[0] == 'G') && all_digits(name + 1, n - 1)) {
        emit(ps, KS_SPAN_MORPH, name, n);
        return after;
    }
    if (IS("CM")) {
        emit(ps, KS_SPAN_PARA, NULL, 0);
        ps->indent = 0;
        return after;
    }
    if (IS("CL") || IS("CI") || IS("CLX")) { /* CI: poetry line; CLX: Aleppo's section breaks */
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
    /* ALM21: "<r>20:43</r>", a verse merged into this one. */
    if (IS("r")) {
        const char *close = strstr(after, "</r>");

        if (close && close - after <= 12)
            return content_span(ps, KS_SPAN_VERSENUM, after, "</r>", NULL, 0);
    }
    {
        const char *next = html_tag(ps, name, n, after);

        if (next)
            return next;
    }
    if (ignored_tag(name, n))
        return after;
    if (looks_literal(name, n)) {
        emit(ps, KS_SPAN_TEXT, name - 1, n + 2);
        return after;
    }
    emit(ps, KS_SPAN_UNKNOWN, name, n);
    return after;
}

void ks_gbf_parse(const char *s, const KsStyleSheet *css, KsSpanCb cb, void *data)
{
    Parser ps = { 0 };
    const char *text = s, *p = s;

    if (!s)
        return;
    ps.cb = cb;
    ps.data = data;
    ps.css = css;
    ps.space = TRUE;
    while (*p) {
        const char *end;

        if (*p == '&') {
            char buf[8];
            int n = ks_html_entity(p, buf);

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
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
            /* A byte order mark pasted into the verse (ARC2009): nothing. */
            emit(&ps, KS_SPAN_TEXT, text, p - text);
            p += 3;
            text = p;
            continue;
        }
        if (*p == '\r' || *p == '\n') {
            /* Line breaks in the stored text are just whitespace, as in
             * MySword's HTML view (some modules end every verse in "\r"). */
            emit(&ps, KS_SPAN_TEXT, text, p - text);
            while (*p == '\r' || *p == '\n')
                p++;
            emit(&ps, KS_SPAN_TEXT, " ", 1);
            text = p;
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
        if (!(sp->style & (KS_ST_TITLE | KS_ST_HIDDEN)))
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

char *ks_gbf_plain(const char *src, const KsStyleSheet *css)
{
    GString *g = g_string_new(NULL);
    char *s, *d;

    ks_gbf_parse(src, css, plain_cb, g);
    /* Removed tags can leave two spaces side by side ("Belém. <v>17:59</v> Vive"). */
    for (s = d = g->str; *s; s++)
        if (!(*s == ' ' && d > g->str && d[-1] == ' '))
            *d++ = *s;
    *d = '\0';
    return g_strstrip(g_string_free(g, FALSE));
}

typedef struct {
    GString *g;
    unsigned style; /* style and color of the open tags */
    const char *color;
    gboolean open;
} Markup;

static const struct { unsigned bit; const char *open, *close; } markup_tags[] = {
    { KS_ST_ITALIC, "<i>", "</i>" }, { KS_ST_BOLD, "<b>", "</b>" },
    { KS_ST_UNDERLINE, "<u>", "</u>" }, { KS_ST_SUP, "<sup>", "</sup>" },
    { KS_ST_SUB, "<sub>", "</sub>" }, { KS_ST_TITLE, "<b>", "</b>" },
    { KS_ST_STRIKE, "<s>", "</s>" }, { KS_ST_SMALL, "<small>", "</small>" },
    { KS_ST_GLOSS | KS_ST_TRANSLIT, "<small>", "</small>" },
};

static void markup_close(Markup *mk)
{
    if (!mk->open)
        return;
    for (size_t i = G_N_ELEMENTS(markup_tags); i-- > 0;)
        if (mk->style & markup_tags[i].bit)
            g_string_append(mk->g, markup_tags[i].close);
    if (mk->color)
        g_string_append(mk->g, "</span>");
    mk->open = FALSE;
}

static void markup_cb(const KsSpan *sp, void *data)
{
    Markup *mk = data;
    GString *g = mk->g;
    char *esc;

    switch (sp->kind) {
    case KS_SPAN_TEXT:
        if (sp->style & KS_ST_HIDDEN)
            break;
        if (mk->open && (sp->style != mk->style || sp->color != mk->color))
            markup_close(mk);
        if (!mk->open) {
            mk->style = sp->style;
            mk->color = sp->color;
            mk->open = TRUE;
            if (sp->color)
                g_string_append_printf(g, "<span foreground='%s'>", sp->color);
            for (size_t i = 0; i < G_N_ELEMENTS(markup_tags); i++)
                if (sp->style & markup_tags[i].bit)
                    g_string_append(g, markup_tags[i].open);
        }
        esc = g_markup_escape_text(sp->text, sp->len);
        g_string_append(g, esc);
        g_free(esc);
        break;
    case KS_SPAN_PARA:
        markup_close(mk);
        while (g->len && g->str[g->len - 1] == '\n')
            g_string_truncate(g, g->len - 1);
        if (g->len)
            g_string_append(g, "\n\n");
        break;
    case KS_SPAN_LINE:
        markup_close(mk);
        if (g->len && g->str[g->len - 1] != '\n')
            g_string_append_c(g, '\n');
        break;
    default:
        break;
    }
}

char *ks_gbf_markup(const char *src)
{
    Markup mk = { g_string_new(NULL), 0, NULL, FALSE };

    ks_gbf_parse(src, NULL, markup_cb, &mk);
    markup_close(&mk);
    while (mk.g->len && mk.g->str[mk.g->len - 1] == '\n')
        g_string_truncate(mk.g, mk.g->len - 1);
    return g_string_free(mk.g, FALSE);
}
