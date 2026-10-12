/* ref.c - parsing and formatting Bible references (see ref.h). */
#include "ref.h"

#include "books.h"

#include <string.h>

static const char *skip_spaces(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static int number(const char **pp)
{
    const char *p = *pp;
    long n = 0;

    if (!g_ascii_isdigit(*p))
        return -1;
    while (g_ascii_isdigit(*p)) {
        if (n < 100000)
            n = n * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return (int)n;
}

static gboolean is_dash(const char **pp)
{
    const char *p = *pp;

    if (*p == '-') {
        *pp = p + 1;
        return TRUE;
    }
    if (strncmp(p, "\xe2\x80\x93", 3) == 0) { /* en dash */
        *pp = p + 3;
        return TRUE;
    }
    return FALSE;
}

static gboolean parse(const char *s, KsRef *r, int (*book_lookup)(const char *, gssize))
{
    const char *p, *name_end;
    gboolean has_name;

    memset(r, 0, sizeof *r);
    s = skip_spaces(s);
    p = s;

    /* A leading number is part of the book ("1Co", "1 Coríntios") only if a
     * name follows it; otherwise it is the chapter ("3:16"). */
    if (g_ascii_isdigit(*p)) {
        const char *q = p;

        while (g_ascii_isdigit(*q))
            q++;
        q = skip_spaces(q);
        has_name = *q && !g_ascii_isdigit(*q) && !strchr(":.,-", *q);
        if (has_name)
            p = q;
    } else {
        has_name = *p != '\0';
    }

    if (has_name) {
        while (*p && !g_ascii_isdigit(*p))
            p = g_utf8_next_char(p);
        name_end = p;
        while (name_end > s && (name_end[-1] == ' ' || name_end[-1] == '\t'))
            name_end--;
        r->book = book_lookup(s, name_end - s);
        if (!r->book)
            return FALSE;
    }

    p = skip_spaces(p);
    if (!*p) {
        if (!r->book)
            return FALSE;
        r->chapter = 1;
        return TRUE;
    }

    r->chapter = number(&p);
    if (r->chapter <= 0)
        return FALSE;
    p = skip_spaces(p);
    if (*p == ':' || *p == '.' || *p == ',') {
        p = skip_spaces(p + 1);
        r->verse = number(&p);
        if (r->verse <= 0)
            return FALSE;
        p = skip_spaces(p);
        if (is_dash(&p)) {
            p = skip_spaces(p);
            r->verse_end = number(&p);
            if (r->verse_end <= 0)
                return FALSE;
            /* "3:16-4:2": ranges across chapters keep only the start. */
            if (*p == ':') {
                p++;
                if (number(&p) <= 0)
                    return FALSE;
                r->verse_end = 0;
            }
        }
    }
    p = skip_spaces(p);
    if (*p)
        return FALSE;
    if (r->verse_end <= r->verse)
        r->verse_end = 0;
    return TRUE;
}

gboolean ks_ref_parse(const char *s, KsRef *r)
{
    return parse(s, r, ks_book_lookup);
}

gboolean ks_ref_parse_en(const char *s, KsRef *r)
{
    return parse(s, r, ks_book_lookup_en);
}

char *ks_ref_format(int book, int chapter, int verse, int verse_end)
{
    const char *b = ks_book_abbrev(book);

    if (verse > 0 && verse_end > verse)
        return g_strdup_printf("%s %d:%d-%d", b, chapter, verse, verse_end);
    if (verse > 0)
        return g_strdup_printf("%s %d:%d", b, chapter, verse);
    return g_strdup_printf("%s %d", b, chapter);
}
