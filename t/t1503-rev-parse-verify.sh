#!/bin/sh
#
# Copyright (c) 2008 Christian Couder
#
test_description='test git rev-parse --verify'

exec </dev/null

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

add_line_into_file()
{
    _line=$1
    _file=$2

    if [ -f "$_file" ]; then
        echo "$_line" >> $_file || return $?
        MSG="Add <$_line> into <$_file>."
    else
        echo "$_line" > $_file || return $?
        git add $_file || return $?
        MSG="Create file <$_file> with <$_line> inside."
    fi

    test_tick
    git commit --quiet -m "$MSG" $_file
}

HASH1=
HASH2=
HASH3=
HASH4=

test_expect_success 'set up basic repo with 1 file (hello) and 4 commits' '
	add_line_into_file "1: Hello World" hello &&
	HASH1=$(git rev-parse --verify HEAD) &&
	add_line_into_file "2: A new day for git" hello &&
	HASH2=$(git rev-parse --verify HEAD) &&
	add_line_into_file "3: Another new day for git" hello &&
	HASH3=$(git rev-parse --verify HEAD) &&
	add_line_into_file "4: Ciao for now" hello &&
	HASH4=$(git rev-parse --verify HEAD)
'

test_expect_success 'works with one good rev' '
	rev_hash1=$(git rev-parse --verify $HASH1) &&
	test "$rev_hash1" = "$HASH1" &&
	rev_hash2=$(git rev-parse --verify $HASH2) &&
	test "$rev_hash2" = "$HASH2" &&
	rev_hash3=$(git rev-parse --verify $HASH3) &&
	test "$rev_hash3" = "$HASH3" &&
	rev_hash4=$(git rev-parse --verify $HASH4) &&
	test "$rev_hash4" = "$HASH4" &&
	rev_main=$(git rev-parse --verify main) &&
	test "$rev_main" = "$HASH4" &&
	rev_head=$(git rev-parse --verify HEAD) &&
	test "$rev_head" = "$HASH4"
'

test_expect_success 'fails with any bad rev or many good revs' '
	test_must_fail git rev-parse --verify 2>error &&
	grep "single revision" error &&
	test_must_fail git rev-parse --verify foo 2>error &&
	grep "single revision" error &&
	test_must_fail git rev-parse --verify HEAD bar 2>error &&
	grep "single revision" error &&
	test_must_fail git rev-parse --verify baz HEAD 2>error &&
	grep "single revision" error &&
	test_must_fail git rev-parse --verify $HASH2 HEAD 2>error &&
	grep "single revision" error
'

test_expect_success 'fails silently when using -q' '
	test_must_fail git rev-parse --verify --quiet 2>error &&
	test_must_be_empty error &&
	test_must_fail git rev-parse -q --verify foo 2>error &&
	test_must_be_empty error &&
	test_must_fail git rev-parse --verify -q HEAD bar 2>error &&
	test_must_be_empty error &&
	test_must_fail git rev-parse --quiet --verify baz HEAD 2>error &&
	test_must_be_empty error &&
	test_must_fail git rev-parse -q --verify $HASH2 HEAD 2>error &&
	test_must_be_empty error
'

test_expect_success 'fails silently when using -q with deleted reflogs' '
	ref=$(git rev-parse HEAD) &&
	git update-ref --create-reflog -m "message for refs/test" refs/test "$ref" &&
	git reflog delete --updateref --rewrite refs/test@{1} &&
	test_must_fail git rev-parse -q --verify refs/test@{1} >error 2>&1 &&
	test_must_be_empty error
'

test_expect_success 'fails silently when using -q with not enough reflogs' '
	ref=$(git rev-parse HEAD) &&
	git update-ref --create-reflog -m "message for refs/test2" refs/test2 "$ref" &&
	test_must_fail git rev-parse -q --verify refs/test2@{999} >error 2>&1 &&
	test_must_be_empty error
'

test_expect_success 'succeeds silently with -q and reflogs that do not go far back enough in time' '
	ref=$(git rev-parse HEAD) &&
	git update-ref --create-reflog -m "message for refs/test3" refs/test3 "$ref" &&
	git rev-parse -q --verify refs/test3@{1.year.ago} >actual 2>error &&
	test_must_be_empty error &&
	echo "$ref" >expect &&
	test_cmp expect actual
'

test_expect_success 'no stdout output on error' '
	test -z "$(git rev-parse --verify)" &&
	test -z "$(git rev-parse --verify foo)" &&
	test -z "$(git rev-parse --verify baz HEAD)" &&
	test -z "$(git rev-parse --verify HEAD bar)" &&
	test -z "$(git rev-parse --verify $HASH2 HEAD)"
'

test_expect_success 'use --default' '
	git rev-parse --verify --default main &&
	git rev-parse --verify --default main HEAD &&
	git rev-parse --default main --verify &&
	git rev-parse --default main --verify HEAD &&
	git rev-parse --verify HEAD --default main &&
	test_must_fail git rev-parse --verify foo --default main &&
	test_must_fail git rev-parse --default HEAD --verify bar &&
	test_must_fail git rev-parse --verify --default HEAD baz &&
	test_must_fail git rev-parse --default foo --verify &&
	test_must_fail git rev-parse --verify --default bar
'

test_expect_success 'main@{n} for various n' '
	git reflog >out &&
	N=$(wc -l <out) &&
	Nm1=$(($N-1)) &&
	Np1=$(($N+1)) &&
	git rev-parse --verify main@{0} &&
	git rev-parse --verify main@{1} &&
	git rev-parse --verify main@{$Nm1} &&
	test_must_fail git rev-parse --verify main@{$N} &&
	test_must_fail git rev-parse --verify main@{$Np1}
'

test_expect_success 'options can appear after --verify' '
	git rev-parse --verify HEAD >expect &&
	git rev-parse --verify -q HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'verify respects --end-of-options' '
	git update-ref refs/heads/-tricky HEAD &&
	git rev-parse --verify HEAD >expect &&
	git rev-parse --verify --end-of-options -tricky >actual &&
	test_cmp expect actual
'

test_done
