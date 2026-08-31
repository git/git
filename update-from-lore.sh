#!/bin/sh

afresh () {
	if git symbolic-ref -q HEAD
	then
		echo >&2 "HEAD not detached"
		exit 1
	fi
	b4 am -o- -t "$1" |
	tee ./+b4am.mbx |
	git am -s3 && rm -f ./+b4am.mbx

	exit $?
}

redo_or_afresh () {
	if git show-ref --quiet --verify "refs/heads/$1"
	then
		git checkout "$1" || exit
		return
	else
		afresh "$1"
		return
	fi
}

force=
while case "$#" in 0) break;; esac
do
	case "$1" in
	-f | --force)
		force=--force ;;
	-*)
		echo >&2 "$0: unknown option '$1'"
		exit 1 ;;
	*)
		break ;;
	esac
	shift
done

case "$#" in
0)	: happy ;;
1)	redo_or_afresh "$1" ;;
*)	echo >&2 "$0: extra arguments" ;;
esac

above_master=$(git rev-list --first-parent master.. | wc -l)
above_next=$(git rev-list --first-parent next.. | wc -l)
if test "$above_master" != "$above_next"
then
	if test "$force" = "--force"
	then
		echo >&2 warning: some patches are already in next
	else
		echo >&2 fatal: some patches are already in next
		exit 1
	fi
fi

MID=
OLD=$(git rev-parse HEAD)
git detach ${force+--force} || exit
git rev-list HEAD..$OLD |
while read commit
do
	git notes --ref=amlog show $commit
done |
sed -n -e '/^Message-Id: /{
	s///p
	q
}' |
xargs -n1 b4 am -o- -T |
tee ./+b4am.mbx |
git am -s3 && rm -f ./+b4am.mbx &&

git range-diff --notes=amlog --crea=999 @{-1}...
echo -n >&2 "Update [y/n]? "
while read yesno
do
	case "$yesno" in
	[Yy]*)
		git co -B @{-1}
		exit 0
		;;
	[Nn]*)
		exit 1
		;;
	esac
done
