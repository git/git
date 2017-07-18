#!/bin/sh

case "$1" in
7)
	V=7
	EXTRA=
	;;
8 | '')
	V=8
	PATH=~/asciidoc/bin:$PATH
	EXTRA=ASCIIDOC8=YesPlease
	;;
esac

make prefix=/var/tmp/asciidoc$V \
	WEBDOC_DEST=/var/tmp/asciidoc$V/webdoc \
	$EXTRA \
	install install-webdoc
