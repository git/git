#!/bin/sh

TOPICS=$(mktemp)
trap 'rm -f "$TOPICS"' 0 1 2 3 15

: ${fromto:=master..seen}

sed -e 's/^ [-+] //' |
while read topic rest
do
	case "$topic" in
	"" | "<"*) continue ;;
	esac

	echo "branch '$topic' into jch"
	echo "branch '$topic' into seen"
	echo "branch '$topic' into HEAD"
	echo "branch '$topic'"
done >"$TOPICS"

test -d Meta || cd .. && test -d Meta || exit 1

git log --first-parent --oneline "$fromto" |
grep -f "$TOPICS" |
tac |
sed -ne "s|^[0-9a-f]* Merge branch '\([^']*\)'\( into \(.*\)\)\?$|\3	\1|p"

exit $?
