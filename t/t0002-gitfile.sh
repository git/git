#!/bin/sh

test_description='.but file

Verify that plumbing commands work when .but is a file
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

objpath() {
	echo "$1" | sed -e 's|\(..\)|\1/|'
}

test_expect_success 'initial setup' '
	REAL="$(pwd)/.real" &&
	mv .but "$REAL"
'

test_expect_success 'bad setup: invalid .but file format' '
	echo "butdir $REAL" >.but &&
	test_must_fail but rev-parse 2>.err &&
	test_i18ngrep "invalid butfile format" .err
'

test_expect_success 'bad setup: invalid .but file path' '
	echo "butdir: $REAL.not" >.but &&
	test_must_fail but rev-parse 2>.err &&
	test_i18ngrep "not a but repository" .err
'

test_expect_success 'final setup + check rev-parse --but-dir' '
	echo "butdir: $REAL" >.but &&
	test "$REAL" = "$(but rev-parse --but-dir)"
'

test_expect_success 'check hash-object' '
	echo "foo" >bar &&
	SHA=$(cat bar | but hash-object -w --stdin) &&
	test_path_is_file "$REAL/objects/$(objpath $SHA)"
'

test_expect_success 'check cat-file' '
	but cat-file blob $SHA >actual &&
	test_cmp bar actual
'

test_expect_success 'check update-index' '
	test_path_is_missing "$REAL/index" &&
	rm -f "$REAL/objects/$(objpath $SHA)" &&
	but update-index --add bar &&
	test_path_is_file "$REAL/index" &&
	test_path_is_file "$REAL/objects/$(objpath $SHA)"
'

test_expect_success 'check write-tree' '
	SHA=$(but write-tree) &&
	test_path_is_file "$REAL/objects/$(objpath $SHA)"
'

test_expect_success 'check cummit-tree' '
	SHA=$(echo "cummit bar" | but cummit-tree $SHA) &&
	test_path_is_file "$REAL/objects/$(objpath $SHA)"
'

test_expect_success !SANITIZE_LEAK 'check rev-list' '
	but update-ref "HEAD" "$SHA" &&
	but rev-list HEAD >actual &&
	echo $SHA >expected &&
	test_cmp expected actual
'

test_expect_success 'setup_but_dir twice in subdir' '
	but init sgd &&
	(
		cd sgd &&
		but config alias.lsfi ls-files &&
		mv .but .realbut &&
		echo "butdir: .realbut" >.but &&
		mkdir subdir &&
		cd subdir &&
		>foo &&
		but add foo &&
		but lsfi >actual &&
		echo foo >expected &&
		test_cmp expected actual
	)
'

test_expect_success 'enter_repo non-strict mode' '
	test_create_repo enter_repo &&
	(
		cd enter_repo &&
		test_tick &&
		test_cummit foo &&
		mv .but .realbut &&
		echo "butdir: .realbut" >.but
	) &&
	head=$(but -C enter_repo rev-parse HEAD) &&
	but ls-remote enter_repo >actual &&
	cat >expected <<-EOF &&
	$head	HEAD
	$head	refs/heads/main
	$head	refs/tags/foo
	EOF
	test_cmp expected actual
'

test_expect_success 'enter_repo linked checkout' '
	(
		cd enter_repo &&
		but worktree add  ../foo refs/tags/foo
	) &&
	head=$(but -C enter_repo rev-parse HEAD) &&
	but ls-remote foo >actual &&
	cat >expected <<-EOF &&
	$head	HEAD
	$head	refs/heads/main
	$head	refs/tags/foo
	EOF
	test_cmp expected actual
'

test_expect_success 'enter_repo strict mode' '
	head=$(but -C enter_repo rev-parse HEAD) &&
	but ls-remote --upload-pack="but upload-pack --strict" foo/.but >actual &&
	cat >expected <<-EOF &&
	$head	HEAD
	$head	refs/heads/main
	$head	refs/tags/foo
	EOF
	test_cmp expected actual
'

test_done
