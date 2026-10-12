/* test_boxes.c - verses as box trees, from real modules (OpenHB, BHSE,
 * OpenGNTOTLA), checked through ks_box_dump(). */
#include "boxes.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *in, const KsStyleSheet *css, const char *want)
{
    KsBox *b = ks_boxes_build(in, css);
    char *got = b ? ks_box_dump(b) : g_strdup("(none)");

    if (strcmp(got, want) != 0) {
        printf("FAIL boxes: %s\n  want: %s\n  got:  %s\n", in, want, got);
        failures++;
    }
    g_free(got);
    ks_box_free(b);
}

int main(void)
{
    KsStyleSheet *css;

    /* Plain verse: no boxes. */
    expect("In the beginning<WH7225> God", NULL, "(none)");

    /* OpenHB: the word's Strong's and morphology become layers under it. */
    expect("<Q><H>בְּ<WH9003><WTprep l=\"בְּ\"><h><X>bĕ<x><X>bᵊ<x><E>in<e><E>In<e><q> "
           "<Q><H>רֵאשִׁ֖ית <WH7225><WTsubs.f.sg.a l=\"רֵאשִׁית\"><h><E>the beginning<e><q>",
           NULL,
           "{בְּ|S:H9003|M:prep|bĕ|bᵊ|in|In}_{רֵאשִׁ֖ית|S:H7225|M:subs.f.sg.a|the_beginning}");

    /* BHSE after its rules: <br> lines inside the word, Strong's in a later
     * line stays where it is. */
    expect("<Q><a class='text' href='d-BHSE 1'>בְּ</a><X>bᵊ<x><E>בְּ<br> | in<br> | "
           "<a class='nd' href='sH7225'>H7225</a><e><q>·<Q>x<q>",
           NULL, "{בְּ|bᵊ|בְּ||_in||_S:H7225}·{x}"); /* the module's own "|" */

    /* OpenGNTOTLA: nested bordered boxes; a note marker. */
    css = ks_style_sheet_new("div.c {border: solid gray 1px; display: table;}\n"
                             "div.d {border: solid gray 1px; display: inline-block;}\n"
                             "div.w {display: inline-block; text-align: center;}\n");
    expect("<div class='c'><b>1:1.w1-w5</b><br><div class='d'><div class='d'>1:1.w1-w2<br>adjunct</div>"
           "<div class='w z'><RF q=w1>Lemma: ἐν<Rf><br>specifier<br>Ἐν<br>in</div></div></div>",
           css, "<#1:1.w1-w5|[#[#1:1.w1-w2|adjunct][N:w1|specifier|Ἐν|in]]>");
    /* Unclosed boxes are closed at the end. */
    expect("<div class='d'>a<Q>b", css, "[#a{b}]");
    ks_style_sheet_free(css);

    if (failures)
        printf("test_boxes: %d failure(s)\n", failures);
    else
        printf("test_boxes: OK\n");
    return failures != 0;
}
