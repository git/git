#!/bin/sh

test_description='blame output in various formats on a simple case'
. ./test-lib.sh

test_expect_success 'setup' '
	echo a >file &&
	git add file
	test_tick &&
	git commit -m one &&
	echo b >>file &&
	echo c >>file &&
	echo d >>file &&
	test_tick &&
	git commit -a -m two
'

cat >expect <<'EOF'
^baf5e0b (A U Thor 2005-04-07 15:13:13 -0700 1) a
8825379d (A U Thor 2005-04-07 15:14:13 -0700 2) b
8825379d (A U Thor 2005-04-07 15:14:13 -0700 3) c
8825379d (A U Thor 2005-04-07 15:14:13 -0700 4) d
EOF
test_expect_success 'normal blame output' '
	git blame file >actual &&
	test_cmp expect actual
'

ID1=baf5e0b3869e0b2b2beb395a3720c7b51eac94fc
COMMIT1='author A U Thor
author-mail <author@example.com>
author-time 1112911993
author-tz -0700
committer C O Mitter
committer-mail <committer@example.com>
committer-time 1112911993
committer-tz -0700
summary one
boundary
filename file'
ID2=8825379dfb8a1267b58e8e5bcf69eec838f685ec
COMMIT2='author A U Thor
author-mail <author@example.com>
author-time 1112912053
author-tz -0700
committer C O Mitter
committer-mail <committer@example.com>
committer-time 1112912053
committer-tz -0700
summary two
previous baf5e0b3869e0b2b2beb395a3720c7b51eac94fc file
filename file'

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

test_done
