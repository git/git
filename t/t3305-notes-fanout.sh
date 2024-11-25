#!/bin/sh

test_description='Test that adding/removing many notes triggers automatic fanout restructuring'

. ./test-lib.sh

path_has_fanout() {
	path=$1 &&
	fanout=$2 &&
	after_last_slash=$(($(test_oid hexsz) - $fanout * 2)) &&
	echo $path | grep -q -E "^([0-9a-f]{2}/){$fanout}[0-9a-f]{$after_last_slash}$"
}

touched_one_note_with_fanout() {
	notes_commit=$1 &&
	modification=$2 &&  # 'A' for addition, 'D' for deletion
	fanout=$3 &&
	diff=$(git diff-tree --no-commit-id --name-status --root -r $notes_commit) &&
	path=$(echo $diff | sed -e "s/^$modification[\t ]//") &&
	path_has_fanout "$path" $fanout;
}

all_notes_have_fanout() {
	notes_commit=$1 &&
	fanout=$2 &&
	git ls-tree -r --name-only $notes_commit |
	while read path
	do
		path_has_fanout $path $fanout || return 1
	done
}

test_expect_success 'tweak test environment' '
	git checkout -b nondeterminism &&
	test_commit A &&
	git checkout --orphan with_notes;
'

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
		git notes add -m "note #$i" || return 1
	done
'

test_expect_success 'many notes created correctly with git-notes' '
	git log >output.raw &&
	grep "^    " output.raw >output &&
	i=$num_notes &&
	while test $i -gt 0
	do
		echo "    commit #$i" &&
		echo "    note #$i" &&
		i=$(($i - 1)) || return 1
	done > expect &&
	test_cmp expect output
'

test_expect_success 'stable fanout 0 is followed by stable fanout 1' '
	i=$num_notes &&
	fanout=0 &&
	while test $i -gt 0
	do
		i=$(($i - 1)) &&
		if touched_one_note_with_fanout refs/notes/commits~$i A $fanout
		then
			continue
		elif test $fanout -eq 0
		then
			fanout=1 &&
			if all_notes_have_fanout refs/notes/commits~$i $fanout
			then
				echo "Fanout 0 -> 1 at refs/notes/commits~$i" &&
				continue
			fi
		fi &&
		echo "Failed fanout=$fanout check at refs/notes/commits~$i" &&
		git ls-tree -r --name-only refs/notes/commits~$i &&
		return 1
	done &&
	all_notes_have_fanout refs/notes/commits 1
'

test_expect_success 'deleting most notes with git-notes' '
	remove_notes=285 &&
	i=0 &&
	git rev-list HEAD >revs &&
	while test $i -lt $remove_notes && read sha1
	do
		i=$(($i + 1)) &&
		test_tick &&
		git notes remove "$sha1" || return 1
	done <revs
'

test_expect_success 'most notes deleted correctly with git-notes' '
	git log HEAD~$remove_notes | grep "^    " > output &&
	i=$(($num_notes - $remove_notes)) &&
	while test $i -gt 0
	do
		echo "    commit #$i" &&
		echo "    note #$i" &&
		i=$(($i - 1)) || return 1
	done > expect &&
	test_cmp expect output
'

test_expect_success 'stable fanout 1 is followed by stable fanout 0' '
	i=$remove_notes &&
	fanout=1 &&
	while test $i -gt 0
	do
		i=$(($i - 1)) &&
		if touched_one_note_with_fanout refs/notes/commits~$i D $fanout
		then
			continue
		elif test $fanout -eq 1
		then
			fanout=0 &&
			if all_notes_have_fanout refs/notes/commits~$i $fanout
			then
				echo "Fanout 1 -> 0 at refs/notes/commits~$i" &&
				continue
			fi
		fi &&
		echo "Failed fanout=$fanout check at refs/notes/commits~$i" &&
		git ls-tree -r --name-only refs/notes/commits~$i &&
		return 1
	done &&
	all_notes_have_fanout refs/notes/commits 0
'

test_done
