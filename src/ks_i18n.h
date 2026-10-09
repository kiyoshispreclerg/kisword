/* ks_i18n.h - gettext setup, same pattern as KiDesktop's xis_i18n.h.
 *
 * `_("...")` translates a user-visible literal at runtime; `N_("...")` only
 * marks it for extraction (static tables, GOption descriptions).
 * LC_NUMERIC goes back to "C" so nothing that parses numbers depends on the
 * user's decimal separator. */
#ifndef KS_I18N_H
#define KS_I18N_H

#include <libintl.h>
#include <locale.h>

#define _(s) gettext(s)
#define N_(s) (s)

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

static inline void ks_i18n_init(void)
{
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    bindtextdomain("kisword", LOCALEDIR);
    bind_textdomain_codeset("kisword", "UTF-8");
    textdomain("kisword");
}

#endif /* KS_I18N_H */
