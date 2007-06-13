#!/bin/sh

PATH=~/asciidoc/bin:$PATH \
make prefix=/var/tmp/asciidoc8 \
	WEBDOC_DEST=/var/tmp/asciidoc8/webdoc \
	install install-webdoc
