#!/bin/sh

test_description='blame output in various formats on a simple case'
. ./test-lib.sh

test_expect_success 'setup' '
	echo a >file &&
	but add file &&
	test_tick &&
	but cummit -m one &&
	echo b >>file &&
	echo c >>file &&
	echo d >>file &&
	test_tick &&
	but cummit -a -m two &&
	ID1=$(but rev-parse HEAD^) &&
	shortID1="^$(but rev-parse HEAD^ |cut -c 1-17)" &&
	ID2=$(but rev-parse HEAD) &&
	shortID2="$(but rev-parse HEAD |cut -c 1-18)"
'

cat >expect <<EOF
$shortID1 (A U Thor 2005-04-07 15:13:13 -0700 1) a
$shortID2 (A U Thor 2005-04-07 15:14:13 -0700 2) b
$shortID2 (A U Thor 2005-04-07 15:14:13 -0700 3) c
$shortID2 (A U Thor 2005-04-07 15:14:13 -0700 4) d
EOF
test_expect_success 'normal blame output' '
	but blame --abbrev=17 file >actual &&
	test_cmp expect actual
'

cummit1="author A U Thor
author-mail <author@example.com>
author-time 1112911993
author-tz -0700
cummitter C O Mitter
cummitter-mail <cummitter@example.com>
cummitter-time 1112911993
cummitter-tz -0700
summary one
boundary
filename file"
cummit2="author A U Thor
author-mail <author@example.com>
author-time 1112912053
author-tz -0700
cummitter C O Mitter
cummitter-mail <cummitter@example.com>
cummitter-time 1112912053
cummitter-tz -0700
summary two
previous $ID1 file
filename file"

cat >expect <<EOF
$ID1 1 1 1
$cummit1
	a
$ID2 2 2 3
$cummit2
	b
$ID2 3 3
	c
$ID2 4 4
	d
EOF
test_expect_success 'blame --porcelain output' '
	but blame --porcelain file >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
$ID1 1 1 1
$cummit1
	a
$ID2 2 2 3
$cummit2
	b
$ID2 3 3
$cummit2
	c
$ID2 4 4
$cummit2
	d
EOF
test_expect_success 'blame --line-porcelain output' '
	but blame --line-porcelain file >actual &&
	test_cmp expect actual
'

test_expect_success '--porcelain detects first non-blank line as subject' '
	(
		BUT_INDEX_FILE=.but/tmp-index &&
		export BUT_INDEX_FILE &&
		echo "This is it" >single-file &&
		but add single-file &&
		tree=$(but write-tree) &&
		cummit=$(printf "%s\n%s\n%s\n\n\n  \noneline\n\nbody\n" \
			"tree $tree" \
			"author A <a@b.c> 123456789 +0000" \
			"cummitter C <c@d.e> 123456789 +0000" |
		but hash-object -w -t cummit --stdin) &&
		but blame --porcelain $cummit -- single-file >output &&
		grep "^summary oneline$" output
	)
'

test_done
