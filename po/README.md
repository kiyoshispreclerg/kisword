# Translations

Source strings are English, marked with `_()` / `N_()` (see `src/ks_i18n.h`).
Book names are not here: they live in `src/books.c`, one table per language.

- `make pot` regenerates `kisword.pot` from the sources.
- `make update-po` merges it into every `<lang>.po` listed in `LINGUAS`.
- `make` compiles each `.po` to `.mo`; `make install` installs them.

To add a language: copy `kisword.pot` to `<lang>.po`, translate it, and add
`<lang>` to `LINGUAS`.
