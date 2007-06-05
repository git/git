#!/bin/sh

test_description='git-filter-branch'
. ./test-lib.sh

make_commit () {
	lower=$(echo $1 | tr A-Z a-z)
	echo $lower > $lower
	git add $lower
	test_tick
	git commit -m $1
	git tag $1
}

test_expect_success 'setup' '
	make_commit A
	make_commit B
	git checkout -b branch B
	make_commit D
	make_commit E
	git checkout master
	make_commit C
	git checkout branch
	git merge C
	git tag F
	make_commit G
	make_commit H
'

H=$(git-rev-parse H)

test_expect_success 'rewrite identically' '
	git-filter-branch H2
'

test_expect_success 'result is really identical' '
	test $H = $(git-rev-parse H2)
'

test_expect_success 'rewrite, renaming a specific file' '
	git-filter-branch --tree-filter "mv d doh || :" H3
'

test_expect_success 'test that the file was renamed' '
	test d = $(git show H3:doh)
'

test_done
