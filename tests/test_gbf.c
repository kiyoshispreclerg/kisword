/* test_gbf.c - the verse-text parser against known inputs, most of them
 * verbatim from the KJV and RVP modules. */
#include "gbf.h"

#include <stdio.h>
#include <string.h>

static int failures;

/* One token per span: kind letter, {style flags}, @indent, (mark), 'text'. */
static void ser(const KsSpan *sp, void *data)
{
    static const char kinds[] = "TSMNVPLU";
    static const char flags[] = "iroubp_t";
    GString *g = data;

    if (g->len)
        g_string_append_c(g, ' ');
    g_string_append_c(g, kinds[sp->kind]);
    if (sp->style) {
        g_string_append_c(g, '{');
        for (int i = 0; flags[i]; i++)
            if (sp->style & (1u << i))
                g_string_append_c(g, flags[i]);
        g_string_append_c(g, '}');
    }
    if (sp->indent)
        g_string_append_printf(g, "@%d", sp->indent);
    if (sp->mark)
        g_string_append_printf(g, "(%.*s)", sp->mark_len, sp->mark);
    if (sp->len)
        g_string_append_printf(g, "'%.*s'", sp->len, sp->text);
}

static void expect(const char *in, const char *want)
{
    GString *g = g_string_new(NULL);

    ks_gbf_parse(in, ser, g);
    if (strcmp(g->str, want) != 0) {
        printf("FAIL parse: %s\n  want: %s\n  got:  %s\n", in, want, g->str);
        failures++;
    }
    g_string_free(g, TRUE);
}

static void expect_plain(const char *in, const char *want)
{
    char *got = ks_gbf_plain(in);

    if (strcmp(got, want) != 0) {
        printf("FAIL plain: %s\n  want: %s\n  got:  %s\n", in, want, got);
        failures++;
    }
    g_free(got);
}

int main(void)
{
    /* Plain text, Strong's numbers after words, several on one word. */
    expect("No princípio, criou Deus os céus e a terra.", "T'No princípio, criou Deus os céus e a terra.'");
    expect("In the beginning<WH7225> God<WH430> created<WH1254><WH853> the",
           "T'In the beginning' S'H7225' T' God' S'H430' T' created' S'H1254' S'H853' T' the'");

    /* Italic and paragraph end (Gn 1:2). */
    expect("darkness<WH2822> <FI>was<Fi> upon.<CM>", "T'darkness' S'H2822' T' ' T{i}'was' T' upon.' P");

    /* Words of Jesus interleaved with Strong's (Jo 3:16). */
    expect("<FR>For<Fr><WG1063><FR> God<Fr><WG2316>", "T{r}'For' S'G1063' T{r}' God' S'G2316'");

    /* Footnote with HTML inside (Rm 16:27), and with a q= marker. */
    expect("Amen<WG281>. <RF>Written to the Romans, <i>and sent</i> by Phebe.<Rf><CM>",
           "T'Amen' S'G281' T'. ' N'Written to the Romans, <i>and sent</i> by Phebe.' P");
    expect("word<RF q=*>note<Rf> after", "T'word' N(*)'note' T' after'");
    expect("word<RF>unclosed note", "T'word' N'unclosed note'");

    /* OT quotation, section title, line break, indent. */
    expect("<FO>Behold<Fo><WG2400>", "T{o}'Behold' S'G2400'");
    expect("<TS>The Title<Ts>Text", "T{t}'The Title' T'Text'");
    expect("<PI2>one<CL>two<CM>three", "T@2'one' L@2 T@2'two' P@2 T'three'");

    /* Merged verse marker from the RVP (1Sm 17:58). */
    expect("Belém. <v>17:59</v> Vive", "T'Belém. ' V'17:59' T' Vive'");

    /* Morphology, unknown tags, HTML. */
    expect("word<WTV-PAI-3S>", "T'word' M'V-PAI-3S'");
    expect("a<XX>b", "T'a' U'XX' T'b'");
    expect("a<br/>b<b>c</b><span class='x'>d</span>", "T'a' L T'b' T{b}'c' T'd'");

    /* Entities and stray '<'. */
    expect("a &amp; b &#233; &lt;x&gt; &bogus; &", "T'a ' T'&' T' b ' T'é' T' ' T'<' T'x' T'>' T' &bogus; &'");
    expect("1 < 2 and <FI>x<Fi>", "T'1 < 2 and ' T{i}'x'");
    expect("", "");

    expect_plain("In the beginning<WH7225> God<WH430> created<WH1254><WH853> the heaven.",
                 "In the beginning God created the heaven.");
    expect_plain("<TS>Title<Ts>Text<RF>note<Rf> more.<CM>", "Text more.");
    expect_plain("Belém. <v>17:59</v> Vive", "Belém. Vive");
    expect_plain("one<CM>two<CL>three", "one two three");

    if (failures)
        printf("test_gbf: %d failure(s)\n", failures);
    else
        printf("test_gbf: OK\n");
    return failures != 0;
}
