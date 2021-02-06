#!/bin/sh
# Copyright (c) 2011, Google Inc.

test_description='adding and checking out large blobs'

. ./test-lib.sh

test_expect_success setup '
	# clone does not allow us to pass core.bigfilethreshold to
	# new repos, so set core.bigfilethreshold globally
	git config --global core.bigfilethreshold 200k &&
	printf "%2000000s" X >large1 &&
	cp large1 large2 &&
	cp large1 large3 &&
	printf "%2500000s" Y >huge &&
	GIT_ALLOC_LIMIT=1500k &&
	export GIT_ALLOC_LIMIT
'

# add a large file with different settings
while read expect config
do
	test_expect_success "add with $config" '
		test_when_finished "rm -f .git/objects/pack/pack-*.* .git/index" &&
		git $config add large1 &&
		sz=$(test_file_size .git/objects/pack/pack-*.pack) &&
		case "$expect" in
		small) test "$sz" -le 100000 ;;
		large) test "$sz" -ge 100000 ;;
		esac
	'
done <<\EOF
large -c core.compression=0
small -c core.compression=9
large -c core.compression=0 -c pack.compression=0
large -c core.compression=9 -c pack.compression=0
small -c core.compression=0 -c pack.compression=9
small -c core.compression=9 -c pack.compression=9
large -c pack.compression=0
small -c pack.compression=9
EOF

test_expect_success 'add a large file or two' '
	git add large1 huge large2 &&
	# make sure we got a single packfile and no loose objects
	bad= count=0 idx= &&
	for p in .git/objects/pack/pack-*.pack
	do
		count=$(( $count + 1 ))
		if test_path_is_file "$p" &&
		   idx=${p%.pack}.idx && test_path_is_file "$idx"
		then
			continue
		fi
		bad=t
	done &&
	test -z "$bad" &&
	test $count = 1 &&
	cnt=$(git show-index <"$idx" | wc -l) &&
	test $cnt = 2 &&
	for l in .git/objects/$OIDPATH_REGEX
	do
		test_path_is_file "$l" || continue
		bad=t
	done &&
	test -z "$bad" &&

	# attempt to add another copy of the same
	git add large3 &&
	bad= count=0 &&
	for p in .git/objects/pack/pack-*.pack
	do
		count=$(( $count + 1 ))
		if test_path_is_file "$p" &&
		   idx=${p%.pack}.idx && test_path_is_file "$idx"
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
	test_cmp large1 another
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
		test-tool genrandom "a" $(( 66 * 1024 )) >mid1 &&
		test-tool genrandom "b" $(( 80 * 1024 )) >mid2 &&
		test-tool genrandom "c" $(( 128 * 1024 )) >mid3 &&
		git add mid1 mid2 mid3 &&

		count=0 &&
		for pi in .git/objects/pack/pack-*.idx
		do
			test_path_is_file "$pi" && count=$(( $count + 1 ))
		done &&
		test $count = 2 &&

		(
			git hash-object --stdin <mid1 &&
			git hash-object --stdin <mid2 &&
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

test_expect_success 'diff --raw' '
	git commit -q -m initial &&
	echo modified >>large1 &&
	git add large1 &&
	git commit -q -m modified &&
	git diff --raw HEAD^
'

test_expect_success 'diff --stat' '
	git diff --stat HEAD^ HEAD
'

test_expect_success 'diff' '
	git diff HEAD^ HEAD >actual &&
	grep "Binary files.*differ" actual
'

test_expect_success 'diff --cached' '
	git diff --cached HEAD^ >actual &&
	grep "Binary files.*differ" actual
'

test_expect_success 'hash-object' '
	git hash-object large1
'

test_expect_success 'cat-file a large file' '
	git cat-file blob :large1 >/dev/null
'

test_expect_success 'cat-file a large file from a tag' '
	git tag -m largefile largefiletag :large1 &&
	git cat-file blob largefiletag >/dev/null
'

test_expect_success 'git-show a large file' '
	git show :large1 >/dev/null

'

test_expect_success 'index-pack' '
	git clone file://"$(pwd)"/.git foo &&
	GIT_DIR=non-existent git index-pack --object-format=$(test_oid algo) \
		--strict --verify foo/.git/objects/pack/*.pack
'

test_expect_success 'repack' '
	git repack -ad
'

test_expect_success 'pack-objects with large loose object' '
	SHA1=$(git hash-object huge) &&
	test_create_repo loose &&
	echo $SHA1 | git pack-objects --stdout |
		GIT_ALLOC_LIMIT=0 GIT_DIR=loose/.git git unpack-objects &&
	echo $SHA1 | GIT_DIR=loose/.git git pack-objects pack &&
	test_create_repo packed &&
	mv pack-* packed/.git/objects/pack &&
	GIT_DIR=packed/.git git cat-file blob $SHA1 >actual &&
	test_cmp huge actual
'

test_expect_success 'tar archiving' '
	git archive --format=tar HEAD >/dev/null
'

test_expect_success 'zip archiving, store only' '
	git archive --format=zip -0 HEAD >/dev/null
'

test_expect_success 'zip archiving, deflate' '
	git archive --format=zip HEAD >/dev/null
'

test_expect_success 'fsck large blobs' '
	git fsck 2>err &&
	test_must_be_empty err
'

test_done
