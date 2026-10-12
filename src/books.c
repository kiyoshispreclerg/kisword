/* books.c - book names and abbreviations (see books.h). */
#include "books.h"

#include <string.h>

typedef struct {
    const char *name;
    const char *abbrev;
    const char *aliases; /* extra exact-match keys, '|'-separated, or NULL */
} KsBookName;

static const KsBookName books_pt[KS_NBOOKS] = {
    { "Gênesis", "Gn", "Gen" },
    { "Êxodo", "Êx", "Ex" },
    { "Levítico", "Lv", NULL },
    { "Números", "Nm", NULL },
    { "Deuteronômio", "Dt", NULL },
    { "Josué", "Js", NULL },
    { "Juízes", "Jz", NULL },
    { "Rute", "Rt", NULL },
    { "1 Samuel", "1Sm", NULL },
    { "2 Samuel", "2Sm", NULL },
    { "1 Reis", "1Rs", NULL },
    { "2 Reis", "2Rs", NULL },
    { "1 Crônicas", "1Cr", NULL },
    { "2 Crônicas", "2Cr", NULL },
    { "Esdras", "Ed", NULL },
    { "Neemias", "Ne", NULL },
    { "Ester", "Et", NULL },
    { "Jó", "Jó", NULL },
    { "Salmos", "Sl", "Salmo" },
    { "Provérbios", "Pv", NULL },
    { "Eclesiastes", "Ec", NULL },
    { "Cânticos", "Ct", "Cantares|Cântico dos Cânticos" },
    { "Isaías", "Is", NULL },
    { "Jeremias", "Jr", NULL },
    { "Lamentações", "Lm", NULL },
    { "Ezequiel", "Ez", NULL },
    { "Daniel", "Dn", NULL },
    { "Oseias", "Os", "Oséias" },
    { "Joel", "Jl", NULL },
    { "Amós", "Am", NULL },
    { "Obadias", "Ob", NULL },
    { "Jonas", "Jn", NULL },
    { "Miqueias", "Mq", "Miquéias" },
    { "Naum", "Na", NULL },
    { "Habacuque", "Hc", NULL },
    { "Sofonias", "Sf", NULL },
    { "Ageu", "Ag", NULL },
    { "Zacarias", "Zc", NULL },
    { "Malaquias", "Ml", NULL },
    { "Mateus", "Mt", NULL },
    { "Marcos", "Mc", NULL },
    { "Lucas", "Lc", NULL },
    { "João", "Jo", NULL },
    { "Atos", "At", NULL },
    { "Romanos", "Rm", NULL },
    { "1 Coríntios", "1Co", NULL },
    { "2 Coríntios", "2Co", NULL },
    { "Gálatas", "Gl", NULL },
    { "Efésios", "Ef", NULL },
    { "Filipenses", "Fp", NULL },
    { "Colossenses", "Cl", NULL },
    { "1 Tessalonicenses", "1Ts", NULL },
    { "2 Tessalonicenses", "2Ts", NULL },
    { "1 Timóteo", "1Tm", NULL },
    { "2 Timóteo", "2Tm", NULL },
    { "Tito", "Tt", NULL },
    { "Filemom", "Fm", "Filemon" },
    { "Hebreus", "Hb", NULL },
    { "Tiago", "Tg", NULL },
    { "1 Pedro", "1Pe", NULL },
    { "2 Pedro", "2Pe", NULL },
    { "1 João", "1Jo", NULL },
    { "2 João", "2Jo", NULL },
    { "3 João", "3Jo", NULL },
    { "Judas", "Jd", NULL },
    { "Apocalipse", "Ap", NULL },
};

static const KsBookName books_en[KS_NBOOKS] = {
    { "Genesis", "Gen", "Gn" },
    { "Exodus", "Exod", "Ex" },
    { "Leviticus", "Lev", NULL },
    { "Numbers", "Num", NULL },
    { "Deuteronomy", "Deut", NULL },
    { "Joshua", "Josh", NULL },
    { "Judges", "Judg", "Jdg" },
    { "Ruth", "Ruth", "Rth" },
    { "1 Samuel", "1Sam", NULL },
    { "2 Samuel", "2Sam", NULL },
    { "1 Kings", "1Kgs", NULL },
    { "2 Kings", "2Kgs", NULL },
    { "1 Chronicles", "1Chr", NULL },
    { "2 Chronicles", "2Chr", NULL },
    { "Ezra", "Ezra", NULL },
    { "Nehemiah", "Neh", NULL },
    { "Esther", "Esth", NULL },
    { "Job", "Job", NULL },
    { "Psalms", "Ps", "Psalm|Psa" },
    { "Proverbs", "Prov", NULL },
    { "Ecclesiastes", "Eccl", NULL },
    { "Song of Solomon", "Song", "Song of Songs|SoS|Sng" },
    { "Isaiah", "Isa", NULL },
    { "Jeremiah", "Jer", NULL },
    { "Lamentations", "Lam", NULL },
    { "Ezekiel", "Ezek", "Ezk" },
    { "Daniel", "Dan", NULL },
    { "Hosea", "Hos", NULL },
    { "Joel", "Joel", NULL },
    { "Amos", "Amos", NULL },
    { "Obadiah", "Obad", NULL },
    { "Jonah", "Jonah", NULL },
    { "Micah", "Mic", NULL },
    { "Nahum", "Nah", NULL },
    { "Habakkuk", "Hab", NULL },
    { "Zephaniah", "Zeph", NULL },
    { "Haggai", "Hag", NULL },
    { "Zechariah", "Zech", NULL },
    { "Malachi", "Mal", NULL },
    { "Matthew", "Matt", "Mt" },
    { "Mark", "Mark", "Mk|Mrk" },
    { "Luke", "Luke", "Lk|Luk" },
    { "John", "John", "Jn|Jhn" },
    { "Acts", "Acts", NULL },
    { "Romans", "Rom", NULL },
    { "1 Corinthians", "1Cor", NULL },
    { "2 Corinthians", "2Cor", NULL },
    { "Galatians", "Gal", NULL },
    { "Ephesians", "Eph", NULL },
    { "Philippians", "Phil", "Php" },
    { "Colossians", "Col", NULL },
    { "1 Thessalonians", "1Thess", NULL },
    { "2 Thessalonians", "2Thess", NULL },
    { "1 Timothy", "1Tim", NULL },
    { "2 Timothy", "2Tim", NULL },
    { "Titus", "Titus", NULL },
    { "Philemon", "Phlm", "Phm" },
    { "Hebrews", "Heb", NULL },
    { "James", "Jas", NULL },
    { "1 Peter", "1Pet", NULL },
    { "2 Peter", "2Pet", NULL },
    { "1 John", "1John", "1Jn|1Jhn" },
    { "2 John", "2John", "2Jn|2Jhn" },
    { "3 John", "3John", "3Jn|3Jhn" },
    { "Jude", "Jude", NULL },
    { "Revelation", "Rev", "Rv|Apocalypse" },
};

static const KsBookName *cur = books_en;
static const KsBookName *other = books_pt;

void ks_books_set_lang(const char *lang)
{
    if (!lang || !*lang) {
        const char *const *langs = g_get_language_names();

        lang = langs && langs[0] ? langs[0] : "en";
    }
    if (g_ascii_strncasecmp(lang, "pt", 2) == 0) {
        cur = books_pt;
        other = books_en;
    } else {
        cur = books_en;
        other = books_pt;
    }
}

static const char *generic_name(int book)
{
    static char *names[256];

    if (book < 0 || book > 255)
        return "?";
    if (!names[book])
        names[book] = g_strdup_printf("%d", book);
    return names[book];
}

const char *ks_book_name(int book)
{
    return book >= 1 && book <= KS_NBOOKS ? cur[book - 1].name : generic_name(book);
}

const char *ks_book_abbrev(int book)
{
    return book >= 1 && book <= KS_NBOOKS ? cur[book - 1].abbrev : generic_name(book);
}

char *ks_fold_key(const char *s, gssize len, gboolean strip_marks)
{
    char *cf = g_utf8_casefold(s, len);
    char *n = g_utf8_normalize(cf, -1, strip_marks ? G_NORMALIZE_NFD : G_NORMALIZE_NFC);
    GString *out;

    g_free(cf);
    if (!n)
        return g_strdup("");
    out = g_string_sized_new(strlen(n));
    for (const char *p = n; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);

        if (g_unichar_isspace(c) || c == '.')
            continue;
        if (strip_marks && g_unichar_type(c) == G_UNICODE_NON_SPACING_MARK)
            continue;
        g_string_append_unichar(out, c);
    }
    g_free(n);
    return g_string_free(out, FALSE);
}

static gboolean key_equals(const char *candidate, gssize len, const char *key, gboolean strip)
{
    char *k = ks_fold_key(candidate, len, strip);
    gboolean eq = strcmp(k, key) == 0;

    g_free(k);
    return eq;
}

static gboolean book_matches(const KsBookName *b, const char *key, gboolean strip, gboolean prefix)
{
    if (prefix) {
        char *k = ks_fold_key(b->name, -1, TRUE);
        gboolean ok = g_str_has_prefix(k, key);

        g_free(k);
        return ok;
    }
    if (key_equals(b->name, -1, key, strip) || key_equals(b->abbrev, -1, key, strip))
        return TRUE;
    for (const char *a = b->aliases; a && *a;) {
        const char *bar = strchr(a, '|');
        gssize n = bar ? bar - a : (gssize)strlen(a);

        if (key_equals(a, n, key, strip))
            return TRUE;
        a = bar ? bar + 1 : NULL;
    }
    return FALSE;
}

/* Three passes, each over the current language first and then the other:
 * exact with accents ("Jó" is Job, "Jo" is João), exact without accents
 * ("genesis"), and finally prefix of the full name ("mat", "apoc"). */
static int lookup(const char *s, gssize len, const KsBookName *first, const KsBookName *second)
{
    char *kc = ks_fold_key(s, len, FALSE);
    char *ks = ks_fold_key(s, len, TRUE);
    const KsBookName *tabs[2] = { first, second };
    gboolean has_letter = FALSE;
    int found = 0;

    for (const char *p = ks; *p; p++)
        if (!g_ascii_isdigit(*p))
            has_letter = TRUE;

    for (int pass = 0; has_letter && pass < 3 && !found; pass++)
        for (int t = 0; t < 2 && !found; t++)
            for (int b = 0; b < KS_NBOOKS && !found; b++)
                if (book_matches(&tabs[t][b], pass == 0 ? kc : ks, pass > 0, pass == 2))
                    found = b + 1;

    g_free(kc);
    g_free(ks);
    return found;
}

int ks_book_lookup(const char *s, gssize len)
{
    return lookup(s, len, cur, other);
}

int ks_book_lookup_en(const char *s, gssize len)
{
    return lookup(s, len, books_en, books_pt);
}
