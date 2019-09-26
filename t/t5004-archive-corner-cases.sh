#!/bin/sh

test_description='test corner cases of git-archive'
. ./test-lib.sh

# the 10knuls.tar file is used to test for an empty git generated tar
# without having to invoke tar because an otherwise valid empty GNU tar
# will be considered broken by {Open,Net}BSD tar
test_expect_success 'create commit with empty tree and fake empty tar' '
	git commit --allow-empty -m foo &&
	perl -e "print \"\\0\" x 10240" >10knuls.tar
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

test_lazy_prereq UNZIP_ZIP64_SUPPORT '
	"$GIT_UNZIP" -v | grep ZIP64_SUPPORT
'

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
	{
		"$GIT_UNZIP" "$TEST_DIRECTORY"/t5004/empty.zip
		expect_code=$?
	} &&

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
	test_cmp_bin 10knuls.tar subtree-all.tar
'

test_expect_success 'archive empty subtree by direct pathspec' '
	git archive --format=tar $root_tree -- sub >subtree-path.tar &&
	test_cmp_bin 10knuls.tar subtree-path.tar
'

ZIPINFO=zipinfo

test_lazy_prereq ZIPINFO '
	n=$("$ZIPINFO" "$TEST_DIRECTORY"/t5004/empty.zip | sed -n "2s/.* //p")
	test "x$n" = "x0"
'

test_expect_success ZIPINFO 'zip archive with many entries' '
	# add a directory with 256 files
	mkdir 00 &&
	for a in 0 1 2 3 4 5 6 7 8 9 a b c d e f
	do
		for b in 0 1 2 3 4 5 6 7 8 9 a b c d e f
		do
			: >00/$a$b
		done
	done &&
	git add 00 &&
	git commit -m "256 files in 1 directory" &&

	# duplicate it to get 65536 files in 256 directories
	subtree=$(git write-tree --prefix=00/) &&
	for c in 0 1 2 3 4 5 6 7 8 9 a b c d e f
	do
		for d in 0 1 2 3 4 5 6 7 8 9 a b c d e f
		do
			echo "040000 tree $subtree	$c$d"
		done
	done >tree &&
	tree=$(git mktree <tree) &&

	# zip them
	git archive -o many.zip $tree &&

	# check the number of entries in the ZIP file directory
	expr 65536 + 256 >expect &&
	"$ZIPINFO" many.zip | head -2 | sed -n "2s/.* //p" >actual &&
	test_cmp expect actual
'

test_expect_success EXPENSIVE,UNZIP,UNZIP_ZIP64_SUPPORT \
	'zip archive bigger than 4GB' '
	# build string containing 65536 characters
	s=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef &&
	s=$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s &&
	s=$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s$s &&

	# create blob with a length of 65536 + 1 bytes
	blob=$(echo $s | git hash-object -w --stdin) &&

	# create tree containing 65500 entries of that blob
	for i in $(test_seq 1 65500)
	do
		echo "100644 blob $blob	$i"
	done >tree &&
	tree=$(git mktree <tree) &&

	# zip it, creating an archive a bit bigger than 4GB
	git archive -0 -o many-big.zip $tree &&

	"$GIT_UNZIP" -t many-big.zip 9999 65500 &&
	"$GIT_UNZIP" -t many-big.zip
'

test_expect_success EXPENSIVE,LONG_IS_64BIT,UNZIP,UNZIP_ZIP64_SUPPORT,ZIPINFO \
	'zip archive with files bigger than 4GB' '
	# Pack created with:
	#   dd if=/dev/zero of=file bs=1M count=4100 && git hash-object -w file
	mkdir -p .git/objects/pack &&
	(
		cd .git/objects/pack &&
		"$GIT_UNZIP" "$TEST_DIRECTORY"/t5004/big-pack.zip
	) &&
	blob=754a93d6fada4c6873360e6cb4b209132271ab0e &&
	size=$(expr 4100 "*" 1024 "*" 1024) &&

	# create a tree containing the file
	tree=$(echo "100644 blob $blob	big-file" | git mktree) &&

	# zip it, creating an archive with a file bigger than 4GB
	git archive -o big.zip $tree &&

	"$GIT_UNZIP" -t big.zip &&
	"$ZIPINFO" big.zip >big.lst &&
	grep $size big.lst
'

build_tree() {
	perl -e '
		my $hash = $ARGV[0];
		foreach my $order (2..6) {
			$first = 10 ** $order;
			foreach my $i (-13..-9) {
				my $name = "a" x ($first + $i);
				print "100644 blob $hash\t$name\n"
			}
		}
	' "$1"
}

test_expect_success 'tar archive with long paths' '
	blob=$(echo foo | git hash-object -w --stdin) &&
	tree=$(build_tree $blob | git mktree) &&
	git archive -o long_paths.tar $tree
'

test_done
