#!/bin/sh
# Runs kisword --check/--dump against the real modules in interno/, which
# stay out of the repository. Skipped when they are not there.
set -e
cd "$(dirname "$0")/.."
dir=interno

if [ ! -d "$dir" ]; then
    echo "==> sample modules: $dir/ not found, skipped"
    exit 0
fi

echo "==> sample modules"
fail=0
for f in "$dir"/*.bbl.mybible; do
    [ -e "$f" ] || continue
    out=$(./kisword --check "$f") || fail=1
    echo "$out"
done

# Commentaries, dictionaries, books and journals, in MySword's own
# subfolders; dictionaries over 100 MB (BHSE, OpenHBd...) only by hand:
# they take seconds each.
for f in "$dir"/commentaries/*.mybible "$dir"/books/*.mybible "$dir"/journals/*.mybible \
         $(find "$dir/dictionaries" -name '*.mybible' -size -100M 2>/dev/null | sort); do
    [ -e "$f" ] || continue
    out=$(./kisword --check "$f") || fail=1
    echo "$out" | head -1
done

expect() { # module ref expected-substring
    [ -e "$dir/$1" ] || return 0
    got=$(LANG=C ./kisword --dump "$2" "$dir/$1")
    case "$got" in
        *"$3"*) ;;
        *) echo "FAIL --dump $2 ($1): got: $got"; fail=1 ;;
    esac
}
expect kjv.bbl.mybible "Jo 3:16" "16 For God so loved the world, that he gave his only begotten Son"
expect kjv.bbl.mybible "Gn 1:1" "1 In the beginning God created the heaven and the earth."
expect RVP-2009.bbl.mybible "Sl 23:1" "1 O SENHOR é meu pastor; nada me faltará."
expect RVP-2009.bbl.mybible "1Sm 17:58" "Belém. Vive a tua alma"

# Words, references and entries in the other kinds.
expect dictionaries/strong_lite.dct.mybible "h0430" "Phonetic: el-o-heem'"
expect dictionaries/wordnet.dct.mybible "love" "1. a strong positive emotion of regard and affection"
expect commentaries/tsk.cmt.mybible "Jo 3:16" "Luk 2:14; Rom 5:8; 2Co 5:19-21"
expect books/kjv.bok.mybible "Tobit 1" "1 The book of the words of Tobit"
expect journals/mybookmarks.jor.mybible "abba" "Mark 14:36; Rom 8:15; Gal 4:6"

expect_search() { # module query expected-substring [options]
    [ -e "$dir/$1" ] || return 0
    got=$(LANG=C ./kisword --search "$2" $4 "$dir/$1" 2>&1)
    case "$got" in
        *"$3"*) ;;
        *) echo "FAIL --search $2 ($1): got: $(echo "$got" | tail -1)"; fail=1 ;;
    esac
}
expect_search RVP-2009.bbl.mybible "AMOU O MUNDO" "Jo 3:16 Porque Deus amou o mundo" --phrase
expect_search RVP-2009.bbl.mybible "principio criou" "Gn 1:1 No princípio"
expect_search kjv.bbl.mybible "H430" "2249 verses"

if [ -e "$dir/HiSB.bbl.mybible" ]; then
    ./kisword --check "$dir/HiSB.bbl.mybible" | grep -q UNSUPPORTED || { echo "FAIL HiSB not refused"; fail=1; }
fi

[ "$fail" = 0 ] && echo "sample modules: OK"
exit $fail
