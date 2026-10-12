/* rules.h - Details.VerseRules: regex substitutions MySword applies to
 * every verse before showing it.
 *
 * One rule per line: pattern<TAB>replacement[<TAB>replacement when the
 * rule is switched off<TAB>name]. The syntax is .NET/Java's ($1 in the
 * replacement), which PCRE (GRegex) reads; rules that don't compile are
 * skipped. Switchable rules are applied in their default (on) form. */
#ifndef KS_RULES_H
#define KS_RULES_H

#include <glib.h>

typedef struct KsRules KsRules;

/* NULL if src has no rule that compiles. n_invalid (may be NULL) gets the
 * number of rules skipped. */
KsRules *ks_rules_new(const char *src, int *n_invalid);
void ks_rules_free(KsRules *r);
int ks_rules_count(const KsRules *r);

/* The verse after every rule, in order; NULL if no rule changed it. */
char *ks_rules_apply(const KsRules *r, const char *text);

#endif /* KS_RULES_H */
