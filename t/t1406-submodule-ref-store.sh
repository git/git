#!/bin/sh

test_description='test submodule ref store api'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

RUN="test-tool ref-store submodule:sub"

test_expect_success 'setup' '
	git init sub &&
	(
		cd sub &&
		test_commit first &&
		git checkout -b new-main &&
		git tag -a -m new-tag new-tag HEAD
	)
'

test_expect_success 'pack_refs() not allowed' '
	test_must_fail $RUN pack-refs 3
'

test_expect_success 'create_symref() not allowed' '
	test_must_fail $RUN create-symref FOO refs/heads/main nothing
'

test_expect_success 'delete_refs() not allowed' '
	test_must_fail $RUN delete-refs 0 nothing FOO refs/tags/new-tag
'

test_expect_success 'rename_refs() not allowed' '
	test_must_fail $RUN rename-ref refs/heads/main refs/heads/new-main
'

test_expect_success 'for_each_ref(refs/heads/)' '
	$RUN for-each-ref refs/heads/ | cut -d" " -f 2- >actual &&
	cat >expected <<-\EOF &&
	main 0x0
	new-main 0x0
	EOF
	test_cmp expected actual
'

test_expect_success 'for_each_ref() is sorted' '
	$RUN for-each-ref refs/heads/ | cut -d" " -f 2- >actual &&
	sort actual > expected &&
	test_cmp expected actual
'

test_expect_success 'resolve_ref(main)' '
	SHA1=`git -C sub rev-parse main` &&
	echo "$SHA1 refs/heads/main 0x0" >expected &&
	$RUN resolve-ref refs/heads/main 0 >actual &&
	test_cmp expected actual
'

test_expect_success 'verify_ref(new-main)' '
	$RUN verify-ref refs/heads/new-main
'

test_expect_success 'for_each_reflog()' '
	$RUN for-each-reflog >actual &&
	cat >expected <<-\EOF &&
	HEAD
	refs/heads/main
	refs/heads/new-main
	EOF
	test_cmp expected actual
'

test_expect_success 'for_each_reflog_ent()' '
	$RUN for-each-reflog-ent HEAD >actual &&
	head -n1 actual | grep first &&
	tail -n1 actual | grep main.to.new
'

test_expect_success 'for_each_reflog_ent_reverse()' '
	$RUN for-each-reflog-ent-reverse HEAD >actual &&
	head -n1 actual | grep main.to.new &&
	tail -n1 actual | grep first
'

test_expect_success 'reflog_exists(HEAD)' '
	$RUN reflog-exists HEAD
'

test_expect_success 'delete_reflog() not allowed' '
	test_must_fail $RUN delete-reflog HEAD
'

test_expect_success 'create-reflog() not allowed' '
	test_must_fail $RUN create-reflog HEAD
'

test_done
