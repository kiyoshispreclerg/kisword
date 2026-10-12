/* rules.c - Details.VerseRules (see rules.h). */
#include "rules.h"

#include <string.h>

typedef struct {
    GRegex *re;
    char *repl; /* in GRegex syntax */
    gboolean literal;
} Rule;

struct KsRules {
    GArray *rules; /* Rule */
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
    lines = g_strsplit(src, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = lines[i], *tab, *repl_end;
        size_t len = strlen(line);
        Rule rule;
        GError *err = NULL;

        if (len && line[len - 1] == '\r')
            line[--len] = '\0';
        tab = strchr(line, '\t');
        if (!*line || tab == line)
            continue;
        if (tab)
            *tab = '\0';
        rule.re = g_regex_new(line, G_REGEX_OPTIMIZE, 0, &err);
        if (!rule.re) {
            g_error_free(err);
            bad++;
            continue;
        }
        if (tab) {
            int n;

            repl_end = strchr(tab + 1, '\t');
            n = repl_end ? repl_end - (tab + 1) : (int)strlen(tab + 1);
            rule.repl = convert_replacement(tab + 1, n, &rule.literal);
            if (rule.literal) { /* no group references: used as is */
                g_free(rule.repl);
                rule.repl = g_strndup(tab + 1, n);
            }
        } else {
            rule.repl = g_strdup("");
            rule.literal = TRUE;
        }
        if (is_identity(line, rule.repl)) {
            g_regex_unref(rule.re);
            g_free(rule.repl);
            continue;
        }
        g_array_append_val(r->rules, rule);
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
        g_free(rule->repl);
    }
    g_array_free(r->rules, TRUE);
    g_free(r);
}

int ks_rules_count(const KsRules *r)
{
    return r ? (int)r->rules->len : 0;
}

char *ks_rules_apply(const KsRules *r, const char *text)
{
    char *cur = NULL;

    for (guint i = 0; r && i < r->rules->len; i++) {
        const Rule *rule = &g_array_index(r->rules, Rule, i);
        const char *in = cur ? cur : text;
        char *out;

        if (!g_regex_match(rule->re, in, 0, NULL))
            continue;
        /* A rule that hits PCRE's backtracking limit leaves the verse as is. */
        out = rule->literal ? g_regex_replace_literal(rule->re, in, -1, 0, rule->repl, 0, NULL)
                            : g_regex_replace(rule->re, in, -1, 0, rule->repl, 0, NULL);
        if (out) {
            g_free(cur);
            cur = out;
        }
    }
    return cur;
}
