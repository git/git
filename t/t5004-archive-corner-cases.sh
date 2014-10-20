#!/bin/sh

test_description='test corner cases of git-archive'
. ./test-lib.sh

test_expect_success 'create commit with empty tree' '
	git commit --allow-empty -m foo
'

# Make a dir and clean it up afterwards
make_dir() {
	mkdir "$1" &&
	test_when_finished "rm -rf '$1'"
}

# Check that the dir given in "$1" contains exactly the
# set of paths given as arguments.
check_dir() {
	dir=$1; shift
	{
		echo "$dir" &&
		for i in "$@"; do
			echo "$dir/$i"
		done
	} | sort >expect &&
	find "$dir" ! -name pax_global_header -print | sort >actual &&
	test_cmp expect actual
}


# bsdtar/libarchive versions before 3.1.3 consider a tar file with a
# global pax header that is not followed by a file record as corrupt.
if "$TAR" tf "$TEST_DIRECTORY"/t5004/empty-with-pax-header.tar >/dev/null 2>&1
then
	test_set_prereq HEADER_ONLY_TAR_OK
fi

test_expect_success HEADER_ONLY_TAR_OK 'tar archive of commit with empty tree' '
	git archive --format=tar HEAD >empty-with-pax-header.tar &&
	make_dir extract &&
	"$TAR" xf empty-with-pax-header.tar -C extract &&
	check_dir extract
'

test_expect_success 'tar archive of empty tree is empty' '
	git archive --format=tar HEAD: >empty.tar &&
	perl -e "print \"\\0\" x 10240" >10knuls.tar &&
	test_cmp_bin 10knuls.tar empty.tar
'

test_expect_success 'tar archive of empty tree with prefix' '
	git archive --format=tar --prefix=foo/ HEAD >prefix.tar &&
	make_dir extract &&
	"$TAR" xf prefix.tar -C extract &&
	check_dir extract foo
'

test_expect_success UNZIP 'zip archive of empty tree is empty' '
	# Detect the exit code produced when our particular flavor of unzip
	# sees an empty archive. Infozip will generate a warning and exit with
	# code 1. But in the name of sanity, we do not expect other unzip
	# implementations to do the same thing (it would be perfectly
	# reasonable to exit 0, for example).
	#
	# This makes our test less rigorous on some platforms (unzip may not
	# handle the empty repo at all, making our later check of its exit code
	# a no-op). But we cannot do anything reasonable except skip the test
	# on such platforms anyway, and this is the moral equivalent.
	"$GIT_UNZIP" "$TEST_DIRECTORY"/t5004/empty.zip
	expect_code=$?

	git archive --format=zip HEAD >empty.zip &&
	make_dir extract &&
	(
		cd extract &&
		test_expect_code $expect_code "$GIT_UNZIP" ../empty.zip
	) &&
	check_dir extract
'

test_expect_success UNZIP 'zip archive of empty tree with prefix' '
	# We do not have to play exit-code tricks here, because our
	# result should not be empty; it has a directory in it.
	git archive --format=zip --prefix=foo/ HEAD >prefix.zip &&
	make_dir extract &&
	(
		cd extract &&
		"$GIT_UNZIP" ../prefix.zip
	) &&
	check_dir extract foo
'

test_expect_success 'archive complains about pathspec on empty tree' '
	test_must_fail git archive --format=tar HEAD -- foo >/dev/null
'

test_expect_success 'create a commit with an empty subtree' '
	empty_tree=$(git hash-object -t tree /dev/null) &&
	root_tree=$(printf "040000 tree $empty_tree\tsub\n" | git mktree)
'

test_expect_success 'archive empty subtree with no pathspec' '
	git archive --format=tar $root_tree >subtree-all.tar &&
	make_dir extract &&
	"$TAR" xf subtree-all.tar -C extract &&
	check_dir extract sub
'

test_expect_success 'archive empty subtree by direct pathspec' '
	git archive --format=tar $root_tree -- sub >subtree-path.tar &&
	make_dir extract &&
	"$TAR" xf subtree-path.tar -C extract &&
	check_dir extract sub
'

test_done
