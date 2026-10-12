/* search.c - full-text and Strong's number search (see search.h). */
#include "search.h"

#include "gbf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    guint8 book;
    guint16 chapter, verse;
    guint32 off; /* into text */
} Entry;

struct KsSearchIndex {
    KsModule *m;
    KsVerseIter *it;  /* NULL once complete */
    int total;        /* verses in the module, for progress */
    GArray *entries;  /* Entry */
    GString *build;   /* folded verses, each NUL-terminated, while building */
    char *text;       /* the same, trimmed to size, once complete */
};

char *ks_search_fold(const char *s)
{
    char *nfd = g_utf8_normalize(s ? s : "", -1, G_NORMALIZE_NFD);
    GString *g = g_string_sized_new(strlen(nfd) + 2);
    gboolean space = TRUE;

    g_string_append_c(g, ' ');
    for (const char *p = nfd; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        GUnicodeType t = g_unichar_type(c);

        if (t == G_UNICODE_NON_SPACING_MARK || t == G_UNICODE_SPACING_MARK || t == G_UNICODE_ENCLOSING_MARK ||
            t == G_UNICODE_FORMAT)
            continue; /* accents, Hebrew points, zero-width and direction marks */
        if (g_unichar_isalnum(c)) {
            g_string_append_unichar(g, g_unichar_tolower(c));
            space = FALSE;
        } else if (!space) {
            g_string_append_c(g, ' ');
            space = TRUE;
        }
    }
    if (!space)
        g_string_append_c(g, ' ');
    g_free(nfd);
    return g_string_free(g, FALSE);
}

gboolean ks_search_parse_strong(const char *s, char *testament, int *number)
{
    char t;
    long n;
    char *end;

    while (*s == ' ')
        s++;
    t = g_ascii_toupper(*s);
    if ((t != 'H' && t != 'G') || !g_ascii_isdigit(s[1]))
        return FALSE;
    n = strtol(s + 1, &end, 10);
    while (*end == ' ')
        end++;
    if (*end || n <= 0 || n > 99999)
        return FALSE;
    *testament = t;
    *number = (int)n;
    return TRUE;
}

/* ---- index ------------------------------------------------------------------ */

KsSearchIndex *ks_search_index_new(KsModule *m)
{
    KsSearchIndex *idx = g_new0(KsSearchIndex, 1);

    idx->m = m;
    idx->it = ks_module_iter_new(m, NULL);
    idx->total = MAX(ks_module_verse_count(m), 1);
    idx->entries = g_array_sized_new(FALSE, FALSE, sizeof(Entry), idx->total);
    idx->build = g_string_sized_new(64 * 1024);
    return idx;
}

void ks_search_index_free(KsSearchIndex *idx)
{
    if (!idx)
        return;
    ks_module_iter_free(idx->it);
    g_array_free(idx->entries, TRUE);
    if (idx->build)
        g_string_free(idx->build, TRUE);
    g_free(idx->text);
    g_free(idx);
}

gboolean ks_search_index_step(KsSearchIndex *idx, int max_verses)
{
    int book, chapter, verse;
    const char *raw;

    for (int i = 0; idx->it && i < max_verses; i++) {
        Entry e;
        char *plain, *folded;

        if (!ks_module_iter_next(idx->it, &book, &chapter, &verse, &raw, NULL)) {
            gsize len = idx->build->len;

            g_clear_pointer(&idx->it, ks_module_iter_free);
            /* The GString grew by doubling: give the slack back. */
            idx->text = g_realloc(g_string_free(idx->build, FALSE), len + 1);
            idx->build = NULL;
            break;
        }
        if (book < 1 || book > 255 || chapter < 0 || chapter > G_MAXUINT16 || verse < 0 || verse > G_MAXUINT16)
            continue;
        plain = ks_gbf_plain(raw, idx->m->css);
        folded = ks_search_fold(plain);
        e.book = (guint8)book;
        e.chapter = (guint16)chapter;
        e.verse = (guint16)verse;
        e.off = idx->build->len;
        g_string_append_len(idx->build, folded, strlen(folded) + 1);
        g_array_append_val(idx->entries, e);
        g_free(folded);
        g_free(plain);
    }
    return idx->it == NULL;
}

double ks_search_index_progress(const KsSearchIndex *idx)
{
    return idx->it ? MIN(1.0, (double)idx->entries->len / idx->total) : 1.0;
}

gboolean ks_search_index_done(const KsSearchIndex *idx)
{
    return idx->it == NULL;
}

/* ---- queries ------------------------------------------------------------------ */

static gboolean in_range(const KsSearchOptions *opt, int book)
{
    return !opt->book_from || (book >= opt->book_from && book <= opt->book_to);
}

static void add_hit(GArray *hits, int book, int chapter, int verse)
{
    KsHit h = { (guint8)book, (guint16)chapter, (guint16)verse };

    g_array_append_val(hits, h);
}

/* The folded words of the query; with whole_words each one keeps its
 * surrounding spaces (" amor "), else just the word ("amor"). */
static GPtrArray *needles(const char *folded, KsSearchMode mode, gboolean whole_words)
{
    GPtrArray *n = g_ptr_array_new_with_free_func(g_free);
    const char *p = folded + 1; /* past the leading space */

    if (mode == KS_SEARCH_PHRASE) {
        size_t len = strlen(folded);

        if (len > 2)
            g_ptr_array_add(n, whole_words ? g_strdup(folded) : g_strndup(folded + 1, len - 2));
        return n;
    }
    while (*p) {
        const char *sp = strchr(p, ' ');

        if (!sp || sp == p)
            break;
        g_ptr_array_add(n, whole_words ? g_strndup(p - 1, sp - p + 2) : g_strndup(p, sp - p));
        p = sp + 1;
    }
    return n;
}

GArray *ks_search_text(const KsSearchIndex *idx, const char *query, const KsSearchOptions *opt)
{
    GArray *hits = g_array_new(FALSE, FALSE, sizeof(KsHit));
    char *folded;
    GPtrArray *n;

    if (!idx->text)
        return hits;
    folded = ks_search_fold(query);
    n = needles(folded, opt->mode, opt->whole_words);

    for (guint i = 0; n->len && i < idx->entries->len; i++) {
        const Entry *e = &g_array_index(idx->entries, Entry, i);
        const char *text = idx->text + e->off;
        gboolean match = opt->mode != KS_SEARCH_ANY_WORD;

        if (!in_range(opt, e->book))
            continue;
        for (guint k = 0; k < n->len; k++) {
            gboolean found = strstr(text, g_ptr_array_index(n, k)) != NULL;

            if (opt->mode == KS_SEARCH_ANY_WORD && found) {
                match = TRUE;
                break;
            }
            if (opt->mode != KS_SEARCH_ANY_WORD && !found) {
                match = FALSE;
                break;
            }
        }
        if (match)
            add_hit(hits, e->book, e->chapter, e->verse);
    }
    g_ptr_array_free(n, TRUE);
    g_free(folded);
    return hits;
}

typedef struct {
    char testament;
    int number;
    gboolean found;
} StrongMatch;

static void strong_cb(const KsSpan *sp, void *data)
{
    StrongMatch *sm = data;
    char buf[16];

    /* "H430", "H0430", "H430a"... */
    if (sp->kind != KS_SPAN_STRONG || sp->len < 2 || g_ascii_toupper(sp->text[0]) != sm->testament)
        return;
    g_snprintf(buf, sizeof buf, "%.*s", MIN(sp->len - 1, (int)sizeof buf - 1), sp->text + 1);
    if (atoi(buf) == sm->number)
        sm->found = TRUE;
}

GArray *ks_search_strong(KsModule *m, char testament, int number, const KsSearchOptions *opt)
{
    GArray *hits = g_array_new(FALSE, FALSE, sizeof(KsHit));
    char like[32];
    KsVerseIter *it;
    int book, chapter, verse;
    const char *text;

    /* Every form of the number ends in its digits: "<WH430>", "<WH0430>",
     * "href='sH430'". The parser then checks the candidates. */
    g_snprintf(like, sizeof like, "%%%d%%", number);
    it = ks_module_iter_new(m, like);
    while (it && ks_module_iter_next(it, &book, &chapter, &verse, &text, NULL)) {
        StrongMatch sm = { testament, number, FALSE };

        if (!in_range(opt, book))
            continue;
        ks_gbf_parse(text, m->css, strong_cb, &sm);
        if (sm.found)
            add_hit(hits, book, chapter, verse);
    }
    ks_module_iter_free(it);
    return hits;
}
