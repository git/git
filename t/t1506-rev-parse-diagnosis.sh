#!/bin/sh

test_description='test git rev-parse diagnosis for invalid argument'

exec </dev/null

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_did_you_mean ()
{
	cat >expected <<-EOF &&
	fatal: path '$2$3' $4, but not ${5:-$SQ$3$SQ}
	hint: Did you mean '$1:$2$3'${2:+ aka $SQ$1:./$3$SQ}?
	EOF
	test_cmp expected error
}

HASH_file=

test_expect_success 'set up basic repo' '
	echo one > file.txt &&
	mkdir subdir &&
	echo two > subdir/file.txt &&
	echo three > subdir/file2.txt &&
	git add . &&
	git commit -m init &&
	echo four > index-only.txt &&
	git add index-only.txt &&
	echo five > disk-only.txt
'

test_expect_success 'correct file objects' '
	HASH_file=$(git rev-parse HEAD:file.txt) &&
	git rev-parse HEAD:subdir/file.txt &&
	git rev-parse :index-only.txt &&
	(cd subdir &&
	 git rev-parse HEAD:subdir/file2.txt &&
	 test $HASH_file = $(git rev-parse HEAD:file.txt) &&
	 test $HASH_file = $(git rev-parse :file.txt) &&
	 test $HASH_file = $(git rev-parse :0:file.txt) )
'

test_expect_success 'correct relative file objects (0)' '
	git rev-parse :file.txt >expected &&
	git rev-parse :./file.txt >result &&
	test_cmp expected result &&
	git rev-parse :0:./file.txt >result &&
	test_cmp expected result
'

test_expect_success 'correct relative file objects (1)' '
	git rev-parse HEAD:file.txt >expected &&
	git rev-parse HEAD:./file.txt >result &&
	test_cmp expected result
'

test_expect_success 'correct relative file objects (2)' '
	(
		cd subdir &&
		git rev-parse HEAD:../file.txt >result &&
		test_cmp ../expected result
	)
'

test_expect_success 'correct relative file objects (3)' '
	(
		cd subdir &&
		git rev-parse HEAD:../subdir/../file.txt >result &&
		test_cmp ../expected result
	)
'

test_expect_success 'correct relative file objects (4)' '
	git rev-parse HEAD:subdir/file.txt >expected &&
	(
		cd subdir &&
		git rev-parse HEAD:./file.txt >result &&
		test_cmp ../expected result
	)
'

test_expect_success 'correct relative file objects (5)' '
	git rev-parse :subdir/file.txt >expected &&
	(
		cd subdir &&
		git rev-parse :./file.txt >result &&
		test_cmp ../expected result &&
		git rev-parse :0:./file.txt >result &&
		test_cmp ../expected result
	)
'

test_expect_success 'correct relative file objects (6)' '
	git rev-parse :file.txt >expected &&
	(
		cd subdir &&
		git rev-parse :../file.txt >result &&
		test_cmp ../expected result &&
		git rev-parse :0:../file.txt >result &&
		test_cmp ../expected result
	)
'

test_expect_success 'incorrect revision id' '
	test_must_fail git rev-parse foobar:file.txt 2>error &&
	test_grep "invalid object name .foobar." error &&
	test_must_fail git rev-parse foobar 2>error &&
	test_grep "unknown revision or path not in the working tree." error
'

test_expect_success 'incorrect file in sha1:path' '
	test_must_fail git rev-parse HEAD:nothing.txt 2>error &&
	test_grep "path .nothing.txt. does not exist in .HEAD." error &&
	test_must_fail git rev-parse HEAD:index-only.txt 2>error &&
	test_grep "path .index-only.txt. exists on disk, but not in .HEAD." error &&
	(cd subdir &&
	 test_must_fail git rev-parse HEAD:file2.txt 2>error &&
	 test_did_you_mean HEAD subdir/ file2.txt exists )
'

test_expect_success 'incorrect file in :path and :N:path' '
	test_must_fail git rev-parse :nothing.txt 2>error &&
	test_grep "path .nothing.txt. does not exist (neither on disk nor in the index)" error &&
	test_must_fail git rev-parse :1:nothing.txt 2>error &&
	test_grep "path .nothing.txt. does not exist (neither on disk nor in the index)" error &&
	test_must_fail git rev-parse :1:file.txt 2>error &&
	test_did_you_mean ":0" "" file.txt "is in the index" "at stage 1" &&
	(cd subdir &&
	 test_must_fail git rev-parse :1:file.txt 2>error &&
	 test_did_you_mean ":0" "" file.txt "is in the index" "at stage 1" &&
	 test_must_fail git rev-parse :file2.txt 2>error &&
	 test_did_you_mean ":0" subdir/ file2.txt "is in the index" &&
	 test_must_fail git rev-parse :2:file2.txt 2>error &&
	 test_did_you_mean :0 subdir/ file2.txt "is in the index") &&
	test_must_fail git rev-parse :disk-only.txt 2>error &&
	test_grep "path .disk-only.txt. exists on disk, but not in the index" error
'

test_expect_success 'invalid @{n} reference' '
	test_must_fail git rev-parse main@{99999} >output 2>error &&
	test_must_be_empty output &&
	test_grep "log for [^ ]* only has [0-9][0-9]* entries" error  &&
	test_must_fail git rev-parse --verify main@{99999} >output 2>error &&
	test_must_be_empty output &&
	test_grep "log for [^ ]* only has [0-9][0-9]* entries" error
'

test_expect_success 'relative path not found' '
	(
		cd subdir &&
		test_must_fail git rev-parse HEAD:./nonexistent.txt 2>error &&
		test_grep subdir/nonexistent.txt error
	)
'

test_expect_success 'relative path outside worktree' '
	test_must_fail git rev-parse HEAD:../file.txt >output 2>error &&
	test_must_be_empty output &&
	test_grep "outside repository" error
'

test_expect_success 'relative path when cwd is outside worktree' '
	test_must_fail git --git-dir=.git --work-tree=subdir rev-parse HEAD:./file.txt >output 2>error &&
	test_must_be_empty output &&
	test_grep "relative path syntax can.t be used outside working tree" error
'

test_expect_success '<commit>:file correctly diagnosed after a pathname' '
	test_must_fail git rev-parse file.txt HEAD:file.txt 1>actual 2>error &&
	test_grep ! "exists on disk" error &&
	test_grep "no such path in the working tree" error &&
	cat >expect <<-\EOF &&
	file.txt
	HEAD:file.txt
	EOF
	test_cmp expect actual
'

test_expect_success 'dotdot is not an empty set' '
	( H=$(git rev-parse HEAD) && echo $H && echo ^$H ) >expect &&

	git rev-parse HEAD.. >actual &&
	test_cmp expect actual &&

	git rev-parse ..HEAD >actual &&
	test_cmp expect actual &&

	echo .. >expect &&
	git rev-parse .. >actual &&
	test_cmp expect actual
'

test_expect_success 'dotdot does not peel endpoints' '
	git tag -a -m "annote" annotated HEAD &&
	A=$(git rev-parse annotated) &&
	H=$(git rev-parse annotated^0) &&
	{
		echo $A && echo ^$A
	} >expect-with-two-dots &&
	{
		echo $A && echo $A && echo ^$H
	} >expect-with-merge-base &&

	git rev-parse annotated..annotated >actual-with-two-dots &&
	test_cmp expect-with-two-dots actual-with-two-dots &&

	git rev-parse annotated...annotated >actual-with-merge-base &&
	test_cmp expect-with-merge-base actual-with-merge-base
'

test_expect_success 'arg before dashdash must be a revision (missing)' '
	test_must_fail git rev-parse foobar -- 2>stderr &&
	test_grep "bad revision" stderr
'

test_expect_success 'arg before dashdash must be a revision (file)' '
	>foobar &&
	test_must_fail git rev-parse foobar -- 2>stderr &&
	test_grep "bad revision" stderr
'

test_expect_success 'arg before dashdash must be a revision (ambiguous)' '
	>foobar &&
	git update-ref refs/heads/foobar HEAD &&
	{
		# we do not want to use rev-parse here, because
		# we are testing it
		git show-ref -s refs/heads/foobar &&
		printf "%s\n" --
	} >expect &&
	git rev-parse foobar -- >actual &&
	test_cmp expect actual
'

test_expect_success 'reject Nth parent if N is too high' '
	test_must_fail git rev-parse HEAD^100000000000000000000000000000000
'

test_expect_success 'reject Nth ancestor if N is too high' '
	test_must_fail git rev-parse HEAD~100000000000000000000000000000000
'

test_expect_success 'pathspecs with wildcards are not ambiguous' '
	echo "*.c" >expect &&
	git rev-parse "*.c" >actual &&
	test_cmp expect actual
'

test_expect_success 'backslash does not trigger wildcard rule' '
	test_must_fail git rev-parse "foo\\bar"
'

test_expect_success 'escaped char does not trigger wildcard rule' '
	test_must_fail git rev-parse "foo\\*bar"
'

test_expect_success 'arg after dashdash not interpreted as option' '
	cat >expect <<-\EOF &&
	--
	--local-env-vars
	EOF
	git rev-parse -- --local-env-vars >actual &&
	test_cmp expect actual
'

test_expect_success 'arg after end-of-options not interpreted as option' '
	test_must_fail git rev-parse --end-of-options --not-real -- 2>err &&
	test_grep bad.revision.*--not-real err
'

test_expect_success 'end-of-options still allows --' '
	cat >expect <<-EOF &&
	--end-of-options
	$(git rev-parse --verify HEAD)
	--
	path
	EOF
	git rev-parse --end-of-options HEAD -- path >actual &&
	test_cmp expect actual
'

test_done
