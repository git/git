#!/bin/sh

test_description='test submodule ref store api'

. ./test-lib.sh

RUN="test-tool ref-store submodule:sub"

test_expect_success 'setup' '
	git init sub &&
	(
		cd sub &&
		test_commit first &&
		git checkout -b new-master
	)
'

test_expect_success 'pack_refs() not allowed' '
	test_must_fail $RUN pack-refs 3
'

test_expect_success 'peel_ref(new-tag)' '
	git -C sub rev-parse HEAD >expected &&
	git -C sub tag -a -m new-tag new-tag HEAD &&
	$RUN peel-ref refs/tags/new-tag >actual &&
	test_cmp expected actual
'

test_expect_success 'create_symref() not allowed' '
	test_must_fail $RUN create-symref FOO refs/heads/master nothing
'

test_expect_success 'delete_refs() not allowed' '
	test_must_fail $RUN delete-refs 0 nothing FOO refs/tags/new-tag
'

test_expect_success 'rename_refs() not allowed' '
	test_must_fail $RUN rename-ref refs/heads/master refs/heads/new-master
'

test_expect_success 'for_each_ref(refs/heads/)' '
	$RUN for-each-ref refs/heads/ | cut -d" " -f 2- >actual &&
	cat >expected <<-\EOF &&
	master 0x0
	new-master 0x0
	EOF
	test_cmp expected actual
'

test_expect_success 'for_each_ref() is sorted' '
	$RUN for-each-ref refs/heads/ | cut -d" " -f 2- >actual &&
	sort actual > expected &&
	test_cmp expected actual
'

test_expect_success 'resolve_ref(master)' '
	SHA1=`git -C sub rev-parse master` &&
	echo "$SHA1 refs/heads/master 0x0" >expected &&
	$RUN resolve-ref refs/heads/master 0 >actual &&
	test_cmp expected actual
'

test_expect_success 'verify_ref(new-master)' '
	$RUN verify-ref refs/heads/new-master
'

test_expect_success 'for_each_reflog()' '
	$RUN for-each-reflog | sort | cut -d" " -f 2- >actual &&
	cat >expected <<-\EOF &&
	HEAD 0x1
	refs/heads/master 0x0
	refs/heads/new-master 0x0
	EOF
	test_cmp expected actual
'

test_expect_success 'for_each_reflog_ent()' '
	$RUN for-each-reflog-ent HEAD >actual && cat actual &&
	head -n1 actual | grep first &&
	tail -n2 actual | head -n1 | grep master.to.new
'

test_expect_success 'for_each_reflog_ent_reverse()' '
	$RUN for-each-reflog-ent-reverse HEAD >actual &&
	head -n1 actual | grep master.to.new &&
	tail -n2 actual | head -n1 | grep first
'

test_expect_success 'reflog_exists(HEAD)' '
	$RUN reflog-exists HEAD
'

test_expect_success 'delete_reflog() not allowed' '
	test_must_fail $RUN delete-reflog HEAD
'

test_expect_success 'create-reflog() not allowed' '
	test_must_fail $RUN create-reflog HEAD 1
'

test_done
