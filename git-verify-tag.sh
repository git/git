#!/bin/sh
. git-sh-setup || die "Not a git archive"

tag=$(git-rev-parse $1) || exit 1

git-cat-file tag $tag > .tmp-vtag || exit 1
cat .tmp-vtag | sed '/-----BEGIN PGP/Q' | gpg --verify .tmp-vtag - || exit 1
rm -f .tmp-vtag
