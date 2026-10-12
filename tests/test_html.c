/* test_html.c - module description HTML, with fragments of real Comments
 * (ARC2009, berean-bsb). */
#include "gbf.h"
#include "html.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *in, const char *want)
{
    char *got = ks_html_plain(in);

    if (strcmp(got, want) != 0) {
        printf("FAIL html: %s\n  want: [%s]\n  got:  [%s]\n", in, want, got);
        failures++;
    }
    g_free(got);
}

/* One token per run: {style hex}'text'. */
static void ser(const char *text, int len, unsigned style, void *data)
{
    GString *g = data;

    if (style)
        g_string_append_printf(g, "{%x}", style);
    g_string_append_printf(g, "'%.*s'", len, text);
}

static void expect_runs(const char *in, const char *want)
{
    GString *g = g_string_new(NULL);

    ks_html_parse(in, ser, g);
    if (strcmp(g->str, want) != 0) {
        printf("FAIL runs: %s\n  want: %s\n  got:  %s\n", in, want, g->str);
        failures++;
    }
    g_string_free(g, TRUE);
}

/* ks_html_render() runs as "text|style|href|color|indent|align|heading",
 * image and anchor runs as "IMG:src" and "#name". */
static void ser_run(const KsHtmlRun *r, void *data)
{
    GString *g = data;

    if (g->len)
        g_string_append(g, " / ");
    if (r->image)
        g_string_append_printf(g, "IMG:%s", r->image);
    else if (r->anchor)
        g_string_append_printf(g, "#%s", r->anchor);
    else
        g_string_append_printf(g, "%.*s|%x|%s|%s|%d|%d|%d", r->len, r->text, r->style, r->href ? r->href : "",
                               r->color ? r->color : "", r->indent, r->align, r->heading);
}

static void expect_render(const char *in, const char *css, const char *want)
{
    GString *g = g_string_new(NULL);
    KsStyleSheet *sheet = css ? ks_style_sheet_new(css) : NULL;

    ks_html_render(in, sheet, ser_run, g);
    if (strcmp(g->str, want) != 0) {
        printf("FAIL render: %s\n  want: %s\n  got:  %s\n", in, want, g->str);
        failures++;
    }
    if (sheet)
        ks_style_sheet_free(sheet);
    g_string_free(g, TRUE);
}

int main(void)
{
    char buf[8];

    expect("<h2>Almeida Revista e Corrigida</h2> <p>Almeida Revista (RC): traduzida.</p> <p>Informações</p>",
           "Almeida Revista e Corrigida\n\nAlmeida Revista (RC): traduzida.\n\nInformações");
    /* Upper-case tags, attributes, source line breaks, tables, entities. */
    expect("<CENTER>\n\t<TABLE WIDTH=372 BORDER=1>\n\t\t<COL WIDTH=358>\n\t\t<TR>\n\t\t\t<TD VALIGN=TOP>\n"
           "\t\t\t\t<P CLASS=\"western\"><FONT SIZE=7><B>Holy\n\t\t\t\tBible</B></FONT></P>\n"
           "</TD></TR></TABLE></CENTER>\n<P>Copyright &copy; 2016<BR>All Rights.</P>",
           "Holy Bible\n\nCopyright © 2016\nAll Rights.");
    expect("<ul><li>one</li><li>two <i>x</i></li></ul>after", "• one\n• two x\nafter");
    expect("<table><tr><th>a</th><td>b</td></tr><tr><td>c</td><td>d</td></tr></table>", "a\tb\nc\td");
    expect("<style>p { color: red }</style><p>text</p><script>if (a<b) x();</script>",
           "text");
    expect("a<!-- <b>hidden</b> -->b &amp; &nbsp;c &bogus; 1 < 2", "ab & \xc2\xa0" "c &bogus; 1 < 2");
    expect("a<br><br><br><br>b<hr>c", "a\n\nb\n———\nc");
    /* No tags at all: plain text, line breaks kept. */
    expect("Line one\r\nLine two\n\nPara", "Line one\nLine two\n\nPara");
    expect("", "");

    expect_runs("<b>a <i>b</i></b> <a href='x'>c</a><h1>T</h1>",
                "{1}'a '{3}'b'' '{40}'c''\n\n'{21}'T'"); /* nothing after the heading */
    expect_runs("</b>x<b><b>y</b>z</b>w", "'x'{1}'yz''w'"); /* stray close, nesting */

    /* Links keep their target; the space between two links belongs to neither. */
    expect_render("<a class='dict' href='#dH1'>H1</a> <a href=\"sH2\">H2</a>",
                  NULL, "H1|40|#dH1||0|0|0 /  |0|||0|0|0 / H2|40|sH2||0|0|0");
    /* Numbered lists (<ol type='a'> nested), bullets. */
    expect("<ol><li>one<ol type='a'><li>x</li><li>y</li></ol><li>two</ol><ul><li>z</ul>",
           "1. one\na. x\nb. y\n2. two\n• z");
    expect("<ol start='3' type='I'><li>c<li>d</ol>", "III. c\nIV. d");
    expect_render("<ul><li>a<ul><li>b</ul></ul>", NULL,
                  "• a|0|||1|0|0 / \n|0|||0|0|0 / ◦ b|0|||2|0|0");
    /* A new <p> closes the open one; </p> without <p> is harmless. */
    expect("<p>a<p>b</p></p>c", "a\n\nb\n\nc");
    /* Classes: built-in colors, the module's CSS, custom elements, hidden. */
    expect_render("<span class='red'>r</span><font color='blue'>b</font>", NULL,
                  "r|0||#cc2222|0|0|0 / b|0||#2a5db0|0|0|0");
    expect_render("<span class='x'>no</span><gloss>g</gloss>y", ".x{display:none}\ngloss{font-style:italic}",
                  "g|2|||0|0|0 / y|0|||0|0|0");
    expect_render("<p style='margin-left:1.8em; color:#008000'>m</p>", NULL, "m|0||#008000|1|0|0");
    expect_render("<P ALIGN=CENTER><B>T</B></P><center>c</center>", NULL,
                  "T|1|||0|1|0 / \n\n|0|||0|0|0 / c|0|||0|1|0");
    expect_render("<h2>Head</h2>", NULL, "Head|21|||0|0|2");
    /* Images and anchors are runs of their own. */
    expect_render("<p class='pic'><img src='Addax-1.jpg'></p><a name='BM1'></a>x", NULL,
                  "IMG:Addax-1.jpg / #BM1 / \n\nx|0|||0|0|0");
    /* Quoted attributes may hold '>' and the other quote (HL). */
    expect_render("<a class='ref nd' href='rpos=\"n-m\" a>b'>אָב</a>", NULL, "אָב|40|rpos=\"n-m\" a>b||0|0|0");
    /* Verse-style Strong's numbers and HL's references to other dictionaries. */
    expect_render("<hr><WH3><TWOT.1a>", NULL,
                  "———\n|0|||0|0|0 / H3|40|sH3||0|0|0 /  |0|||0|0|0 / TWOT 1a|40|d-TWOT 1a||0|0|0");
    expect("def <WH25a>", "def H25a");
    /* Unknown elements show their content (ETCBC's <TESTING>, SEC's <H>...<h>). */
    expect("<h1><H>אָב<h>&#x200E; (ʼâb)</h1><TESTING>[SN]", "אָב\xe2\x80\x8e (ʼâb)\n\n[SN]");

    if (ks_html_entity("&eacute;", buf) != 8 || strcmp(buf, "é") != 0 ||
        ks_html_entity("&#x5D0;", buf) != 7 || strcmp(buf, "א") != 0 ||
        ks_html_entity("&mdash;x", buf) != 7 || ks_html_entity("&nope;", buf) != 0 ||
        ks_html_entity("&#0;", buf) != 0 || ks_html_entity("&amp", buf) != 0) {
        printf("FAIL entities\n");
        failures++;
    }

    if (failures)
        printf("test_html: %d failure(s)\n", failures);
    else
        printf("test_html: OK\n");
    return failures != 0;
}
