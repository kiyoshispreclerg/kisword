/* rules.c - Details.VerseRules (see rules.h). */
#include "rules.h"

#include <string.h>

/* One replacement as GRegex wants it. */
typedef struct {
    char *repl;
    gboolean literal;  /* no group references: g_regex_replace_literal */
    gboolean identity; /* replaces the match with itself: skipped */
} Repl;

typedef struct {
    GRegex *re;
    Repl on, off;      /* off: the third field, used when switched off */
    int option;        /* index in KsRules.options, -1: always on */
} Rule;

struct KsRules {
    GArray *rules;      /* Rule */
    GPtrArray *options; /* option names, in order of first appearance */
    GArray *off;        /* gboolean per option: switched off */
};

/* "$1", "${1}", "$$" -> GRegex's "\g<1>", "$"; backslashes are literal in
 * .NET replacements, so they get escaped. */
static char *convert_replacement(const char *s, int n, gboolean *literal)
{
    GString *g = g_string_sized_new(n + 8);

    *literal = TRUE;
    for (int i = 0; i < n; i++) {
        if (s[i] == '$' && i + 1 < n && g_ascii_isdigit(s[i + 1])) {
            int j = i + 1;

            /* At most two digits: "$10" is group 10, as in .NET when it exists. */
            while (j < n && j < i + 3 && g_ascii_isdigit(s[j]))
                j++;
            g_string_append_printf(g, "\\g<%.*s>", j - i - 1, s + i + 1);
            *literal = FALSE;
            i = j - 1;
        } else if (s[i] == '$' && i + 1 < n && s[i + 1] == '{') {
            const char *close = memchr(s + i, '}', n - i);

            if (close) {
                g_string_append_printf(g, "\\g<%.*s>", (int)(close - (s + i + 2)), s + i + 2);
                *literal = FALSE;
                i = close - s;
            } else {
                g_string_append_c(g, '$');
            }
        } else if (s[i] == '$' && i + 1 < n && s[i + 1] == '$') {
            g_string_append_c(g, '$');
            *literal = FALSE;
            i++;
        } else if (s[i] == '\\') {
            g_string_append(g, "\\\\");
        } else {
            g_string_append_c(g, s[i]);
        }
    }
    return g_string_free(g, FALSE);
}

/* "(...)" -> "$1": the whole match replaced by itself. Modules use these as
 * switchable no-ops ("Hebrew vowels" on/off); in their on form they only
 * cost time, and on every letter of the Hebrew text that adds up. */
static gboolean is_identity(const char *pattern, const char *repl)
{
    size_t n = strlen(pattern);
    int depth = 0;

    if (strcmp(repl, "\\g<1>") != 0 || n < 2 || pattern[0] != '(' || pattern[n - 1] != ')' || pattern[1] == '?')
        return FALSE;
    for (size_t i = 0; i < n; i++) {
        if (pattern[i] == '\\') {
            i++;
            continue;
        }
        if (pattern[i] == '[') { /* a class may hold parentheses */
            for (i++; i < n && pattern[i] != ']'; i++)
                if (pattern[i] == '\\')
                    i++;
            continue;
        }
        if (pattern[i] == '(')
            depth++;
        else if (pattern[i] == ')' && --depth == 0 && i != n - 1)
            return FALSE; /* "(a)(b)": the first group is not the whole match */
    }
    return TRUE;
}

static void make_repl(Repl *r, const char *pattern, const char *s, int n)
{
    r->repl = convert_replacement(s, n, &r->literal);
    if (r->literal) { /* used as is */
        g_free(r->repl);
        r->repl = g_strndup(s, n);
    }
    r->identity = is_identity(pattern, r->repl);
}

static int option_index(KsRules *r, const char *name)
{
    gboolean off = FALSE;

    for (guint i = 0; i < r->options->len; i++)
        if (strcmp(g_ptr_array_index(r->options, i), name) == 0)
            return (int)i;
    g_ptr_array_add(r->options, g_strdup(name));
    g_array_append_val(r->off, off);
    return (int)r->options->len - 1;
}

KsRules *ks_rules_new(const char *src, int *n_invalid)
{
    KsRules *r;
    char **lines;
    int bad = 0;

    if (n_invalid)
        *n_invalid = 0;
    if (!src || !*src)
        return NULL;
    r = g_new0(KsRules, 1);
    r->rules = g_array_new(FALSE, FALSE, sizeof(Rule));
    r->options = g_ptr_array_new_with_free_func(g_free);
    r->off = g_array_new(FALSE, FALSE, sizeof(gboolean));
    lines = g_strsplit(src, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = lines[i], **f;
        size_t len = strlen(line);
        Rule rule = { 0 };
        GError *err = NULL;
        int nf;

        if (len && line[len - 1] == '\r')
            line[--len] = '\0';
        if (!*line || *line == '\t')
            continue;
        /* pattern, replacement, replacement when off, name */
        f = g_strsplit(line, "\t", 4);
        nf = g_strv_length(f);
        rule.re = g_regex_new(f[0], G_REGEX_OPTIMIZE, 0, &err);
        if (!rule.re) {
            g_error_free(err);
            g_strfreev(f);
            bad++;
            continue;
        }
        make_repl(&rule.on, f[0], nf > 1 ? f[1] : "", nf > 1 ? (int)strlen(f[1]) : 0);
        rule.option = -1;
        if (nf > 3 && *f[3]) {
            rule.option = option_index(r, g_strstrip(f[3]));
            make_repl(&rule.off, f[0], f[2], strlen(f[2]));
        }
        g_array_append_val(r->rules, rule);
        g_strfreev(f);
    }
    g_strfreev(lines);
    if (n_invalid)
        *n_invalid = bad;
    if (!r->rules->len) {
        ks_rules_free(r);
        return NULL;
    }
    return r;
}

void ks_rules_free(KsRules *r)
{
    if (!r)
        return;
    for (guint i = 0; i < r->rules->len; i++) {
        Rule *rule = &g_array_index(r->rules, Rule, i);

        g_regex_unref(rule->re);
        g_free(rule->on.repl);
        g_free(rule->off.repl);
    }
    g_array_free(r->rules, TRUE);
    g_ptr_array_free(r->options, TRUE);
    g_array_free(r->off, TRUE);
    g_free(r);
}

int ks_rules_count(const KsRules *r)
{
    return r ? (int)r->rules->len : 0;
}

int ks_rules_n_options(const KsRules *r)
{
    return r ? (int)r->options->len : 0;
}

const char *ks_rules_option_name(const KsRules *r, int i)
{
    return g_ptr_array_index(r->options, i);
}

gboolean ks_rules_option_on(const KsRules *r, int i)
{
    return !g_array_index(r->off, gboolean, i);
}

void ks_rules_set_option(KsRules *r, int i, gboolean on)
{
    g_array_index(r->off, gboolean, i) = !on;
}

char *ks_rules_apply(const KsRules *r, const char *text)
{
    char *cur = NULL;

    for (guint i = 0; r && i < r->rules->len; i++) {
        const Rule *rule = &g_array_index(r->rules, Rule, i);
        const Repl *rp = rule->option >= 0 && g_array_index(r->off, gboolean, rule->option) ? &rule->off
                                                                                               : &rule->on;
        const char *in = cur ? cur : text;
        char *out;

        if (rp->identity || !g_regex_match(rule->re, in, 0, NULL))
            continue;
        /* A rule that hits PCRE's backtracking limit leaves the verse as is. */
        out = rp->literal ? g_regex_replace_literal(rule->re, in, -1, 0, rp->repl, 0, NULL)
                          : g_regex_replace(rule->re, in, -1, 0, rp->repl, 0, NULL);
        if (out) {
            g_free(cur);
            cur = out;
        }
    }
    return cur;
}
