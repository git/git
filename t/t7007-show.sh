#!/bin/sh

test_description='git show'

. ./test-lib.sh

test_expect_success setup '
	echo hello world >foo &&
	H=$(git hash-object -w foo) &&
	git tag -a foo-tag -m "Tags $H" $H &&
	HH=$(expr "$H" : "\(..\)") &&
	H38=$(expr "$H" : "..\(.*\)") &&
	rm -f .git/objects/$HH/$H38
'

test_expect_success 'showing a tag that point at a missing object' '
	test_must_fail git --no-pager show foo-tag
'

test_expect_success 'set up a bit of history' '
	test_cummit main1 &&
	test_cummit main2 &&
	test_cummit main3 &&
	git tag -m "annotated tag" annotated &&
	git checkout -b side HEAD^^ &&
	test_cummit side2 &&
	test_cummit side3 &&
	test_merge merge main3
'

test_expect_success 'showing two cummits' '
	cat >expect <<-EOF &&
	cummit $(git rev-parse main2)
	cummit $(git rev-parse main3)
	EOF
	git show main2 main3 >actual &&
	grep ^cummit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing a tree' '
	cat >expected <<-EOF &&
	tree main1:

	main1.t
	EOF
	git show main1: >actual &&
	test_cmp expected actual
'

test_expect_success 'showing two trees' '
	cat >expected <<-EOF &&
	tree main1^{tree}

	main1.t

	tree main2^{tree}

	main1.t
	main2.t
	EOF
	git show main1^{tree} main2^{tree} >actual &&
	test_cmp expected actual
'

test_expect_success 'showing a trees is not recursive' '
	git worktree add not-recursive main1 &&
	mkdir not-recursive/a &&
	test_cummit -C not-recursive a/file &&
	cat >expected <<-EOF &&
	tree HEAD^{tree}

	a/
	main1.t
	EOF
	git -C not-recursive show HEAD^{tree} >actual &&
	test_cmp expected actual
'

test_expect_success 'showing a range walks (linear)' '
	cat >expect <<-EOF &&
	cummit $(git rev-parse main3)
	cummit $(git rev-parse main2)
	EOF
	git show main1..main3 >actual &&
	grep ^cummit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing a range walks (Y shape, ^ first)' '
	cat >expect <<-EOF &&
	cummit $(git rev-parse main3)
	cummit $(git rev-parse main2)
	EOF
	git show ^side3 main3 >actual &&
	grep ^cummit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing a range walks (Y shape, ^ last)' '
	cat >expect <<-EOF &&
	cummit $(git rev-parse main3)
	cummit $(git rev-parse main2)
	EOF
	git show main3 ^side3 >actual &&
	grep ^cummit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing with -N walks' '
	cat >expect <<-EOF &&
	cummit $(git rev-parse main3)
	cummit $(git rev-parse main2)
	EOF
	git show -2 main3 >actual &&
	grep ^cummit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing annotated tag' '
	cat >expect <<-EOF &&
	tag annotated
	cummit $(git rev-parse annotated^{cummit})
	EOF
	git show annotated >actual &&
	grep -E "^(cummit|tag)" actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing annotated tag plus cummit' '
	cat >expect <<-EOF &&
	tag annotated
	cummit $(git rev-parse annotated^{cummit})
	cummit $(git rev-parse side3)
	EOF
	git show annotated side3 >actual &&
	grep -E "^(cummit|tag)" actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing range' '
	cat >expect <<-EOF &&
	cummit $(git rev-parse main3)
	cummit $(git rev-parse main2)
	EOF
	git show ^side3 annotated >actual &&
	grep -E "^(cummit|tag)" actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success '-s suppresses diff' '
	cat >expect <<-\EOF &&
	merge
	main3
	EOF
	git show -s --format=%s merge main3 >actual &&
	test_cmp expect actual
'

test_expect_success '--quiet suppresses diff' '
	echo main3 >expect &&
	git show --quiet --format=%s main3 >actual &&
	test_cmp expect actual
'

test_expect_success 'show --graph is forbidden' '
  test_must_fail git show --graph HEAD
'

test_done
