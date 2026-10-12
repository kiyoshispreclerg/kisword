#!/bin/sh
# Builds a .deb from the current tree, on the machine it runs on: the
# dependencies are computed by dpkg-shlibdeps against the libraries
# installed here, so build on the oldest target distro of each family.
# Usage: tools/build-deb.sh [OUTDIR]   (needs build-essential, dpkg-dev,
# gettext, libgtk2.0-dev and libsqlite3-dev).
set -eu

cd "$(dirname "$0")/.."
root=$(pwd)
out=${1:-$root/dist}
version=$(cat VERSION)
arch=$(dpkg --print-architecture)
maint=${DEB_MAINTAINER:-Kiyoshi Spreclerg <kiyoshi_pip@protonmail.com>}

mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pkg=$work/pkg

make clean
make -j"$(nproc)" PREFIX=/usr
make PREFIX=/usr DESTDIR="$pkg" install
strip --strip-unneeded "$pkg"/usr/bin/kisword
find "$pkg" -type d -exec chmod 755 {} +

# dpkg-shlibdeps insists on a debian/control in the current directory.
mkdir -p "$work/debian"
printf 'Source: kisword\n\nPackage: kisword\nArchitecture: any\n' > "$work/debian/control"
(
	cd "$work"
	dpkg-shlibdeps -O "-e$pkg/usr/bin/kisword" > "$work/substvars"
)
depends=$(sed -n 's/^shlibs:Depends=//p' "$work/substvars")

mkdir -p "$pkg/DEBIAN"
size=$(du -sk "$pkg" | cut -f1)
cat > "$pkg/DEBIAN/control" <<CTL
Package: kisword
Version: $version-1
Architecture: $arch
Maintainer: $maint
Installed-Size: $size
Depends: $depends
Section: text
Priority: optional
Homepage: https://github.com/${GITHUB_REPOSITORY:-kisword/kisword}
Description: light Bible reader for MySword modules
 Reads MySword Bible modules (*.bbl.mybible) directly, with Strong's
 numbers, footnotes, search and parallel Bibles. C and GTK2, no WebKit.
CTL

deb=$out/kisword_${version}-1_${arch}.deb
dpkg-deb --root-owner-group --build "$pkg" "$deb"
echo "$deb"
