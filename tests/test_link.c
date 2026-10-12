/* test_link.c - MySword module links, with hrefs from real modules (TSK,
 * KJC, Mounce, HL, SEC, ThCBn, GreekGrammar, berean-interlinear,
 * sample-journal). */
#include "books.h"
#include "link.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *href, KsLinkKind kind, const char *module, const char *key, const char *anchor)
{
    KsLink l;

    ks_link_parse(href, &l);
    if (l.kind != kind || g_strcmp0(l.module, module) != 0 || g_strcmp0(l.key, key) != 0 ||
        g_strcmp0(l.anchor, anchor) != 0) {
        printf("FAIL link %s\n  want: %d [%s] [%s] [%s]\n  got:  %d [%s] [%s] [%s]\n", href, kind, module, key,
               anchor, l.kind, l.module, l.key, l.anchor);
        failures++;
    }
    ks_link_clear(&l);
}

static void expect_ref(const char *href, int book, int chapter, int verse, int verse_end)
{
    KsLink l;

    ks_link_parse(href, &l);
    if (l.kind != KS_LINK_BIBLE || l.ref.book != book || l.ref.chapter != chapter || l.ref.verse != verse ||
        l.ref.verse_end != verse_end) {
        printf("FAIL ref %s\n  want: %d %d:%d-%d\n  got:  kind %d, %d %d:%d-%d\n", href, book, chapter, verse,
               verse_end, l.kind, l.ref.book, l.ref.chapter, l.ref.verse, l.ref.verse_end);
        failures++;
    }
    ks_link_clear(&l);
}

static void expect_strong(const char *in, const char *want)
{
    char *got = ks_strong_normalize(in);

    if (g_strcmp0(got, want) != 0) {
        printf("FAIL strong %s: want [%s] got [%s]\n", in, want, got);
        failures++;
    }
    g_free(got);
}

int main(void)
{
    ks_books_set_lang("pt"); /* links are English even so */

    expect_ref("#b1.1.1", 1, 1, 1, 0);
    expect_ref("#b1.8.3-1.8.4", 1, 8, 3, 4);
    expect_ref("#b23.40.12-23.40.14", 23, 40, 12, 14);
    expect_ref("b2.4.14", 2, 4, 14, 0);
    expect_ref("bLam 1:8", 25, 1, 8, 0);
    expect_ref("bExod. 4:14", 2, 4, 14, 0);
    expect_ref("bLk. 1:15", 42, 1, 15, 0);
    expect_ref("b2 Cor 7:4", 47, 7, 4, 0);
    expect_ref("bRev. 9:11", 66, 9, 11, 0);
    expect_ref("b1 Th 1:6", 52, 1, 6, 0);
    expect_ref("#bProv 1:20-23", 20, 1, 20, 23);
    expect_ref("#bPsalm 37:28", 19, 37, 28, 0);
    expect_ref("#bJos 1:5", 6, 1, 5, 0);
    expect_ref("#bJohn 15:4", 43, 15, 4, 0);
    expect_ref("#bJn 3:16", 43, 3, 16, 0);
    expect_ref("#bJohn 3:16 KJV", 43, 3, 16, 0);
    expect_ref("bActs 7:40", 44, 7, 40, 0);
    expect_ref("B:290 23:5", 23, 23, 5, 0);
    expect_ref("B:10 26:27-29", 1, 26, 27, 29);
    expect_ref("bI Timothy 6:11", 54, 6, 11, 0);
    expect_ref("bICO 10:33", 46, 10, 33, 0);
    expect_ref("bITH 5:23", 52, 5, 23, 0);
    expect_ref("bIsa 53:5", 23, 53, 5, 0);
    expect_ref("#bRth 3:11", 8, 3, 11, 0);
    expect_ref("b4.15.8.BHSE", 4, 15, 8, 0);
    expect_ref("b14.11:15", 14, 11, 15, 0);

    expect("sH1", KS_LINK_STRONG, NULL, "H1", NULL);
    expect("#sG1000", KS_LINK_STRONG, NULL, "G1000", NULL);
    expect("sH0430", KS_LINK_STRONG, NULL, "H430", NULL);
    expect("#dH24", KS_LINK_DICT, NULL, "H24", NULL);
    expect("d-HL H1", KS_LINK_DICT, "HL", "H1", NULL);
    expect("d-*אָב", KS_LINK_DICT, NULL, "אָב", NULL);
    expect("#d[About:Abbreviations]#BM1643-", KS_LINK_DICT, NULL, "[About:Abbreviations]", "BM1643-");
    expect("dSHE797#in", KS_LINK_DICT, NULL, "SHE797", "in");
    expect("#dneck of the woods", KS_LINK_DICT, NULL, "neck of the woods", NULL);
    expect("d-uBDB BDB10022", KS_LINK_DICT, "uBDB", "BDB10022", NULL);
    expect("mprep", KS_LINK_MORPH, NULL, "prep", NULL);
    expect("m/lἈαρών", KS_LINK_MORPH, NULL, "Ἀαρών", NULL);
    expect("#j ความรู้ทั่วไป", KS_LINK_ENTRY, NULL, "ความรู้ทั่วไป", NULL);
    expect("j Adjective ascriptive", KS_LINK_ENTRY, NULL, "Adjective ascriptive", NULL);
    expect("k-HebrewGrammar Gender Feminine", KS_LINK_BOOK, "HebrewGrammar", "Gender Feminine", NULL);
    expect("https://ugg.readthedocs.io/", KS_LINK_URL, NULL, "https://ugg.readthedocs.io/", NULL);
    expect("//readthedocs.org/x", KS_LINK_URL, NULL, "https://readthedocs.org/x", NULL);
    expect("p3.jpg?mod=k-Timeline", KS_LINK_PICTURE, "Timeline", "3.jpg", NULL);
    expect("rActive%20voice", KS_LINK_POPUP, NULL, "Active voice", NULL);
    expect("rHewlett, John Grigg, D.D. Bible difficulties", KS_LINK_POPUP, NULL,
           "Hewlett, John Grigg, D.D. Bible difficulties", NULL);
    expect("rpos=\"n-m\" pron=\"awb\"", KS_LINK_POPUP, NULL, "pos=\"n-m\" pron=\"awb\"", NULL); /* HL */
    expect("sXYZ", KS_LINK_NONE, NULL, NULL, NULL);
    {
        KsLink l;

        ks_link_parse("b27.6.3.BHSEk", &l);
        if (g_strcmp0(l.module, "BHSEk") != 0 || l.ref.book != 27 || l.ref.verse != 3) {
            printf("FAIL link b27.6.3.BHSEk: module [%s], %d %d:%d\n", l.module, l.ref.book, l.ref.chapter,
                   l.ref.verse);
            failures++;
        }
        ks_link_clear(&l);
    }
    expect("", KS_LINK_NONE, NULL, NULL, NULL);

    expect_strong("H0430", "H430");
    expect_strong("WG2316", "G2316");
    expect_strong("h1254a", "H1254a");
    expect_strong("G2697.1", "G2697.1");
    expect_strong("H0", "H0");
    expect_strong("love", NULL);
    expect_strong("H12x3", NULL);

    if (failures)
        printf("test_link: %d failure(s)\n", failures);
    else
        printf("test_link: OK\n");
    return failures != 0;
}
