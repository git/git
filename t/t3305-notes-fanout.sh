#!/bin/sh

test_description='Test that adding many notes triggers automatic fanout restructuring'

. ./test-lib.sh

test_expect_success 'creating many notes with git-notes' '
	num_notes=300 &&
	i=0 &&
	while test $i -lt $num_notes
	do
		i=$(($i + 1)) &&
		test_tick &&
		echo "file for commit #$i" > file &&
		git add file &&
		git commit -q -m "commit #$i" &&
		git notes edit -m "note #$i" || return 1
	done
'

test_expect_success 'many notes created correctly with git-notes' '
	git log | grep "^    " > output &&
	i=300 &&
	while test $i -gt 0
	do
		echo "    commit #$i" &&
		echo "    note #$i" &&
		i=$(($i - 1));
	done > expect &&
	test_cmp expect output
'

test_expect_success 'many notes created with git-notes triggers fanout' '
	# Expect entire notes tree to have a fanout == 1
	git ls-tree -r --name-only refs/notes/commits |
	while read path
	do
		case "$path" in
		??/??????????????????????????????????????)
			: true
			;;
		*)
			echo "Invalid path \"$path\"" &&
			return 1
			;;
		esac
	done
'

test_done
