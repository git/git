#!/bin/sh

test_description='but repack works correctly'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

fsha1=
csha1=
tsha1=

test_expect_success '-A with -d option leaves unreachable objects unpacked' '
	echo content > file1 &&
	but add . &&
	test_tick &&
	but cummit -m initial_cummit &&
	# create a transient branch with unique content
	but checkout -b transient_branch &&
	echo more content >> file1 &&
	# record the objects created in the database for file, cummit, tree
	fsha1=$(but hash-object file1) &&
	test_tick &&
	but cummit -a -m more_content &&
	csha1=$(but rev-parse HEAD^{cummit}) &&
	tsha1=$(but rev-parse HEAD^{tree}) &&
	but checkout main &&
	echo even more content >> file1 &&
	test_tick &&
	but cummit -a -m even_more_content &&
	# delete the transient branch
	but branch -D transient_branch &&
	# pack the repo
	but repack -A -d -l &&
	# verify objects are packed in repository
	test 3 = $(but verify-pack -v -- .but/objects/pack/*.idx |
		   egrep "^($fsha1|$csha1|$tsha1) " |
		   sort | uniq | wc -l) &&
	but show $fsha1 &&
	but show $csha1 &&
	but show $tsha1 &&
	# now expire the reflog, while keeping reachable ones but expiring
	# unreachables immediately
	test_tick &&
	sometimeago=$(( $test_tick - 10000 )) &&
	but reflog expire --expire=$sometimeago --expire-unreachable=$test_tick --all &&
	# and repack
	but repack -A -d -l &&
	# verify objects are retained unpacked
	test 0 = $(but verify-pack -v -- .but/objects/pack/*.idx |
		   egrep "^($fsha1|$csha1|$tsha1) " |
		   sort | uniq | wc -l) &&
	but show $fsha1 &&
	but show $csha1 &&
	but show $tsha1
'

compare_mtimes ()
{
	read tref &&
	while read t; do
		test "$tref" = "$t" || return 1
	done
}

test_expect_success '-A without -d option leaves unreachable objects packed' '
	fsha1path=$(echo "$fsha1" | sed -e "s|\(..\)|\1/|") &&
	fsha1path=".but/objects/$fsha1path" &&
	csha1path=$(echo "$csha1" | sed -e "s|\(..\)|\1/|") &&
	csha1path=".but/objects/$csha1path" &&
	tsha1path=$(echo "$tsha1" | sed -e "s|\(..\)|\1/|") &&
	tsha1path=".but/objects/$tsha1path" &&
	but branch transient_branch $csha1 &&
	but repack -a -d -l &&
	test ! -f "$fsha1path" &&
	test ! -f "$csha1path" &&
	test ! -f "$tsha1path" &&
	test 1 = $(ls -1 .but/objects/pack/pack-*.pack | wc -l) &&
	packfile=$(ls .but/objects/pack/pack-*.pack) &&
	but branch -D transient_branch &&
	test_tick &&
	but repack -A -l &&
	test ! -f "$fsha1path" &&
	test ! -f "$csha1path" &&
	test ! -f "$tsha1path" &&
	but show $fsha1 &&
	but show $csha1 &&
	but show $tsha1
'

test_expect_success 'unpacked objects receive timestamp of pack file' '
	tmppack=".but/objects/pack/tmp_pack" &&
	ln "$packfile" "$tmppack" &&
	but repack -A -l -d &&
	test-tool chmtime --get "$tmppack" "$fsha1path" "$csha1path" "$tsha1path" \
		> mtimes &&
	compare_mtimes < mtimes
'

test_expect_success 'do not bother loosening old objects' '
	obj1=$(echo one | but hash-object -w --stdin) &&
	obj2=$(echo two | but hash-object -w --stdin) &&
	pack1=$(echo $obj1 | but pack-objects .but/objects/pack/pack) &&
	pack2=$(echo $obj2 | but pack-objects .but/objects/pack/pack) &&
	but prune-packed &&
	but cat-file -p $obj1 &&
	but cat-file -p $obj2 &&
	test-tool chmtime =-86400 .but/objects/pack/pack-$pack2.pack &&
	but repack -A -d --unpack-unreachable=1.hour.ago &&
	but cat-file -p $obj1 &&
	test_must_fail but cat-file -p $obj2
'

test_expect_success 'keep packed objects found only in index' '
	echo my-unique-content >file &&
	but add file &&
	but cummit -m "make it reachable" &&
	but gc &&
	but reset HEAD^ &&
	but reflog expire --expire=now --all &&
	but add file &&
	test-tool chmtime =-86400 .but/objects/pack/* &&
	but gc --prune=1.hour.ago &&
	but cat-file blob :file
'

test_expect_success 'repack -k keeps unreachable packed objects' '
	# create packed-but-unreachable object
	sha1=$(echo unreachable-packed | but hash-object -w --stdin) &&
	pack=$(echo $sha1 | but pack-objects .but/objects/pack/pack) &&
	but prune-packed &&

	# -k should keep it
	but repack -adk &&
	but cat-file -p $sha1 &&

	# and double check that without -k it would have been removed
	but repack -ad &&
	test_must_fail but cat-file -p $sha1
'

test_expect_success 'repack -k packs unreachable loose objects' '
	# create loose unreachable object
	sha1=$(echo would-be-deleted-loose | but hash-object -w --stdin) &&
	objpath=.but/objects/$(echo $sha1 | sed "s,..,&/,") &&
	test_path_is_file $objpath &&

	# and confirm that the loose object goes away, but we can
	# still access it (ergo, it is packed)
	but repack -adk &&
	test_path_is_missing $objpath &&
	but cat-file -p $sha1
'

test_done
