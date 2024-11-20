#!/bin/sh

test_description='handling of deep trees in various commands'

. ./test-lib.sh

# We'll test against two depths here: a small one that will let us check the
# behavior of the config setting easily, and a large one that should be
# forbidden by default. Testing the default depth will let us know whether our
# default is enough to prevent segfaults on systems that run the tests.
small_depth=50
big_depth=4100

small_ok="-c core.maxtreedepth=$small_depth"
small_no="-c core.maxtreedepth=$((small_depth-1))"

# usage: mkdeep <name> <depth>
#   Create a tag <name> containing a file whose path has depth <depth>.
#
# We'll use fast-import here for two reasons:
#
#   1. It's faster than creating $big_depth tree objects.
#
#   2. As we tighten tree limits, it's more likely to allow large sizes
#      than trying to stuff a deep path into the index.
mkdeep () {
	{
		echo "commit refs/tags/$1" &&
		echo "committer foo <foo@example.com> 1234 -0000" &&
		echo "data <<EOF" &&
		echo "the commit message" &&
		echo "EOF" &&

		printf 'M 100644 inline ' &&
		i=0 &&
		while test $i -lt $2
		do
			printf 'a/'
			i=$((i+1))
		done &&
		echo "file" &&

		echo "data <<EOF" &&
		echo "the file contents" &&
		echo "EOF" &&
		echo
	} | git fast-import
}

test_expect_success 'create small tree' '
	mkdeep small $small_depth
'

test_expect_success 'create big tree' '
	mkdeep big $big_depth
'

test_expect_success 'limit recursion of git-archive' '
	git $small_ok archive small >/dev/null &&
	test_must_fail git $small_no archive small >/dev/null
'

test_expect_success 'default limit for git-archive fails gracefully' '
	test_must_fail git archive big >/dev/null
'

test_expect_success 'limit recursion of ls-tree -r' '
	git $small_ok ls-tree -r small &&
	test_must_fail git $small_no ls-tree -r small
'

test_expect_success 'default limit for ls-tree fails gracefully' '
	test_must_fail git ls-tree -r big >/dev/null
'

test_expect_success 'limit recursion of rev-list --objects' '
	git $small_ok rev-list --objects small >/dev/null &&
	test_must_fail git $small_no rev-list --objects small >/dev/null
'

test_expect_success 'default limit for rev-list fails gracefully' '
	test_must_fail git rev-list --objects big >/dev/null
'

test_expect_success 'limit recursion of diff-tree -r' '
	git $small_ok diff-tree -r $EMPTY_TREE small &&
	test_must_fail git $small_no diff-tree -r $EMPTY_TREE small
'

test_expect_success 'default limit for diff-tree fails gracefully' '
	test_must_fail git diff-tree -r $EMPTY_TREE big
'

test_done
