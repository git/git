#!/bin/sh
. git-sh-setup

type="$(git-cat-file -t "$1" 2>/dev/null)" ||
	die "$1: no such object."

test "$type" = tag ||
	die "$1: cannot verify a non-tag object of type $type."

git-cat-file tag "$1" > .tmp-vtag || exit 1
cat .tmp-vtag | sed '/-----BEGIN PGP/Q' | gpg --verify .tmp-vtag - || exit 1
rm -f .tmp-vtag
