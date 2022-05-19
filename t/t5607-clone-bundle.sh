#!/bin/sh

test_description='some bundle related tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_oid_cache <<-EOF &&
	version sha1:2
	version sha256:3
	EOF
	test_cummit initial &&
	test_tick &&
	but tag -m tag tag &&
	test_cummit second &&
	test_cummit third &&
	but tag -d initial &&
	but tag -d second &&
	but tag -d third
'

test_expect_success '"verify" needs a worktree' '
	but bundle create tip.bundle -1 main &&
	nonbut test_must_fail but bundle verify ../tip.bundle 2>err &&
	test_i18ngrep "need a repository" err
'

test_expect_success 'annotated tags can be excluded by rev-list options' '
	but bundle create bundle --all --since=7.Apr.2005.15:14:00.-0700 &&
	cat >expect <<-EOF &&
	$(but rev-parse HEAD)	HEAD
	$(but rev-parse tag)	refs/tags/tag
	$(but rev-parse main)	refs/heads/main
	EOF
	but ls-remote bundle >actual &&
	test_cmp expect actual &&

	but bundle create bundle --all --since=7.Apr.2005.15:16:00.-0700 &&
	cat >expect <<-EOF &&
	$(but rev-parse HEAD)	HEAD
	$(but rev-parse main)	refs/heads/main
	EOF
	but ls-remote bundle >actual &&
	test_cmp expect actual
'

test_expect_success 'die if bundle file cannot be created' '
	mkdir adir &&
	test_must_fail but bundle create adir --all
'

test_expect_success 'bundle --stdin' '
	echo main | but bundle create stdin-bundle.bdl --stdin &&
	cat >expect <<-EOF &&
	$(but rev-parse main)	refs/heads/main
	EOF
	but ls-remote stdin-bundle.bdl >actual &&
	test_cmp expect actual
'

test_expect_success 'bundle --stdin <rev-list options>' '
	echo main | but bundle create hybrid-bundle.bdl --stdin tag &&
	cat >expect <<-EOF &&
	$(but rev-parse main)	refs/heads/main
	EOF
	but ls-remote stdin-bundle.bdl >actual &&
	test_cmp expect actual
'

test_expect_success 'empty bundle file is rejected' '
	>empty-bundle &&
	test_must_fail but fetch empty-bundle
'

# This triggers a bug in older versions where the resulting line (with
# --pretty=oneline) was longer than a 1024-char buffer.
test_expect_success 'ridiculously long subject in boundary' '
	>file4 &&
	test_tick &&
	but add file4 &&
	printf "%01200d\n" 0 | but cummit -F - &&
	test_cummit fifth &&
	but bundle create long-subject-bundle.bdl HEAD^..HEAD &&
	cat >expect <<-EOF &&
	$(but rev-parse main) HEAD
	EOF
	but bundle list-heads long-subject-bundle.bdl >actual &&
	test_cmp expect actual &&

	but fetch long-subject-bundle.bdl &&

	algo=$(test_oid algo) &&
	if test "$algo" != sha1
	then
		echo "@object-format=sha256"
	fi >expect &&
	cat >>expect <<-EOF &&
	-$(but log --pretty=format:"%H %s" -1 HEAD^)
	$(but rev-parse HEAD) HEAD
	EOF

	if test "$algo" = sha1
	then
		head -n 3 long-subject-bundle.bdl
	else
		head -n 4 long-subject-bundle.bdl
	fi | grep -v "^#" >actual &&

	test_cmp expect actual
'

test_expect_success 'prerequisites with an empty cummit message' '
	>file1 &&
	but add file1 &&
	test_tick &&
	but cummit --allow-empty-message -m "" &&
	test_cummit file2 &&
	but bundle create bundle HEAD^.. &&
	but bundle verify bundle
'

test_expect_success 'failed bundle creation does not leave cruft' '
	# This fails because the bundle would be empty.
	test_must_fail but bundle create fail.bundle main..main &&
	test_path_is_missing fail.bundle.lock
'

test_expect_success 'fetch SHA-1 from bundle' '
	test_create_repo foo &&
	test_cummit -C foo x &&
	but -C foo bundle create tip.bundle -1 main &&
	but -C foo rev-parse HEAD >hash &&

	# Exercise to ensure that fetching a SHA-1 from a bundle works with no
	# errors
	but fetch --no-tags foo/tip.bundle "$(cat hash)"
'

test_expect_success 'but bundle uses expected default format' '
	but bundle create bundle HEAD^.. &&
	cat >expect <<-EOF &&
	# v$(test_oid version) but bundle
	EOF
	head -n1 bundle >actual &&
	test_cmp expect actual
'

test_expect_success 'but bundle v3 has expected contents' '
	but branch side HEAD &&
	but bundle create --version=3 bundle HEAD^..side &&
	head -n2 bundle >actual &&
	cat >expect <<-EOF &&
	# v3 but bundle
	@object-format=$(test_oid algo)
	EOF
	test_cmp expect actual &&
	but bundle verify bundle
'

test_expect_success 'but bundle v3 rejects unknown capabilities' '
	cat >new <<-EOF &&
	# v3 but bundle
	@object-format=$(test_oid algo)
	@unknown=silly
	EOF
	test_must_fail but bundle verify new 2>output &&
	test_i18ngrep "unknown capability .unknown=silly." output
'

test_done
