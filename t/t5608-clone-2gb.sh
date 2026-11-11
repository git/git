#!/bin/sh

test_description='Test cloning a repository larger than 2 gigabyte'
. ./test-lib.sh

if ! test_bool_env GIT_TEST_CLONE_2GB false
then
	skip_all='expensive 2GB clone test; enable with GIT_TEST_CLONE_2GB=true'
	test_done
fi

size_2gb=2147483648
size_4gb=4294967296

# The tests below are only meaningful if the packs involved really are
# that large, so check it explicitly instead of trusting the setup.
test_has_pack_larger_than () {
	max=$(test-tool path-utils file-size "$1"/*.pack | sort -n | tail -n 1) &&
	max=${max:-0} &&
	if test "$max" -le "$2"
	then
		echo >&2 "largest pack in '$1' is $max bytes, expected more than $2" &&
		return 1
	fi
}

test_expect_success 'setup' '
	git init 2gb-repo &&
	(
		cd 2gb-repo &&
		git config pack.compression 0 &&
		git config pack.depth 0 &&
		git config fastimport.unpackLimit 0 &&
		blobsize=$((100*1024*1024)) &&
		blobcount=$((2*1024*1024*1024/$blobsize+1)) &&
		i=1 &&
		(while test $i -le $blobcount
		 do
			printf "Generating blob $i/$blobcount\r" >&2 &&
			printf "blob\nmark :$i\ndata $blobsize\n" &&
			#test-tool genrandom $i $blobsize &&
			printf "%-${blobsize}s" $i &&
			echo "M 100644 :$i $i" >> commit &&
			i=$(($i+1)) ||
			echo $? > exit-status
		 done &&
		 echo "commit refs/heads/main" &&
		 echo "author A U Thor <author@email.com> 123456789 +0000" &&
		 echo "committer C O Mitter <committer@email.com> 123456789 +0000" &&
		 echo "data 5" &&
		 echo ">2gb" &&
		 cat commit) |
		git fast-import --big-file-threshold=2 &&
		test ! -f exit-status
	)
'

test_expect_success 'setup produces a pack larger than 2GB' '
	test_has_pack_larger_than 2gb-repo/.git/objects/pack $size_2gb
'

test_expect_success 'clone - bare' '
	test_when_finished rm -rf clone-bare &&
	git clone --bare --no-hardlinks 2gb-repo clone-bare &&
	test_has_pack_larger_than clone-bare/objects/pack $size_2gb
'

test_expect_success 'clone - with worktree, file:// protocol' '
	test_when_finished rm -rf clone-wt &&
	git clone "file://$(pwd)/2gb-repo" clone-wt &&
	test_has_pack_larger_than clone-wt/.git/objects/pack $size_2gb
'

rm -rf 2gb-repo 2>/dev/null

test_expect_success SIZE_T_IS_64BIT,EXPENSIVE 'set up repo with >4GB object' '
	large_blob_size=$((4*1024*1024*1024+1)) &&
	git init --bare 4gb-repo &&
	head_oid=$(test-tool synthesize pack \
		--reachable-large "$large_blob_size" \
		4gb-repo/objects/pack/test.pack) &&
	git -C 4gb-repo index-pack objects/pack/test.pack &&
	git -C 4gb-repo update-ref refs/heads/main $head_oid &&
	git -C 4gb-repo symbolic-ref HEAD refs/heads/main &&
	test_has_pack_larger_than 4gb-repo/objects/pack $size_4gb
'

test_expect_success SIZE_T_IS_64BIT,EXPENSIVE 'fetch >4GB object via unpack-objects' '
	test_when_finished rm -rf 4gb-fetch-unpack &&
	# The synthesized pack has five objects, so a large unpack limit keeps
	# fetch-pack on the unpack-objects path.  This needs "fetch", as
	# "clone" always keeps the received pack.
	git init --bare 4gb-fetch-unpack &&
	GIT_TRACE2_EVENT="$(pwd)/trace-unpack" \
		git -C 4gb-fetch-unpack -c fetch.unpackLimit=100 fetch \
		"file://$(pwd)/4gb-repo" main:refs/heads/main &&
	test_subcommand_flex git unpack-objects <trace-unpack &&

	# Verify the large blob survived the fetch by comparing its OID
	# between source and destination.  We cannot use "cat-file -s" because
	# object_info.sizep is still unsigned long, which truncates >4GB
	# sizes on Windows.  OID equality proves content integrity since
	# the fetch already verified checksums via unpack-objects.
	source_blob=$(git -C 4gb-repo rev-parse main^:file) &&
	fetched_blob=$(git -C 4gb-fetch-unpack rev-parse main^:file) &&
	test "$source_blob" = "$fetched_blob"
'

test_expect_success SIZE_T_IS_64BIT,EXPENSIVE 'clone with >4GB object via index-pack' '
	test_when_finished rm -rf 4gb-clone-index &&
	# Force fetch-pack to hand the pack to index-pack instead.
	git -c fetch.unpackLimit=1 clone --bare \
		"file://$(pwd)/4gb-repo" 4gb-clone-index &&
	test_has_pack_larger_than 4gb-clone-index/objects/pack $size_4gb &&

	source_blob=$(git -C 4gb-repo rev-parse main^:file) &&
	clone_blob=$(git -C 4gb-clone-index rev-parse main^:file) &&
	test "$source_blob" = "$clone_blob"
'

test_done
