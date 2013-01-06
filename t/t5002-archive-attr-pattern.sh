#!/bin/sh

test_description='git archive attribute pattern tests'

. ./test-lib.sh

test_expect_exists() {
	test_expect_success " $1 exists" "test -e $1"
}

test_expect_missing() {
	test_expect_success " $1 does not exist" "test ! -e $1"
}

test_expect_success 'setup' '
	echo ignored >ignored &&
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


	mkdir -p one-level-lower/two-levels-lower/ignored-only-if-dir &&
	echo ignored by ignored dir >one-level-lower/two-levels-lower/ignored-only-if-dir/ignored-by-ignored-dir &&
	git add one-level-lower &&

	git commit -m. &&

	git clone --bare . bare &&
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
test_expect_exists	archive/one-level-lower/
test_expect_missing	archive/one-level-lower/two-levels-lower/ignored-only-if-dir/
test_expect_missing	archive/one-level-lower/two-levels-lower/ignored-ony-if-dir/ignored-by-ignored-dir


test_done
