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

sync
git-fsck-objects --full --cache --unreachable "$@" |
sed -ne '/unreachable /{
    s/unreachable [^ ][^ ]* //
    s|\(..\)|\1/|p
}' | {
	cd "$GIT_OBJECT_DIRECTORY" || exit
	xargs $echo rm -f
	rmdir 2>/dev/null [0-9a-f][0-9a-f]
}

git-prune-packed $dryrun

redundant=$(git-pack-redundant --all)
if test "" != "$redundant"
then
	if test "" = "$dryrun"
	then
		echo "$redundant" | xargs rm -f
	else
		echo rm -f "$redundant"
	fi
fi
