/* test_html.c - module description HTML, with fragments of real Comments
 * (ARC2009, berean-bsb). */
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
