#!/bin/sh

USAGE='[-n] [--] [<head>...]'
. git-sh-setup

dryrun=
echo=
while case "$#" in 0) break ;; esac
do
    case "$1" in
    -n) dryrun=-n echo=echo ;;
    --) break ;;
    -*) usage ;;
    *)  break ;;
    esac
    shift;
done

sync
case "$#" in
0) git-fsck-objects --full --cache --unreachable ;;
*) git-fsck-objects --full --cache --unreachable $(git-rev-parse --all) "$@" ;;
esac |

sed -ne '/unreachable /{
    s/unreachable [^ ][^ ]* //
    s|\(..\)|\1/|p
}' | {
	cd "$GIT_OBJECT_DIRECTORY" || exit
	xargs $echo rm -f
	rmdir 2>/dev/null [0-9a-f][0-9a-f]
}

git-prune-packed $dryrun

if redundant=$(git-pack-redundant --all 2>/dev/null) && test "" != "$redundant"
then
	if test "" = "$dryrun"
	then
		echo "$redundant" | xargs rm -f
	else
		echo rm -f "$redundant"
	fi
fi
