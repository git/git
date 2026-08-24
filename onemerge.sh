#!/bin/sh
# between master..seen, are there merges that bring in 
# more than one topic at a time?

export LANG=C LC_ALL=C

endpoint=${1-seen}

cnt1=$(git lgf --grep="Merge branch '" master..$endpoint | wc -l)
cnt2=$(git branch --no-merged master --merged $endpoint '??/*' | wc -l)

if test $cnt1 -eq $cnt2
then
	exit 0
fi

tmp=/var/tmp/e.$$
rm -f "$tmp.1" "$tmp.2" &&
prev= &&
trap 'rm -f "$tmp."*' 0 || exit

git rev-list --merges --first-parent master..$endpoint |
while read commit
do
	# $tmp.1 has remaining topics after the merge we are looking at.
	# $tmp.2 has remaining topics after the previous merge that is
	# a descendant of the current merge.

	git branch --list --no-merged "$commit" '??/*' >"$tmp.1"
	if test -f "$tmp.2" && test -n "$prev"
	then
		cnt=$(comm -23 "$tmp.1" "$tmp.2" | wc -l)
		if test $cnt != 1
		then
			echo Merges multiple topics
			git show -s --format="* %s" "$prev"
			comm -23 "$tmp.1" "$tmp.2"
			exit 1
		fi
	fi
	mv -f "$tmp.1" "$tmp.2"
	prev=$commit
done
