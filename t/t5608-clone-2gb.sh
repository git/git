#!/bin/sh

test_description='Test cloning a repository larger than 2 gigabyte'
. ./test-lib.sh

if ! test_bool_env GIT_TEST_CLONE_2GB false
then
	say 'Skipping expensive 2GB clone test; enable it with GIT_TEST_CLONE_2GB=t'
else
	test_set_prereq CLONE_2GB
fi

test_expect_success CLONE_2GB 'setup' '

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
	 echo "commit refs/heads/master" &&
	 echo "author A U Thor <author@email.com> 123456789 +0000" &&
	 echo "committer C O Mitter <committer@email.com> 123456789 +0000" &&
	 echo "data 5" &&
	 echo ">2gb" &&
	 cat commit) |
	git fast-import --big-file-threshold=2 &&
	test ! -f exit-status

'

test_expect_success CLONE_2GB 'clone - bare' '

	git clone --bare --no-hardlinks . clone-bare

'

test_expect_success CLONE_2GB 'clone - with worktree, file:// protocol' '

	git clone "file://$(pwd)" clone-wt

'

test_done
