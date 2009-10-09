#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test commit notes index (expensive!)'

. ./test-lib.sh

test -z "$GIT_NOTES_TIMING_TESTS" && {
	say Skipping timing tests
	test_done
	exit
}

create_repo () {
	number_of_commits=$1
	nr=0
	parent=
	test -d .git || {
	git init &&
	tree=$(git write-tree) &&
	while [ $nr -lt $number_of_commits ]; do
		test_tick &&
		commit=$(echo $nr | git commit-tree $tree $parent) ||
			return
		parent="-p $commit"
		nr=$(($nr+1))
	done &&
	git update-ref refs/heads/master $commit &&
	{
		GIT_INDEX_FILE=.git/temp; export GIT_INDEX_FILE;
		git rev-list HEAD | cat -n | sed "s/^[ 	][ 	]*/ /g" |
		while read nr sha1; do
			blob=$(echo note $nr | git hash-object -w --stdin) &&
			echo $sha1 | sed "s/^/0644 $blob 0	/"
		done | git update-index --index-info &&
		tree=$(git write-tree) &&
		test_tick &&
		commit=$(echo notes | git commit-tree $tree) &&
		git update-ref refs/notes/commits $commit
	} &&
	git config core.notesRef refs/notes/commits
	}
}

test_notes () {
	count=$1 &&
	git config core.notesRef refs/notes/commits &&
	git log | grep "^    " > output &&
	i=1 &&
	while [ $i -le $count ]; do
		echo "    $(($count-$i))" &&
		echo "    note $i" &&
		i=$(($i+1));
	done > expect &&
	git diff expect output
}

cat > time_notes << \EOF
	mode=$1
	i=1
	while [ $i -lt $2 ]; do
		case $1 in
		no-notes)
			GIT_NOTES_REF=non-existing; export GIT_NOTES_REF
		;;
		notes)
			unset GIT_NOTES_REF
		;;
		esac
		git log >/dev/null
		i=$(($i+1))
	done
EOF

time_notes () {
	for mode in no-notes notes
	do
		echo $mode
		/usr/bin/time sh ../time_notes $mode $1
	done
}

for count in 10 100 1000 10000; do

	mkdir $count
	(cd $count;

	test_expect_success "setup $count" "create_repo $count"

	test_expect_success 'notes work' "test_notes $count"

	test_expect_success 'notes timing' "time_notes 100"
	)
done

test_done
