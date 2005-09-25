#!/bin/sh
#
# This is for people who installed previous GIT by hand and would want
# to remove the backward compatible links:
#
# ./cmd-rename.sh $bindir
#
d="$1"
test -d "$d" || exit
while read old new
do
	rm -f "$d/$old"
done <<\EOF
git-add-script	git-add
git-archimport-script	git-archimport
git-bisect-script	git-bisect
git-branch-script	git-branch
git-checkout-script	git-checkout
git-cherry-pick-script	git-cherry-pick
git-clone-script	git-clone
git-commit-script	git-commit
git-count-objects-script	git-count-objects
git-cvsimport-script	git-cvsimport
git-diff-script	git-diff
git-send-email-script	git-send-email
git-fetch-script	git-fetch
git-format-patch-script	git-format-patch
git-log-script	git-log
git-ls-remote-script	git-ls-remote
git-merge-one-file-script	git-merge-one-file
git-octopus-script	git-octopus
git-parse-remote-script	git-parse-remote
git-prune-script	git-prune
git-pull-script	git-pull
git-push-script	git-push
git-rebase-script	git-rebase
git-relink-script	git-relink
git-rename-script	git-rename
git-repack-script	git-repack
git-request-pull-script	git-request-pull
git-reset-script	git-reset
git-resolve-script	git-resolve
git-revert-script	git-revert
git-sh-setup-script	git-sh-setup
git-status-script	git-status
git-tag-script	git-tag
git-verify-tag-script	git-verify-tag
git-http-pull	git-http-fetch
git-local-pull	git-local-fetch
git-checkout-cache	git-checkout-index
git-diff-cache	git-diff-index
git-merge-cache	git-merge-index
git-update-cache	git-update-index
git-convert-cache	git-convert-objects
git-fsck-cache	git-fsck-objects
EOF

# These two are a bit more than symlinks now.
# git-ssh-push	git-ssh-upload
# git-ssh-pull	git-ssh-fetch
