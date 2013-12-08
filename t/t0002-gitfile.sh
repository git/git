#!/bin/sh

test_description='.git file

Verify that plumbing commands work when .git is a file
'
. ./test-lib.sh

objpath() {
	echo "$1" | sed -e 's|\(..\)|\1/|'
}

objck() {
	p=$(objpath "$1")
	if test ! -f "$REAL/objects/$p"
	then
		echo "Object not found: $REAL/objects/$p"
		false
	fi
}

test_expect_success 'initial setup' '
	REAL="$(pwd)/.real" &&
	mv .git "$REAL"
'

test_expect_success 'bad setup: invalid .git file format' '
	echo "gitdir $REAL" >.git &&
	if git rev-parse 2>.err
	then
		echo "git rev-parse accepted an invalid .git file"
		false
	fi &&
	if ! grep "Invalid gitfile format" .err
	then
		echo "git rev-parse returned wrong error"
		false
	fi
'

test_expect_success 'bad setup: invalid .git file path' '
	echo "gitdir: $REAL.not" >.git &&
	if git rev-parse 2>.err
	then
		echo "git rev-parse accepted an invalid .git file path"
		false
	fi &&
	if ! grep "Not a git repository" .err
	then
		echo "git rev-parse returned wrong error"
		false
	fi
'

test_expect_success 'final setup + check rev-parse --git-dir' '
	echo "gitdir: $REAL" >.git &&
	test "$REAL" = "$(git rev-parse --git-dir)"
'

test_expect_success 'check hash-object' '
	echo "foo" >bar &&
	SHA=$(cat bar | git hash-object -w --stdin) &&
	objck $SHA
'

test_expect_success 'check cat-file' '
	git cat-file blob $SHA >actual &&
	test_cmp bar actual
'

test_expect_success 'check update-index' '
	if test -f "$REAL/index"
	then
		echo "Hmm, $REAL/index exists?"
		false
	fi &&
	rm -f "$REAL/objects/$(objpath $SHA)" &&
	git update-index --add bar &&
	if ! test -f "$REAL/index"
	then
		echo "$REAL/index not found"
		false
	fi &&
	objck $SHA
'

test_expect_success 'check write-tree' '
	SHA=$(git write-tree) &&
	objck $SHA
'

test_expect_success 'check commit-tree' '
	SHA=$(echo "commit bar" | git commit-tree $SHA) &&
	objck $SHA
'

test_expect_success 'check rev-list' '
	echo $SHA >"$REAL/HEAD" &&
	test "$SHA" = "$(git rev-list HEAD)"
'

test_done
