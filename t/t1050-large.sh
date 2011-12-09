#!/bin/sh
# Copyright (c) 2011, Google Inc.

test_description='adding and checking out large blobs'

. ./test-lib.sh

test_expect_success setup '
	git config core.bigfilethreshold 200k &&
	echo X | dd of=large1 bs=1k seek=2000 &&
	echo X | dd of=large2 bs=1k seek=2000 &&
	echo X | dd of=large3 bs=1k seek=2000 &&
	echo Y | dd of=huge bs=1k seek=2500
'

test_expect_success 'add a large file or two' '
	git add large1 huge large2 &&
	# make sure we got a single packfile and no loose objects
	bad= count=0 idx= &&
	for p in .git/objects/pack/pack-*.pack
	do
		count=$(( $count + 1 ))
		if test -f "$p" && idx=${p%.pack}.idx && test -f "$idx"
		then
			continue
		fi
		bad=t
	done &&
	test -z "$bad" &&
	test $count = 1 &&
	cnt=$(git show-index <"$idx" | wc -l) &&
	test $cnt = 2 &&
	for l in .git/objects/??/??????????????????????????????????????
	do
		test -f "$l" || continue
		bad=t
	done &&
	test -z "$bad" &&

	# attempt to add another copy of the same
	git add large3 &&
	bad= count=0 &&
	for p in .git/objects/pack/pack-*.pack
	do
		count=$(( $count + 1 ))
		if test -f "$p" && idx=${p%.pack}.idx && test -f "$idx"
		then
			continue
		fi
		bad=t
	done &&
	test -z "$bad" &&
	test $count = 1
'

test_expect_success 'checkout a large file' '
	large1=$(git rev-parse :large1) &&
	git update-index --add --cacheinfo 100644 $large1 another &&
	git checkout another &&
	cmp large1 another ;# this must not be test_cmp
'

test_expect_success 'packsize limit' '
	test_create_repo mid &&
	(
		cd mid &&
		git config core.bigfilethreshold 64k &&
		git config pack.packsizelimit 256k &&

		# mid1 and mid2 will fit within 256k limit but
		# appending mid3 will bust the limit and will
		# result in a separate packfile.
		test-genrandom "a" $(( 66 * 1024 )) >mid1 &&
		test-genrandom "b" $(( 80 * 1024 )) >mid2 &&
		test-genrandom "c" $(( 128 * 1024 )) >mid3 &&
		git add mid1 mid2 mid3 &&

		count=0
		for pi in .git/objects/pack/pack-*.idx
		do
			test -f "$pi" && count=$(( $count + 1 ))
		done &&
		test $count = 2 &&

		(
			git hash-object --stdin <mid1
			git hash-object --stdin <mid2
			git hash-object --stdin <mid3
		) |
		sort >expect &&

		for pi in .git/objects/pack/pack-*.idx
		do
			git show-index <"$pi"
		done |
		sed -e "s/^[0-9]* \([0-9a-f]*\) .*/\1/" |
		sort >actual &&

		test_cmp expect actual
	)
'

test_done
