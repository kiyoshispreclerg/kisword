/* link.c - MySword module links (see link.h). */
#include "link.h"

#include "books.h"

#include <stdio.h>
#include <string.h>

char *ks_strong_normalize(const char *s)
{
    const char *p = s;
    char letter;
    GString *g;

    while (*p == ' ')
        p++;
    if ((*p == 'W' || *p == 'w') && (p[1] == 'H' || p[1] == 'h' || p[1] == 'G' || p[1] == 'g'))
        p++;
    letter = g_ascii_toupper(*p);
    if ((letter != 'H' && letter != 'G') || !g_ascii_isdigit(p[1]))
        return NULL;
    p++;
    while (*p == '0' && g_ascii_isdigit(p[1]))
        p++;
    g = g_string_new(NULL);
    g_string_append_c(g, letter);
    while (g_ascii_isdigit(*p))
        g_string_append_c(g, *p++);
    /* An extended number: "H1254a", "G2316.1". */
    if (g_ascii_isalpha(*p) && !g_ascii_isalnum(p[1]))
        g_string_append_c(g, *p++);
    else if (*p == '.' && g_ascii_isdigit(p[1]))
        while (*p == '.' || g_ascii_isdigit(*p))
            g_string_append_c(g, *p++);
    while (*p == ' ')
        p++;
    if (*p) {
        g_string_free(g, TRUE);
        return NULL;
    }
    return g_string_free(g, FALSE);
}

/* MyBible's book numbers, in our order (1 = Genesis). */
static const short mybible_books[] = {
    10,  20,  30,  40,  50,  60,  70,  80,  90,  100, 110, 120, 130, 140, 150, 160, 190, 220, 230, 240, 250, 260,
    290, 300, 310, 330, 340, 350, 360, 370, 380, 390, 400, 410, 420, 430, 440, 450, 460, 470, 480, 490, 500, 510,
    520, 530, 540, 550, 560, 570, 580, 590, 600, 610, 620, 630, 640, 650, 660, 670, 680, 690, 700, 710, 720, 730,
};

static int from_mybible(int n)
{
    for (size_t i = 0; i < G_N_ELEMENTS(mybible_books); i++)
        if (mybible_books[i] == n)
            return (int)i + 1;
    return 0;
}

/* "I Timothy", "ICO", "IIKi" -> "1 Timothy", "1CO", "2Ki"; NULL if s does
 * not start with a Roman numeral followed by a name. */
static char *arabic_book(const char *s)
{
    static const char *const romans[] = { "III", "II", "I" };

    for (int i = 0; i < 3; i++) {
        size_t n = strlen(romans[i]);
        const char *rest = s + n;

        if (strncmp(s, romans[i], n) == 0 && g_ascii_isalpha(*(*rest == ' ' ? rest + 1 : rest)))
            return g_strdup_printf("%d%s", 3 - i, rest);
    }
    return NULL;
}

static gboolean parse_text_ref(char *s, KsRef *r)
{
    char *sp;

    if (ks_ref_parse_en(s, r) && r->book)
        return TRUE;
    /* A translation after it: "John 3:16 KJV". */
    if ((sp = strrchr(s, ' '))) {
        char keep = *sp;
        gboolean ok;

        *sp = '\0';
        ok = ks_ref_parse_en(s, r) && r->book;
        *sp = keep;
        if (ok)
            return TRUE;
    }
    return FALSE;
}

gboolean ks_link_parse_ref(const char *s, KsRef *r)
{
    int b = 0, c = 0, v = 0, b2 = 0, c2 = 0, v2 = 0;
    char tail;
    char *copy, *arabic = NULL;
    gboolean ok;

    memset(r, 0, sizeof *r);
    while (*s == ' ')
        s++;
    /* Book.chapter.verse, with an optional end of the same form. */
    if (sscanf(s, "%d.%d.%d-%d.%d.%d%c", &b, &c, &v, &b2, &c2, &v2, &tail) == 6 ||
        sscanf(s, "%d.%d.%d-%d%c", &b, &c, &v, &v2, &tail) == 4) {
        r->book = b;
        r->chapter = c;
        r->verse = v;
        /* Only a range within the chapter: "1.8.3-1.8.4" -> 8:3-4. */
        r->verse_end = (strchr(strchr(s, '-'), '.') == NULL || (b2 == b && c2 == c)) && v2 > v ? v2 : 0;
        return b >= 1 && c >= 1;
    }
    if (sscanf(s, "%d.%d.%d%c", &b, &c, &v, &tail) == 3 || sscanf(s, "%d.%d:%d%c", &b, &c, &v, &tail) == 3 ||
        sscanf(s, "%d.%d%c", &b, &c, &tail) == 2) {
        if (sscanf(s, "%d.%d.%d", &b, &c, &v) != 3 && sscanf(s, "%d.%d:%d", &b, &c, &v) != 3)
            v = 0;
        r->book = b;
        r->chapter = c;
        r->verse = v;
        return b >= 1 && c >= 1;
    }
    /* MyBible: "B:290 23:5". */
    if (sscanf(s, "B:%d %d:%d-%d%c", &b, &c, &v, &v2, &tail) == 4 || sscanf(s, "B:%d %d:%d%c", &b, &c, &v, &tail) == 3) {
        r->book = from_mybible(b);
        r->chapter = c;
        r->verse = v;
        r->verse_end = v2 > v ? v2 : 0;
        return r->book && c >= 1;
    }
    copy = g_strstrip(g_strdup(s));
    /* "Rom 3:23;" */
    while (*copy && strchr(";,.", copy[strlen(copy) - 1]))
        copy[strlen(copy) - 1] = '\0';
    ok = parse_text_ref(copy, r);
    if (!ok && (arabic = arabic_book(copy)))
        ok = parse_text_ref(arabic, r);
    g_free(arabic);
    g_free(copy);
    return ok;
}

static KsLinkKind kind_of(char c)
{
    switch (c) {
    case 'b': return KS_LINK_BIBLE;
    case 's': return KS_LINK_STRONG;
    case 'd': return KS_LINK_DICT;
    case 'm': return KS_LINK_MORPH;
    case 'c': return KS_LINK_COMMENTARY;
    case 'j': return KS_LINK_ENTRY;
    case 'k': return KS_LINK_BOOK;
    case 'p': return KS_LINK_PICTURE;
    case 'r': return KS_LINK_POPUP;
    default: return KS_LINK_NONE;
    }
}

void ks_link_clear(KsLink *l)
{
    g_free(l->module);
    g_free(l->key);
    g_free(l->anchor);
    memset(l, 0, sizeof *l);
}

gboolean ks_link_parse(const char *href, KsLink *l)
{
    const char *p = href;
    char *rest, *hash;

    memset(l, 0, sizeof *l);
    if (!href)
        return FALSE;
    while (*p == ' ')
        p++;
    if (g_str_has_prefix(p, "http://") || g_str_has_prefix(p, "https://") || g_str_has_prefix(p, "mailto:") ||
        g_str_has_prefix(p, "www.") || g_str_has_prefix(p, "//")) {
        l->kind = KS_LINK_URL;
        l->key = g_str_has_prefix(p, "www.") ? g_strconcat("http://", p, NULL)
                 : g_str_has_prefix(p, "//") ? g_strconcat("https:", p, NULL)
                                              : g_strdup(p);
        return TRUE;
    }
    if (g_str_has_prefix(p, "B:")) {
        l->kind = ks_link_parse_ref(p, &l->ref) ? KS_LINK_BIBLE : KS_LINK_NONE;
        return l->kind != KS_LINK_NONE;
    }
    if (*p == '#')
        p++;
    l->kind = kind_of(*p);
    if (l->kind == KS_LINK_NONE)
        return FALSE;
    p++;

    /* "-ABBR key": a module named by its abbreviation; "-*key": any. */
    if (*p == '-') {
        const char *sp = strchr(p, ' ');

        p++;
        if (*p == '*') {
            p++;
        } else if (sp) {
            l->module = g_strndup(p, sp - p);
            p = sp + 1;
        }
    }
    rest = g_strstrip(g_strdup(p));

    switch (l->kind) {
    case KS_LINK_BIBLE:
    case KS_LINK_COMMENTARY:
        /* "4.15.8.BHSE": in that Bible. */
        if (g_ascii_isdigit(*rest)) {
            char *q = rest;

            while (*q && (g_ascii_isdigit(*q) || strchr(".:-", *q)) && !(*q == '.' && g_ascii_isalpha(q[1])))
                q++;
            if (*q == '.' && g_ascii_isalpha(q[1])) {
                *q = '\0';
                g_free(l->module);
                l->module = g_strdup(q + 1);
            }
        }
        if (ks_link_parse_ref(rest, &l->ref)) {
            g_free(rest);
            return TRUE;
        }
        g_free(rest);
        break;
    case KS_LINK_STRONG:
        l->key = ks_strong_normalize(rest);
        g_free(rest);
        if (l->key)
            return TRUE;
        break;
    case KS_LINK_MORPH:
        /* "m/lἈαρών": the lemma itself. */
        if (rest[0] == '/' && rest[1] && rest[2]) {
            l->key = g_strdup(rest + 2);
            g_free(rest);
        } else {
            l->key = rest;
        }
        if (*l->key)
            return TRUE;
        break;
    case KS_LINK_PICTURE: {
        /* "3.jpg?mod=k-Timeline" */
        char *q = strchr(rest, '?');

        if (q) {
            char *mod = strstr(q, "mod=");

            *q = '\0';
            if (mod) {
                mod += 4;
                if (mod[0] && mod[1] == '-')
                    mod += 2;
                l->module = g_strdup(mod);
            }
        }
        l->key = rest;
        if (*l->key)
            return TRUE;
        break;
    }
    case KS_LINK_POPUP:
        l->key = g_uri_unescape_string(rest, NULL);
        g_free(rest);
        if (!l->key)
            l->key = g_strdup(p);
        if (*l->key)
            return TRUE;
        break;
    case KS_LINK_DICT:
        hash = strchr(rest, '#');
        if (hash && hash > rest) {
            l->anchor = g_strdup(hash + 1);
            *hash = '\0';
        }
        l->key = rest;
        if (*l->key)
            return TRUE;
        break;
    default:
        l->key = rest;
        if (*l->key)
            return TRUE;
        break;
    }
    l->kind = KS_LINK_NONE;
    return FALSE;
}
