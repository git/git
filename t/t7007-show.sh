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
	test_commit main1 &&
	test_commit main2 &&
	test_commit main3 &&
	git tag -m "annotated tag" annotated &&
	git checkout -b side HEAD^^ &&
	test_commit side2 &&
	test_commit side3
'

test_expect_success 'showing two commits' '
	cat >expect <<-EOF &&
	commit $(git rev-parse main2)
	commit $(git rev-parse main3)
	EOF
	git show main2 main3 >actual &&
	grep ^commit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing a range walks (linear)' '
	cat >expect <<-EOF &&
	commit $(git rev-parse main3)
	commit $(git rev-parse main2)
	EOF
	git show main1..main3 >actual &&
	grep ^commit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing a range walks (Y shape, ^ first)' '
	cat >expect <<-EOF &&
	commit $(git rev-parse main3)
	commit $(git rev-parse main2)
	EOF
	git show ^side3 main3 >actual &&
	grep ^commit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing a range walks (Y shape, ^ last)' '
	cat >expect <<-EOF &&
	commit $(git rev-parse main3)
	commit $(git rev-parse main2)
	EOF
	git show main3 ^side3 >actual &&
	grep ^commit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing with -N walks' '
	cat >expect <<-EOF &&
	commit $(git rev-parse main3)
	commit $(git rev-parse main2)
	EOF
	git show -2 main3 >actual &&
	grep ^commit actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing annotated tag' '
	cat >expect <<-EOF &&
	tag annotated
	commit $(git rev-parse annotated^{commit})
	EOF
	git show annotated >actual &&
	grep -E "^(commit|tag)" actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing annotated tag plus commit' '
	cat >expect <<-EOF &&
	tag annotated
	commit $(git rev-parse annotated^{commit})
	commit $(git rev-parse side3)
	EOF
	git show annotated side3 >actual &&
	grep -E "^(commit|tag)" actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success 'showing range' '
	cat >expect <<-EOF &&
	commit $(git rev-parse main3)
	commit $(git rev-parse main2)
	EOF
	git show ^side3 annotated >actual &&
	grep -E "^(commit|tag)" actual >actual.filtered &&
	test_cmp expect actual.filtered
'

test_expect_success '-s suppresses diff' '
	echo main3 >expect &&
	git show -s --format=%s main3 >actual &&
	test_cmp expect actual
'

test_expect_success '--quiet suppresses diff' '
	echo main3 >expect &&
	git show --quiet --format=%s main3 >actual &&
	test_cmp expect actual
'

test_done
