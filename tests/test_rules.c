/* test_rules.c - VerseRules and the CustomCSS subset, with rules and CSS
 * taken from real modules (HiSB, ACV, kjv, tr, OpenGNTbe, OpenHB-LA). */
#include "gbf.h"
#include "rules.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void expect_rules(const char *rules, const char *in, const char *want)
{
    int bad;
    KsRules *r = ks_rules_new(rules, &bad);
    char *got = ks_rules_apply(r, in);
    const char *out = got ? got : in;

    if (strcmp(out, want) != 0) {
        printf("FAIL rules on: %s\n  want: %s\n  got:  %s\n", in, want, out);
        failures++;
    }
    g_free(got);
    ks_rules_free(r);
}

/* The style of the first text span of src, as "{bits}#color". */
static void style_cb(const KsSpan *sp, void *data)
{
    GString *g = data;

    if (sp->kind == KS_SPAN_TEXT && !g->len)
        g_string_printf(g, "%x%s", sp->style, sp->color ? sp->color : "");
}

static char *first_style(const KsStyleSheet *css, const char *src)
{
    GString *g = g_string_new(NULL);

    ks_gbf_parse(src, css, style_cb, g);
    return g_string_free(g, FALSE);
}

static void expect_style(const KsStyleSheet *css, const char *src, const char *want)
{
    char *got = first_style(css, src);

    if (strcmp(got, want) != 0) {
        printf("FAIL style of: %s\n  want: %s\n  got:  %s\n", src, want, got);
        failures++;
    }
    g_free(got);
}

int main(void)
{
    int bad;
    KsRules *r;
    KsStyleSheet *css;
    char *plain;

    /* HiSB: groups, optional group, quotes in the replacement. */
    expect_rules("<K>(.*?)( )?<k>\t<span class='red' dir='rtl'>[</span>$1<span class='red' dir='rtl'>]</span>$2",
                 "a <K>word <k>b",
                 "a <span class='red' dir='rtl'>[</span>word<span class='red' dir='rtl'>]</span> b");
    /* ACV: four fields (on, off, name); the "on" form is used. Two rules in order. */
    expect_rules("<G>(.*?)<g>\t<span class='blue'>$1</span>\t$1\tGreek in Blue\n"
                 "((?:<W[^>]+>)+)(<E>.*?<e>)\t$2$1\t$1$2\tGreek before Strong/Morph",
                 "<G>logos<g><WG3056><E>word<e>",
                 "<span class='blue'>logos</span><E>word<e><WG3056>");
    /* kjv: replacement of a single space, CRLF line ends. */
    expect_rules("(<W[GH]\\d+x>)\t \r\n", "a<WG1234x>b", "a b");
    /* tr: lookbehind/lookahead, and a rule with nothing to replace with. */
    expect_rules("(?<=»)([^»«]+)(?=»)\t»$1«\n<VX(.*?)>", "»a»<VX1>", "»»a«»");
    /* Backslashes in a replacement are literal (.NET). */
    expect_rules("x\t\\n$$", "x", "\\n$");
    expect_rules("(a)\t\\$1", "a", "\\a");

    r = ks_rules_new("([unclosed\tx\nok\tOK\n\n", &bad);
    CHECK(r && ks_rules_count(r) == 1 && bad == 1);
    CHECK(ks_rules_apply(r, "nothing here") == NULL); /* unchanged */
    ks_rules_free(r);
    CHECK(ks_rules_new("", NULL) == NULL && ks_rules_new(NULL, NULL) == NULL);

    /* Options: ETCBC-like rules, two sharing a name (tr's "Variant"). */
    r = ks_rules_new("(\u05b8)\t$1\t\tHebrew vowels\n"
                     "<V1>(.*?)<v1>\t{$1}\t$1\tVariant\n"
                     "<V2>(.*?)<v2>\t[$1]\t\tVariant\n"
                     "x\ty", &bad);
    if (!r) {
        printf("FAIL: rules with options not loaded\n");
        return 1;
    }
    CHECK(ks_rules_n_options(r) == 2 && bad == 0);
    CHECK(strcmp(ks_rules_option_name(r, 0), "Hebrew vowels") == 0 && ks_rules_option_on(r, 0));
    {
        char *out = ks_rules_apply(r, "\xd7\x91\xd6\xb8 <V1>a<v1><V2>b<v2> x");

        CHECK(out && strcmp(out, "\xd7\x91\xd6\xb8 {a}[b] y") == 0); /* identity "on": untouched */
        g_free(out);
        ks_rules_set_option(r, 0, FALSE);
        ks_rules_set_option(r, 1, FALSE);
        CHECK(!ks_rules_option_on(r, 1));
        out = ks_rules_apply(r, "\xd7\x91\xd6\xb8 <V1>a<v1><V2>b<v2> x");
        CHECK(out && strcmp(out, "\xd7\x91 a y") == 0); /* vowel and second variant gone */
        g_free(out);
    }
    ks_rules_free(r);
    CHECK(ks_rules_new("((\tx", &bad) == NULL && bad == 1);

    /* Colors. */
    CHECK(strcmp(ks_style_color("red", -1), "#cc2222") == 0);
    CHECK(strcmp(ks_style_color("@darken(@red,5%)", -1), "#cc2222") == 0);
    CHECK(strcmp(ks_style_color(" @adjust(@blue,-30,-20%,-5%)", -1), "#2a5db0") == 0);
    CHECK(strcmp(ks_style_color("#C22", -1), "#cc2222") == 0);
    CHECK(strcmp(ks_style_color("#1F7A5C", -1), "#1f7a5c") == 0);
    CHECK(ks_style_color("black", -1) == NULL && ks_style_color("#fff", -1) == NULL);
    CHECK(ks_style_color("rgb(1,2,3)", -1) == NULL && ks_style_color("#12", -1) == NULL);

    /* CustomCSS: OpenGNTbe and OpenHB-LA, trimmed, plus a stray script. */
    css = ks_style_sheet_new(
        "span.c:hover {background-color: @adjust(@blue,-50,-10%,30%);}\n"
        "a.l,a.c {text-decoration: none; color:@adjust(@blue,-30,-20%,-5%);}\n"
        "gloss { vertical-align: super; color: @darken(@red,5%);}\n"
        "span.s {display: block; background-color: @gray;}\n"
        "/* comment { color: red } */ det {font-weight: bold;} undet {font-style: italic;}\n"
        "a { color: green } .gone { display: none }\n"
        "@font-face {font-family: 'KoineGreek'; src: url('KoineGreek.ttf');}\n"
        ".greek{font-family:'KoineGreek' !important;}\n"
        "</style><script> window.addEventListener('load', function(event) { if (x) y = 1; }); </script><style>");
    expect_style(css, "<span class='c'>x</span>", "0");             /* only :hover; a.c is not span.c */
    expect_style(css, "<a class='c' href='d-1'>x</a>", "0#2a5db0");
    expect_style(css, "<gloss>x</gloss>", "20#cc2222");             /* sup + color */
    expect_style(css, "<det>x</det>", "10");
    expect_style(css, "<a href='x'>x</a>", "0");                    /* plain elements untouched */
    expect_style(css, "<span class='red s'>x</span>", "0#cc2222");  /* built-in + custom */
    expect_style(css, "<span class='greek'>x</span>", "0");
    plain = ks_gbf_plain("a<span class='gone'>b</span>c<span class='s'>d</span>e", css);
    CHECK(strcmp(plain, "ac d e") == 0); /* hidden dropped, block -> break */
    g_free(plain);
    CHECK(ks_style_sheet_element(css, "gloss", -1) && !ks_style_sheet_element(css, "a", -1));
    CHECK(ks_style_sheet_class(css, NULL, "GREEN", -1) && ks_style_sheet_class(NULL, "span", "red", -1));
    ks_style_sheet_free(css);

    plain = ks_gbf_markup("<i>a &amp; b</i><br><span class='red'>c</span><p>x</p>");
    if (strcmp(plain, "<i>a &amp; b</i>\n<span foreground='#cc2222'>c</span>x") != 0) {
        printf("FAIL markup: %s\n", plain);
        failures++;
    }
    g_free(plain);

    if (failures)
        printf("test_rules: %d failure(s)\n", failures);
    else
        printf("test_rules: OK\n");
    return failures != 0;
}
