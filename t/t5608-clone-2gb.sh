#!/bin/sh

test_description='Test cloning a repository larger than 2 gigabyte'
. ./test-lib.sh

if ! test_bool_env GIT_TEST_CLONE_2GB false
then
	skip_all='expensive 2GB clone test; enable with GIT_TEST_CLONE_2GB=true'
	test_done
fi

test_expect_success 'setup' '

	git config pack.compression 0 &&
	git config pack.depth 0 &&
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

'

test_expect_success 'clone - bare' '

	git clone --bare --no-hardlinks . clone-bare

'

test_expect_success 'clone - with worktree, file:// protocol' '

	git clone "file://$(pwd)" clone-wt

'

test_expect_success SIZE_T_IS_64BIT,EXPENSIVE 'set up repo with >4GB object' '
	large_blob_size=$((4*1024*1024*1024+1)) &&
	git init --bare 4gb-repo &&
	head_oid=$(test-tool synthesize pack \
		--reachable-large "$large_blob_size" \
		4gb-repo/objects/pack/test.pack) &&
	git -C 4gb-repo index-pack objects/pack/test.pack &&
	git -C 4gb-repo update-ref refs/heads/main $head_oid &&
	git -C 4gb-repo symbolic-ref HEAD refs/heads/main
'

test_expect_success SIZE_T_IS_64BIT,EXPENSIVE 'clone >4GB object via unpack-objects' '
	# The synthesized pack has five objects, so a large unpack limit keeps
	# fetch-pack on the unpack-objects path.
	git -c fetch.unpackLimit=100 clone --bare \
		"file://$(pwd)/4gb-repo" 4gb-clone-unpack &&

	# Verify the large blob survived the clone by comparing its OID
	# between source and clone.  We cannot use "cat-file -s" because
	# object_info.sizep is still unsigned long, which truncates >4GB
	# sizes on Windows.  OID equality proves content integrity since
	# the clone already verified checksums via index-pack/unpack-objects.
	source_blob=$(git -C 4gb-repo rev-parse main^:file) &&
	clone_blob=$(git -C 4gb-clone-unpack rev-parse main^:file) &&
	test "$source_blob" = "$clone_blob"
'

test_expect_success SIZE_T_IS_64BIT,EXPENSIVE 'clone with >4GB object via index-pack' '
	# Force fetch-pack to hand the pack to index-pack instead.
	git -c fetch.unpackLimit=1 clone --bare \
		"file://$(pwd)/4gb-repo" 4gb-clone-index &&

	source_blob=$(git -C 4gb-repo rev-parse main^:file) &&
	clone_blob=$(git -C 4gb-clone-index rev-parse main^:file) &&
	test "$source_blob" = "$clone_blob"
'

test_done
