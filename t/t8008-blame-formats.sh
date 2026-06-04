#!/bin/sh

test_description='blame output in various formats on a simple case'

. ./test-lib.sh

test_expect_success 'setup' '
	echo a >file &&
	git add file &&
	test_tick &&
	git commit -m one &&
	echo b >>file &&
	echo c >>file &&
	echo d >>file &&
	test_tick &&
	git commit -a -m two &&
	ID1=$(git rev-parse HEAD^) &&
	shortID1="^$(git rev-parse HEAD^ |cut -c 1-17)" &&
	ID2=$(git rev-parse HEAD) &&
	shortID2="$(git rev-parse HEAD |cut -c 1-18)"
'

cat >expect <<EOF
$shortID1 (A U Thor 2005-04-07 15:13:13 -0700 1) a
$shortID2 (A U Thor 2005-04-07 15:14:13 -0700 2) b
$shortID2 (A U Thor 2005-04-07 15:14:13 -0700 3) c
$shortID2 (A U Thor 2005-04-07 15:14:13 -0700 4) d
EOF
test_expect_success 'normal blame output' '
	git blame --abbrev=17 file >actual &&
	test_cmp expect actual
'

COMMIT1="author A U Thor
author-mail <author@example.com>
author-time 1112911993
author-tz -0700
committer C O Mitter
committer-mail <committer@example.com>
committer-time 1112911993
committer-tz -0700
summary one
boundary
filename file"
COMMIT2="author A U Thor
author-mail <author@example.com>
author-time 1112912053
author-tz -0700
committer C O Mitter
committer-mail <committer@example.com>
committer-time 1112912053
committer-tz -0700
summary two
previous $ID1 file
filename file"

cat >expect <<EOF
$ID1 1 1 1
$COMMIT1
	a
$ID2 2 2 3
$COMMIT2
	b
$ID2 3 3
	c
$ID2 4 4
	d
EOF
test_expect_success 'blame --porcelain output' '
	git blame --porcelain file >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
$ID1 1 1 1
$COMMIT1
	a
$ID2 2 2 3
$COMMIT2
	b
$ID2 3 3
$COMMIT2
	c
$ID2 4 4
$COMMIT2
	d
EOF
test_expect_success 'blame --line-porcelain output' '
	git blame --line-porcelain file >actual &&
	test_cmp expect actual
'

test_expect_success '--porcelain detects first non-blank line as subject' '
	(
		GIT_INDEX_FILE=.git/tmp-index &&
		export GIT_INDEX_FILE &&
		echo "This is it" >single-file &&
		git add single-file &&
		tree=$(git write-tree) &&
		commit=$(printf "%s\n%s\n%s\n\n\n  \noneline\n\nbody\n" \
			"tree $tree" \
			"author A <a@b.c> 123456789 +0000" \
			"committer C <c@d.e> 123456789 +0000" |
		git hash-object -w -t commit --stdin) &&
		git blame --porcelain $commit -- single-file >output &&
		grep "^summary oneline$" output
	)
'

test_done
