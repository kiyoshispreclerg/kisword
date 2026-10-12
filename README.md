# kisword

[Português](README.pt-BR.md) · **English**

A light desktop Bible reader for **MySword** modules (`*.bbl.mybible`), in C
and GTK2.

MySword is an Android Bible app. Its modules are SQLite databases, and as far
as I know nothing on the desktop reads them directly — only converters to
other formats exist. kisword reads them as they are: copy the files from your
phone and open them.

It is a sibling of KiDesktop with the same goal — start fast, stay small —
but a separate program; it may end up in a future `kidesktop-extras`.

## Features

- Bibles in MySword format, with the tags they use rendered natively (no
  WebKit): words of Jesus in red, added words in italics, OT quotations,
  section titles, paragraph marks, indentation.
- Modules' own formatting: `VerseRules` (the regex substitutions MySword
  applies to each verse) and the simple part of their `CustomCSS` — class
  colors, italics, bold, superscripts, hidden and block elements.
- Strong's numbers as small clickable superscripts (F9 hides them).
- Footnotes as clickable markers that open a popup.
- Verse-per-line or paragraph layout.
- "Go to" with free-form references in Portuguese or English: `Jo 3:16`,
  `1Co 13:4-7`, `gn 1`, `João 3`, `John 3:16`, or just `5` for chapter 5.
- Search (Ctrl+F) in a side panel: all words, exact phrase or any word,
  ignoring case and accents (and Hebrew points), whole words optional,
  limited to the Old/New Testament or the current book. A Strong's number
  (`H430`, `G26`) finds the verses tagged with it. The index is built the
  first time you search a module, in the background.
- Parallel Bibles (F8): up to seven next to the main one, side by side in
  columns that scroll together, or interleaved verse by verse in one view
  (right-to-left modules keep their direction line by line).
- Copy (Ctrl+C) gives whole verses as plain text with the reference:
  `Jo 3:16-17 (KJV)` followed by the verses.
- Back/forward history, zoom, font choice, last position remembered.
- Help → About the module shows the module's details and its description
  (the HTML in `Details.Comments`) as formatted text.
- Right-to-left modules are laid out right-to-left.
- Interface in English and Brazilian Portuguese; book names follow the locale.

Not yet: dictionaries and commentaries (see
[KISWORD_PLAN.md](KISWORD_PLAN.md)).

Tested against a collection of 125 MySword Bibles: all open, and all but
the two encrypted ones parse without unknown tags — titles, poetry lines,
interlinears, cross references, the HTML that VerseRules produce, modules
mixing right-to-left and left-to-right testaments. Interlinears built for
MySword's stacked HTML layout (word over gloss over morphology) are shown in
running text: complete, but dense.

**Encrypted modules are not supported.** Some MySword modules (for example the
ones with `encryption` set in their `Details` table) are protected by MySword
itself; kisword lists them greyed out and explains why, and does not try to get
around that protection.

## Installing

Every release has `.deb` packages on the
[Releases](../../releases) page, for Ubuntu 22.04, 24.04 and 26.04, Debian 12
and 13, and Devuan daedalus and excalibur (use the Debian 12 / 13 package
respectively). Tags `v1.x.x` and later are stable releases; `v0.x.x` and
tags with a suffix (`v1.2.0-beta`) are marked as pre-releases.

```sh
sudo apt install ./kisword_0.1.0-1_<distro>_amd64.deb
```

## Building

Needs `libgtk2.0-dev`, `libsqlite3-dev` and gettext.

```sh
make
make test        # unit tests; also checks real modules in interno/ if present
sudo make install
```

`make lint` builds with `-Werror` and runs cppcheck.

## Using

```sh
kisword                         # modules from ~/.local/share/kisword/modules
kisword path/to/kjv.bbl.mybible --ref "Jo 3:16"
kisword --dump "Sl 23" rvp.bbl.mybible   # plain text to stdout, no display needed
kisword --check *.bbl.mybible            # parse every verse, report problems
kisword --search "amor próximo" arc.bbl.mybible   # verses with all the words
kisword --search H430 --phrase ...       # --phrase, --whole-words as in the panel
```

Put modules in `~/.local/share/kisword/modules/`, list more directories in
`modules_dirs=` in `~/.config/kisword/kisword.conf`, or use File → Open module.

| Shortcut | Action |
|---|---|
| Ctrl+L | Go to reference |
| Ctrl+F | Search (Esc closes the panel) |
| F8 | Parallel Bibles |
| Ctrl+PgUp / Ctrl+PgDn | Previous / next chapter |
| Alt+← / Alt+→ | Back / forward |
| Ctrl++ / Ctrl+- / Ctrl+0 | Zoom |
| F9 | Strong's numbers on/off |
| Ctrl+C | Copy verses with reference |
| Ctrl+O | Open module |

## Lightness, measured

On Xvfb, opening the KJV at Genesis 1 or Psalm 119 (the longest chapter):

| | kisword | minimal GTK2 window with a text view |
|---|---|---|
| Start to window shown | ~190 ms | ~100 ms |
| Max RSS | ~34 MB | ~27 MB |
| Binary | 122 KB | — |

Parsing all 31,102 verses of the KJV takes ~25 ms (`kisword --check`). Most of
the startup time is GTK and fontconfig; switching chapters is a single indexed
query and renders in a few milliseconds.

## License

GPL-3.0, see [LICENSE](LICENSE).
