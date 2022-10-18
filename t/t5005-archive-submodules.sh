#!/bin/sh

test_description='git archive --recurse-submodules test'

. ./test-lib.sh

check_tar() {
	tarfile=$1.tar
	listfile=$1.lst
	dir=$1
	dir_with_prefix=$dir/$2

	test_expect_success ' extract tar archive' '
		(mkdir $dir && cd $dir && "$TAR" xf -) <$tarfile
	'
}

check_added() {
	dir=$1
	path_in_fs=$2
	path_in_archive=$3

	test_expect_success " validate extra file $path_in_archive" '
		test -f $dir/$path_in_archive &&
		diff -r $path_in_fs $dir/$path_in_archive
	'
}

check_not_added() {
	dir=$1
	path_in_archive=$2

	test_expect_success " validate unpresent file $path_in_archive" '
		! test -f $dir/$path_in_archive &&
		! test -d $dir/$path_in_archive
	'
}

test_expect_success 'setup' '
	rm -rf repo_with_submodules submodule1 uninited_repo_with_submodules &&
	git init repo_with_submodules &&
	git init submodule1 &&
	(
		cd submodule1 &&
		echo "dir1/sub1/file1.txt" > "file1.txt" &&
		git add file1.txt &&
		git commit -m "initialize with file1.txt"
	) &&
	(
	    cd repo_with_submodules &&
	    echo "file2" > file2.txt &&
	    git add file2.txt &&
	    git commit -m "initialize with file2.txt" &&
	    mkdir -p dir1 &&
	    git submodule add ../submodule1 dir1/sub1 &&
	    git commit -m "add submodule1"
	) &&
	git clone repo_with_submodules uninited_repo_with_submodules
'

test_expect_success 'archive without recurse, non-init' '
	git -C uninited_repo_with_submodules archive -v HEAD >b.tar
'

check_tar b
check_added b uninited_repo_with_submodules/file2.txt file2.txt
check_not_added b uninited_repo_with_submodules/dir1/sub1/file1.txt

# It is expected that --recurse-submodules will not work if submodules are not
# initialized.
test_expect_success 'archive with recurse, non-init' '
	! git -C uninited_repo_with_submodules archive --recurse-submodules -v HEAD >b2-err.tar
'

test_expect_success 'archive with recurse, init' '
	git -C repo_with_submodules archive --recurse-submodules -v HEAD >b3.tar
'

check_tar b3
check_added b3 repo_with_submodules/file2.txt file2.txt
check_added b3 repo_with_submodules/dir1/sub1/file1.txt dir1/sub1/file1.txt

test_done
