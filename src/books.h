/* books.h - book names and abbreviations.
 *
 * MySword modules only store book numbers (1-66, Protestant order), so the
 * names come from here: one table per language, picked from the locale or
 * the `book_names` config key. Lookups accept both languages. */
#ifndef KS_BOOKS_H
#define KS_BOOKS_H

#include <glib.h>

#define KS_NBOOKS 66

/* "pt" or "en"; NULL or "" picks from the locale (pt* -> pt, else en). */
void ks_books_set_lang(const char *lang);

/* Display name ("Gênesis") and short form ("Gn") in the current language.
 * Books past 66 (apocrypha, unknown numbering) just get their number. */
const char *ks_book_name(int book);
const char *ks_book_abbrev(int book);

/* Book number for a name, abbreviation or prefix of the full name, in
 * either language, ignoring case, spaces, dots and (as a fallback) accents.
 * 0 if nothing matches. */
int ks_book_lookup(const char *s, gssize len);

/* The same, English first whatever the language: for the references
 * inside modules, written in English ("Jn" is John, not Jonas). */
int ks_book_lookup_en(const char *s, gssize len);

/* Case-folded copy of s without spaces or dots; with strip_marks, also
 * without diacritics ("1 Coríntios" -> "1corintios"). */
char *ks_fold_key(const char *s, gssize len, gboolean strip_marks);

#endif /* KS_BOOKS_H */
