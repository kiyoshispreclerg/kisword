# kisword - light Bible reader for MySword modules (C + GTK2).
PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
DATADIR := $(PREFIX)/share
LOCALEDIR := $(DATADIR)/locale

CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
VERSION := $(shell cat VERSION)

PKGS := gtk+-2.0 sqlite3
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs $(PKGS) 2>/dev/null)
ifeq ($(strip $(PKG_LIBS)),)
$(error kisword: gtk+-2.0 and sqlite3 are required (install libgtk2.0-dev and libsqlite3-dev))
endif

# The tests only need GLib and SQLite: no GTK, no display.
TEST_PKGS := glib-2.0 sqlite3
TEST_CFLAGS := $(shell pkg-config --cflags $(TEST_PKGS))
TEST_LIBS := $(shell pkg-config --libs $(TEST_PKGS))

DEFS := -DLOCALEDIR='"$(LOCALEDIR)"' -DKS_VERSION='"$(VERSION)"'
# GTK2's own headers use GLib types GLib has since deprecated (GTimeVal,
# GTypeDebugFlags); those warnings say nothing about this code.
DEFS += -DGLIB_DISABLE_DEPRECATION_WARNINGS
# gettext is part of glibc; -lintl only where it is a separate library.
LIBINTL := $(shell echo 'int main(void){return 0;}' | $(CC) -x c - -o /dev/null -lintl 2>/dev/null && echo -lintl)

LINGUAS := $(shell test -f po/LINGUAS && grep -v '^\#' po/LINGUAS)
MOFILES := $(foreach lang,$(LINGUAS),po/$(lang).mo)

SRCS := src/main.c src/window.c src/searchpane.c src/render.c src/module.c src/gbf.c src/style.c src/rules.c src/html.c \
	src/search.c src/books.c src/ref.c src/config.c
OBJS := $(SRCS:.c=.o)
HDRS := $(wildcard src/*.h)

all: kisword $(MOFILES)

kisword: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(PKG_LIBS) $(LIBINTL)

src/%.o: src/%.c $(HDRS) VERSION
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(DEFS) -c -o $@ $<

po/%.mo: po/%.po
	msgfmt -c -o $@ $<

# ---- translations (see po/README.md) ---------------------------------------

pot: po/kisword.pot

po/kisword.pot: $(SRCS)
	xgettext --from-code=UTF-8 --language=C --keyword=_ --keyword=N_ \
		--add-comments=TRANSLATORS: --sort-by-file --package-name=kisword \
		--package-version=$(VERSION) -o $@ $(SRCS)

update-po: po/kisword.pot
	@for lang in $(LINGUAS); do msgmerge -U --backup=none po/$$lang.po po/kisword.pot; done

# ---- tests -------------------------------------------------------------------

TESTS := tests/test_gbf tests/test_rules tests/test_html tests/test_ref tests/test_module tests/test_search

GBF_SRCS := src/gbf.c src/style.c src/html.c
GBF_HDRS := src/gbf.h src/html.h

tests/test_gbf: tests/test_gbf.c $(GBF_SRCS) $(GBF_HDRS)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -Isrc -o $@ tests/test_gbf.c $(GBF_SRCS) $(TEST_LIBS)

tests/test_rules: tests/test_rules.c src/rules.c src/rules.h $(GBF_SRCS) $(GBF_HDRS)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -Isrc -o $@ tests/test_rules.c src/rules.c $(GBF_SRCS) $(TEST_LIBS)

tests/test_html: tests/test_html.c src/html.c src/html.h
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -Isrc -o $@ tests/test_html.c src/html.c $(TEST_LIBS)

tests/test_ref: tests/test_ref.c src/ref.c src/books.c src/ref.h src/books.h
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -Isrc -o $@ tests/test_ref.c src/ref.c src/books.c $(TEST_LIBS)

MODULE_SRCS := src/module.c src/rules.c $(GBF_SRCS)

tests/test_module: tests/test_module.c $(MODULE_SRCS) src/module.h src/rules.h $(GBF_HDRS)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) $(DEFS) -Isrc -o $@ tests/test_module.c $(MODULE_SRCS) $(TEST_LIBS) $(LIBINTL)

tests/test_search: tests/test_search.c src/search.c src/search.h $(MODULE_SRCS) src/module.h $(GBF_HDRS)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) $(DEFS) -Isrc -o $@ tests/test_search.c src/search.c $(MODULE_SRCS) $(TEST_LIBS) $(LIBINTL)

# Unit tests always; the real modules in interno/ (kept out of the
# repository) too, when present.
test: kisword $(TESTS)
	@for t in $(TESTS); do echo "==> $$t"; ./$$t || exit 1; done
	@./tests/check-samples.sh

# -Werror build of everything plus cppcheck. varFuncNullUB is the GObject
# property-list idiom (..., NULL) that all of GTK's API uses; NULL is
# (void *)0 on every platform this targets.
lint:
	$(MAKE) clean
	$(MAKE) CFLAGS="-Wall -Wextra -Werror -O2" all $(TESTS)
	cppcheck --quiet --error-exitcode=1 --enable=warning,performance,portability \
		--suppress=missingIncludeSystem --suppress=varFuncNullUB --inline-suppr -Isrc src tests

# ---- install -----------------------------------------------------------------

install: all
	install -Dm755 kisword $(DESTDIR)$(BINDIR)/kisword
	install -Dm644 kisword.desktop $(DESTDIR)$(DATADIR)/applications/kisword.desktop
	@for lang in $(LINGUAS); do \
		install -Dm644 po/$$lang.mo $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/kisword.mo; \
	done

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/kisword $(DESTDIR)$(DATADIR)/applications/kisword.desktop
	@for lang in $(LINGUAS); do rm -f $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/kisword.mo; done

clean:
	rm -f kisword $(OBJS) $(TESTS) po/*.mo

.PHONY: all pot update-po test lint install uninstall clean
