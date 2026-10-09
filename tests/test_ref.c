/* test_ref.c - reference parsing and book lookup in both languages. */
#include "books.h"
#include "ref.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *in, gboolean ok, int book, int chapter, int verse, int verse_end)
{
    KsRef r;
    gboolean got = ks_ref_parse(in, &r);

    if (got != ok || (ok && (r.book != book || r.chapter != chapter || r.verse != verse || r.verse_end != verse_end))) {
        printf("FAIL \"%s\": want %s %d %d:%d-%d, got %s %d %d:%d-%d\n", in, ok ? "ok" : "error", book, chapter,
               verse, verse_end, got ? "ok" : "error", r.book, r.chapter, r.verse, r.verse_end);
        failures++;
    }
}

static void expect_format(int b, int c, int v, int e, const char *want)
{
    char *got = ks_ref_format(b, c, v, e);

    if (strcmp(got, want) != 0) {
        printf("FAIL format: want %s, got %s\n", want, got);
        failures++;
    }
    g_free(got);
}

int main(void)
{
    ks_books_set_lang("pt");
    expect("Jo 3:16", TRUE, 43, 3, 16, 0);
    expect("jo 3.16", TRUE, 43, 3, 16, 0);
    expect("João 3", TRUE, 43, 3, 0, 0);
    expect("joao 3", TRUE, 43, 3, 0, 0);
    expect("Jó 1", TRUE, 18, 1, 0, 0);
    expect("1Co 13:4-7", TRUE, 46, 13, 4, 7);
    expect("1 Coríntios 13", TRUE, 46, 13, 0, 0);
    expect("1 corintios 13", TRUE, 46, 13, 0, 0);
    expect("gn1", TRUE, 1, 1, 0, 0);
    expect("Gênesis", TRUE, 1, 1, 0, 0);
    expect("Êx 20:1–17", TRUE, 2, 20, 1, 17);
    expect("ex 20", TRUE, 2, 20, 0, 0);
    expect("Sl 119:105", TRUE, 19, 119, 105, 0);
    expect("Jn 1", TRUE, 32, 1, 0, 0);          /* Jonas in Portuguese */
    expect("Cantares 2", TRUE, 22, 2, 0, 0);
    expect("apoc 22:21", TRUE, 66, 22, 21, 0);  /* prefix */
    expect("1 jo 4:8", TRUE, 62, 4, 8, 0);
    expect("John 3:16", TRUE, 43, 3, 16, 0);    /* English accepted too */
    expect("Rev 1", TRUE, 66, 1, 0, 0);
    expect("3:16", TRUE, 0, 3, 16, 0);          /* no book: current one */
    expect("  Mt 5 : 3 - 12  ", TRUE, 40, 5, 3, 12);
    expect("Jo 3:16-4:2", TRUE, 43, 3, 16, 0);  /* cross-chapter: start only */
    expect("Jo 3:16-10", TRUE, 43, 3, 16, 0);   /* backwards range ignored */
    expect("Xyz 3", FALSE, 0, 0, 0, 0);
    expect("Jo 3:", FALSE, 0, 0, 0, 0);
    expect("Jo 3 abc", FALSE, 0, 0, 0, 0);
    expect("", FALSE, 0, 0, 0, 0);
    expect("5", TRUE, 0, 5, 0, 0);             /* chapter of the current book */
    expect_format(43, 3, 16, 0, "Jo 3:16");
    expect_format(46, 13, 4, 7, "1Co 13:4-7");
    expect_format(1, 1, 0, 0, "Gn 1");

    ks_books_set_lang("en");
    expect("Jn 1", TRUE, 43, 1, 0, 0);          /* John in English */
    expect("Gen 1:1", TRUE, 1, 1, 1, 0);
    expect("Jo 3:16", TRUE, 43, 3, 16, 0);      /* falls back to Portuguese */
    expect("song 2", TRUE, 22, 2, 0, 0);
    expect_format(43, 3, 16, 0, "John 3:16");
    if (strcmp(ks_book_name(67), "67") != 0) {
        printf("FAIL generic book name: %s\n", ks_book_name(67));
        failures++;
    }

    if (failures)
        printf("test_ref: %d failure(s)\n", failures);
    else
        printf("test_ref: OK\n");
    return failures != 0;
}
