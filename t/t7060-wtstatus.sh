#!/bin/sh

test_description='basic work tree status reporting'

. ./test-lib.sh

test_expect_success setup '
	test_commit A &&
	test_commit B oneside added &&
	git checkout A^0 &&
	test_commit C oneside created
'

test_expect_success 'A/A conflict' '
	git checkout B^0 &&
	test_must_fail git merge C
'

test_expect_success 'Report path with conflict' '
	git diff --cached --name-status >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_expect_success 'Report new path with conflict' '
	git diff --cached --name-status HEAD^ >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

cat >expect <<EOF
# On branch side
# Unmerged paths:
#   (use "git add/rm <file>..." as appropriate to mark resolution)
#
#	deleted by us:      foo
#
no changes added to commit (use "git add" and/or "git commit -a")
EOF

test_expect_success 'M/D conflict does not segfault' '
	mkdir mdconflict &&
	(
		cd mdconflict &&
		git init &&
		test_commit initial foo "" &&
		test_commit modify foo foo &&
		git checkout -b side HEAD^ &&
		git rm foo &&
		git commit -m delete &&
		test_must_fail git merge master &&
		test_must_fail git commit --dry-run >../actual &&
		test_cmp ../expect ../actual &&
		git status >../actual &&
		test_cmp ../expect ../actual
	)
'

test_done
