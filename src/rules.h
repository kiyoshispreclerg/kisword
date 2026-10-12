/* rules.h - Details.VerseRules: regex substitutions MySword applies to
 * every verse before showing it.
 *
 * One rule per line: pattern<TAB>replacement[<TAB>replacement when the
 * rule is switched off<TAB>name]. The syntax is .NET/Java's ($1 in the
 * replacement), which PCRE (GRegex) reads; rules that don't compile are
 * skipped. Rules with a name are options MySword lets the reader switch
 * ("Show Hebrew vowels"); several rules may share one name. All start on. */
#ifndef KS_RULES_H
#define KS_RULES_H

#include <glib.h>

typedef struct KsRules KsRules;

/* NULL if src has no rule that compiles. n_invalid (may be NULL) gets the
 * number of rules skipped. */
KsRules *ks_rules_new(const char *src, int *n_invalid);
void ks_rules_free(KsRules *r);
int ks_rules_count(const KsRules *r);

/* The named options, and switching them. */
int ks_rules_n_options(const KsRules *r);
const char *ks_rules_option_name(const KsRules *r, int i);
gboolean ks_rules_option_on(const KsRules *r, int i);
void ks_rules_set_option(KsRules *r, int i, gboolean on);

/* The verse after every rule, in order; NULL if no rule changed it. */
char *ks_rules_apply(const KsRules *r, const char *text);

#endif /* KS_RULES_H */
