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
		test_i18ncmp ../expect ../actual &&
		git status >../actual &&
		test_i18ncmp ../expect ../actual
	)
'

test_expect_success 'rename & unmerged setup' '
	git rm -f -r . &&
	cat "$TEST_DIRECTORY/README" >ONE &&
	git add ONE &&
	test_tick &&
	git commit -m "One commit with ONE" &&

	echo Modified >TWO &&
	cat ONE >>TWO &&
	cat ONE >>THREE &&
	git add TWO THREE &&
	sha1=$(git rev-parse :ONE) &&
	git rm --cached ONE &&
	(
		echo "100644 $sha1 1	ONE" &&
		echo "100644 $sha1 2	ONE" &&
		echo "100644 $sha1 3	ONE"
	) | git update-index --index-info &&
	echo Further >>THREE
'

test_expect_success 'rename & unmerged status' '
	git status -suno >actual &&
	cat >expect <<-EOF &&
	UU ONE
	AM THREE
	A  TWO
	EOF
	test_cmp expect actual
'

test_expect_success 'git diff-index --cached shows 2 added + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	A	THREE
	A	TWO
	EOF
	git diff-index --cached --name-status HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'git diff-index --cached -M shows 2 added + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	A	THREE
	A	TWO
	EOF
	git diff-index --cached --name-status HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'git diff-index --cached -C shows 2 copies + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	C	ONE	THREE
	C	ONE	TWO
	EOF
	git diff-index --cached -C --name-status HEAD |
	sed "s/^C[0-9]*/C/g" >actual &&
	test_cmp expected actual
'

test_done
