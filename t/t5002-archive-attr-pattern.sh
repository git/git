#!/bin/sh

test_description='git archive attribute pattern tests'

TEST_PASSES_SANITIZE_LEAK=true
TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh

test_expect_exists() {
	test_expect_success " $1 exists" "test -e $1"
}

test_expect_missing() {
	test_expect_success " $1 does not exist" "test ! -e $1"
}

test_expect_success 'setup' '
	echo ignored >ignored &&
	mkdir .git/info &&
	echo ignored export-ignore >>.git/info/attributes &&
	git add ignored &&

	mkdir not-ignored-dir &&
	echo ignored-in-tree >not-ignored-dir/ignored &&
	echo not-ignored-in-tree >not-ignored-dir/ignored-only-if-dir &&
	git add not-ignored-dir &&

	mkdir ignored-only-if-dir &&
	echo ignored by ignored dir >ignored-only-if-dir/ignored-by-ignored-dir &&
	echo ignored-only-if-dir/ export-ignore >>.git/info/attributes &&
	git add ignored-only-if-dir &&

	mkdir -p ignored-without-slash &&
	echo "ignored without slash" >ignored-without-slash/foo &&
	git add ignored-without-slash/foo &&
	echo "ignored-without-slash export-ignore" >>.git/info/attributes &&

	mkdir -p wildcard-without-slash &&
	echo "ignored without slash" >wildcard-without-slash/foo &&
	git add wildcard-without-slash/foo &&
	echo "wild*-without-slash export-ignore" >>.git/info/attributes &&

	mkdir -p deep/and/slashless &&
	echo "ignored without slash" >deep/and/slashless/foo &&
	git add deep/and/slashless/foo &&
	echo "deep/and/slashless export-ignore" >>.git/info/attributes &&

	mkdir -p deep/with/wildcard &&
	echo "ignored without slash" >deep/with/wildcard/foo &&
	git add deep/with/wildcard/foo &&
	echo "deep/*t*/wildcard export-ignore" >>.git/info/attributes &&

	mkdir -p one-level-lower/two-levels-lower/ignored-only-if-dir &&
	echo ignored by ignored dir >one-level-lower/two-levels-lower/ignored-only-if-dir/ignored-by-ignored-dir &&
	git add one-level-lower &&

	git commit -m. &&

	git clone --template= --bare . bare &&
	mkdir bare/info &&
	cp .git/info/attributes bare/info/attributes
'

test_expect_success 'git archive' '
	git archive HEAD >archive.tar &&
	(mkdir archive && cd archive && "$TAR" xf -) <archive.tar
'

test_expect_missing	archive/ignored
test_expect_missing	archive/not-ignored-dir/ignored
test_expect_exists	archive/not-ignored-dir/ignored-only-if-dir
test_expect_exists	archive/not-ignored-dir/
test_expect_missing	archive/ignored-only-if-dir/
test_expect_missing	archive/ignored-ony-if-dir/ignored-by-ignored-dir
test_expect_missing	archive/ignored-without-slash/ &&
test_expect_missing	archive/ignored-without-slash/foo &&
test_expect_missing	archive/wildcard-without-slash/
test_expect_missing	archive/wildcard-without-slash/foo &&
test_expect_missing	archive/deep/and/slashless/ &&
test_expect_missing	archive/deep/and/slashless/foo &&
test_expect_missing	archive/deep/with/wildcard/ &&
test_expect_missing	archive/deep/with/wildcard/foo &&
test_expect_missing	archive/one-level-lower/
test_expect_missing	archive/one-level-lower/two-levels-lower/ignored-only-if-dir/
test_expect_missing	archive/one-level-lower/two-levels-lower/ignored-ony-if-dir/ignored-by-ignored-dir


test_done
