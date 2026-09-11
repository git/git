#!/bin/sh

if test $# = 3 || test $# = 1
then
	commit=$1 base=${2-} target=${3-}
	label=$(git log --oneline -1 "$commit")

	if test -n "$base" && test -n "$target"
	then
		base0=$(git rev-parse "$base^0") &&
		base1=$(git rev-parse "$commit^2") &&
		test "$base0" = "$base1" || {
			echo >&2 "BAD: stale $base in $target"
			exit 2
		}
	fi

	cd ../git.one || exit 1
	if grep "$commit" :basecheck-tested-ok >/dev/null
	then
		exit 0
	elif grep "$commit" :basecheck-tested-ng >/dev/null
	then
		echo >&2 "BAD (again): $label"
		exit 1
	fi

	if test -n "$target"
	then
		echo >&2 "Testing $commit $target"
	else
		echo >&2 "Testing $label"
	fi
	git reset --quiet --hard "$commit" || exit 1

	if git diff --quiet "$commit^1" "$commit"
	then
		echo >&2 "NOOP: $label"
		echo "$commit" >>:basecheck-tested-ok
		exit 0
	elif Meta/Make -s -j32 >:basecheck-errors 2>&1
	then
		echo >&2 "OK: $label"
		echo "$commit" >>:basecheck-tested-ok
		exit 0
	else
		echo "$commit" >>:basecheck-tested-ng
		cat ":basecheck-errors"
		echo >&2 "BAD: $label"
		exit 1
	fi

	exit 0 ;# just in case
fi

git rev-list --max-parents=1 master..seen |
{
	exit=
	while read commit
	do
		"$0" "$commit" </dev/null || exit=$?
	done
	exit $exit
}
exit=$?

git log --oneline --abbrev=-1 --min-parents=2 master..seen |
sed -n -e "s|^\([0-9a-f]*\) Merge branch '\(.*\)' into \(../..*\)$|\1 \2 \3|p" |
{
	while read merge base target
	do
		"$0" "$merge" "$base" "$target" </dev/null || exit=$?
	done
	exit $exit
}
