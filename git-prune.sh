#!/bin/sh

. git-sh-setup || die "Not a git archive"

dryrun=
echo=
while case "$#" in 0) break ;; esac
do
    case "$1" in
    -n) dryrun=-n echo=echo ;;
    --) break ;;
    -*) echo >&2 "usage: git-prune [ -n ] [ heads... ]"; exit 1 ;;
    *)  break ;;
    esac
    shift;
done

git-fsck-objects --full --cache --unreachable "$@" |
sed -ne '/unreachable /{
    s/unreachable [^ ][^ ]* //
    s|\(..\)|\1/|p
}' | {
	cd "$GIT_OBJECT_DIRECTORY" || exit
	xargs $echo rm -f
}

git-prune-packed $dryrun
