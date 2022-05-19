#!/bin/sh
#
# Copyright (C) 2006 Martin Waitz <tali@admingilde.org>
#

test_description='test transitive info/alternate entries'
. ./test-lib.sh

test_expect_success 'preparing first repository' '
	test_create_repo A && (
		cd A &&
		echo "Hello World" > file1 &&
		but add file1 &&
		but cummit -m "Initial cummit" file1 &&
		but repack -a -d &&
		but prune
	)
'

test_expect_success 'preparing second repository' '
	but clone -l -s A B && (
		cd B &&
		echo "foo bar" > file2 &&
		but add file2 &&
		but cummit -m "next cummit" file2 &&
		but repack -a -d -l &&
		but prune
	)
'

test_expect_success 'preparing third repository' '
	but clone -l -s B C && (
		cd C &&
		echo "Goodbye, cruel world" > file3 &&
		but add file3 &&
		but cummit -m "one more" file3 &&
		but repack -a -d -l &&
		but prune
	)
'

test_expect_success 'count-objects shows the alternates' '
	cat >expect <<-EOF &&
	alternate: $(pwd)/B/.but/objects
	alternate: $(pwd)/A/.but/objects
	EOF
	but -C C count-objects -v >actual &&
	grep ^alternate: actual >actual.alternates &&
	test_cmp expect actual.alternates
'

# Note: These tests depend on the hard-coded value of 5 as the maximum depth
# we will follow recursion. We start the depth at 0 and count links, not
# repositories. This means that in a chain like:
#
#   A --> B --> C --> D --> E --> F --> G --> H
#      0     1     2     3     4     5     6
#
# we are OK at "G", but break at "H", even though "H" is actually the 8th
# repository, not the 6th, which you might expect. Counting the links allows
# N+1 repositories, and counting from 0 to 5 inclusive allows 6 links.
#
# Note also that we must use "--bare -l" to make the link to H. The "-l"
# ensures we do not do a connectivity check, and the "--bare" makes sure
# we do not try to checkout the result (which needs objects), either of
# which would cause the clone to fail.
test_expect_success 'creating too deep nesting' '
	but clone -l -s C D &&
	but clone -l -s D E &&
	but clone -l -s E F &&
	but clone -l -s F G &&
	but clone --bare -l -s G H
'

test_expect_success 'validity of seventh repository' '
	but -C G fsck
'

test_expect_success 'invalidity of eighth repository' '
	test_must_fail but -C H fsck
'

test_expect_success 'breaking of loops' '
	echo "$(pwd)"/B/.but/objects >>A/.but/objects/info/alternates &&
	but -C C fsck
'

test_expect_success 'that info/alternates is necessary' '
	rm -f C/.but/objects/info/alternates &&
	test_must_fail but -C C fsck
'

test_expect_success 'that relative alternate is possible for current dir' '
	echo "../../../B/.but/objects" >C/.but/objects/info/alternates &&
	but fsck
'

test_expect_success 'that relative alternate is recursive' '
	but -C D fsck
'

# we can reach "A" from our new repo both directly, and via "C".
# The deep/subdir is there to make sure we are not doing a stupid
# pure-text comparison of the alternate names.
test_expect_success 'relative duplicates are eliminated' '
	mkdir -p deep/subdir &&
	but init --bare deep/subdir/duplicate.but &&
	cat >deep/subdir/duplicate.but/objects/info/alternates <<-\EOF &&
	../../../../C/.but/objects
	../../../../A/.but/objects
	EOF
	cat >expect <<-EOF &&
	alternate: $(pwd)/C/.but/objects
	alternate: $(pwd)/B/.but/objects
	alternate: $(pwd)/A/.but/objects
	EOF
	but -C deep/subdir/duplicate.but count-objects -v >actual &&
	grep ^alternate: actual >actual.alternates &&
	test_cmp expect actual.alternates
'

test_expect_success CASE_INSENSITIVE_FS 'dup finding can be case-insensitive' '
	but init --bare insensitive.but &&
	# the previous entry for "A" will have used uppercase
	cat >insensitive.but/objects/info/alternates <<-\EOF &&
	../../C/.but/objects
	../../a/.but/objects
	EOF
	cat >expect <<-EOF &&
	alternate: $(pwd)/C/.but/objects
	alternate: $(pwd)/B/.but/objects
	alternate: $(pwd)/A/.but/objects
	EOF
	but -C insensitive.but count-objects -v >actual &&
	grep ^alternate: actual >actual.alternates &&
	test_cmp expect actual.alternates
'

test_done
