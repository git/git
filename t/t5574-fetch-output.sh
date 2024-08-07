#!/bin/sh

test_description='git fetch output format'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'fetch with invalid output format configuration' '
	test_when_finished "rm -rf clone" &&
	git clone . clone &&

	test_must_fail git -C clone -c fetch.output fetch origin 2>actual.err &&
	cat >expect <<-EOF &&
	error: missing value for ${SQ}fetch.output${SQ}
	fatal: unable to parse ${SQ}fetch.output${SQ} from command-line config
	EOF
	test_cmp expect actual.err &&

	test_must_fail git -C clone -c fetch.output= fetch origin 2>actual.err &&
	cat >expect <<-EOF &&
	fatal: invalid value for ${SQ}fetch.output${SQ}: ${SQ}${SQ}
	EOF
	test_cmp expect actual.err &&

	test_must_fail git -C clone -c fetch.output=garbage fetch origin 2>actual.err &&
	cat >expect <<-EOF &&
	fatal: invalid value for ${SQ}fetch.output${SQ}: ${SQ}garbage${SQ}
	EOF
	test_cmp expect actual.err
'

test_expect_success 'fetch aligned output' '
	git clone . full-output &&
	test_commit looooooooooooong-tag &&
	(
		cd full-output &&
		git -c fetch.output=full fetch origin >actual 2>&1 &&
		grep -e "->" actual | cut -c 22- >../actual
	) &&
	cat >expect <<-\EOF &&
	main                 -> origin/main
	looooooooooooong-tag -> looooooooooooong-tag
	EOF
	test_cmp expect actual
'

test_expect_success 'fetch compact output' '
	git clone . compact &&
	test_commit extraaa &&
	(
		cd compact &&
		git -c fetch.output=compact fetch origin >actual 2>&1 &&
		grep -e "->" actual | cut -c 22- >../actual
	) &&
	cat >expect <<-\EOF &&
	main       -> origin/*
	extraaa    -> *
	EOF
	test_cmp expect actual
'

test_expect_success 'setup for fetch porcelain output' '
	# Set up a bunch of references that we can use to demonstrate different
	# kinds of flag symbols in the output format.
	test_commit commit-for-porcelain-output &&
	MAIN_OLD=$(git rev-parse HEAD) &&
	git branch "fast-forward" &&
	git branch "deleted-branch" &&
	git checkout -b force-updated &&
	test_commit --no-tag force-update-old &&
	FORCE_UPDATED_OLD=$(git rev-parse HEAD) &&
	git checkout main &&

	# Backup to preseed.git
	git clone --mirror . preseed.git &&

	# Continue changing our local references.
	git branch new-branch &&
	git branch -d deleted-branch &&
	git checkout fast-forward &&
	test_commit --no-tag fast-forward-new &&
	FAST_FORWARD_NEW=$(git rev-parse HEAD) &&
	git checkout force-updated &&
	git reset --hard HEAD~ &&
	test_commit --no-tag force-update-new &&
	FORCE_UPDATED_NEW=$(git rev-parse HEAD)
'

for opt in "" "--atomic"
do
	test_expect_success "fetch porcelain output ${opt:+(atomic)}" '
		test_when_finished "rm -rf porcelain" &&

		# Clone and pre-seed the repositories. We fetch references into two
		# namespaces so that we can test that rejected and force-updated
		# references are reported properly.
		refspecs="refs/heads/*:refs/unforced/* +refs/heads/*:refs/forced/*" &&
		git clone preseed.git porcelain &&
		git -C porcelain fetch origin $opt $refspecs &&

		cat >expect <<-EOF &&
		- $MAIN_OLD $ZERO_OID refs/forced/deleted-branch
		- $MAIN_OLD $ZERO_OID refs/unforced/deleted-branch
		  $MAIN_OLD $FAST_FORWARD_NEW refs/unforced/fast-forward
		! $FORCE_UPDATED_OLD $FORCE_UPDATED_NEW refs/unforced/force-updated
		* $ZERO_OID $MAIN_OLD refs/unforced/new-branch
		  $MAIN_OLD $FAST_FORWARD_NEW refs/forced/fast-forward
		+ $FORCE_UPDATED_OLD $FORCE_UPDATED_NEW refs/forced/force-updated
		* $ZERO_OID $MAIN_OLD refs/forced/new-branch
		  $MAIN_OLD $FAST_FORWARD_NEW refs/remotes/origin/fast-forward
		+ $FORCE_UPDATED_OLD $FORCE_UPDATED_NEW refs/remotes/origin/force-updated
		* $ZERO_OID $MAIN_OLD refs/remotes/origin/new-branch
		EOF

		# Change the URL of the repository to fetch different references.
		git -C porcelain remote set-url origin .. &&

		# Execute a dry-run fetch first. We do this to assert that the dry-run
		# and non-dry-run fetches produces the same output. Execution of the
		# fetch is expected to fail as we have a rejected reference update.
		test_must_fail git -C porcelain fetch $opt \
			--porcelain --dry-run --prune origin $refspecs >actual &&
		test_cmp expect actual &&

		# And now we perform a non-dry-run fetch.
		test_must_fail git -C porcelain fetch $opt \
			--porcelain --prune origin $refspecs >actual 2>stderr &&
		test_cmp expect actual &&
		test_must_be_empty stderr
	'
done

test_expect_success 'fetch porcelain with multiple remotes' '
	test_when_finished "rm -rf porcelain" &&

	git switch --create multiple-remotes &&
	git clone . porcelain &&
	git -C porcelain remote add second-remote "$PWD" &&
	git -C porcelain fetch second-remote &&

	test_commit --no-tag multi-commit &&
	old_commit=$(git rev-parse HEAD~) &&
	new_commit=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	  $old_commit $new_commit refs/remotes/origin/multiple-remotes
	  $old_commit $new_commit refs/remotes/second-remote/multiple-remotes
	EOF

	git -C porcelain fetch --porcelain --all >actual 2>stderr &&
	test_cmp expect actual &&
	test_must_be_empty stderr
'

test_expect_success 'fetch porcelain refuses to work with submodules' '
	test_when_finished "rm -rf porcelain" &&

	cat >expect <<-EOF &&
	fatal: options ${SQ}--porcelain${SQ} and ${SQ}--recurse-submodules${SQ} cannot be used together
	EOF

	git init porcelain &&
	test_must_fail git -C porcelain fetch --porcelain --recurse-submodules=yes 2>stderr &&
	test_cmp expect stderr &&

	test_must_fail git -C porcelain fetch --porcelain --recurse-submodules=on-demand 2>stderr &&
	test_cmp expect stderr
'

test_expect_success 'fetch porcelain overrides fetch.output config' '
	test_when_finished "rm -rf porcelain" &&

	git switch --create config-override &&
	git clone . porcelain &&
	test_commit new-commit &&
	old_commit=$(git rev-parse HEAD~) &&
	new_commit=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	  $old_commit $new_commit refs/remotes/origin/config-override
	* $ZERO_OID $new_commit refs/tags/new-commit
	EOF

	git -C porcelain -c fetch.output=compact fetch --porcelain >stdout 2>stderr &&
	test_must_be_empty stderr &&
	test_cmp expect stdout
'

test_expect_success 'fetch --no-porcelain overrides previous --porcelain' '
	test_when_finished "rm -rf no-porcelain" &&

	git switch --create no-porcelain &&
	git clone . no-porcelain &&
	test_commit --no-tag no-porcelain &&
	old_commit=$(git rev-parse --short HEAD~) &&
	new_commit=$(git rev-parse --short HEAD) &&

	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	   $old_commit..$new_commit  no-porcelain -> origin/no-porcelain
	EOF

	git -C no-porcelain fetch --porcelain --no-porcelain >stdout 2>stderr &&
	test_cmp expect stderr &&
	test_must_be_empty stdout
'

test_expect_success 'fetch output with HEAD' '
	test_when_finished "rm -rf head" &&
	git clone . head &&

	git -C head fetch --dry-run origin HEAD >actual.out 2>actual.err &&
	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	 * branch            HEAD       -> FETCH_HEAD
	EOF
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C head fetch origin HEAD >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C head fetch --dry-run origin HEAD:foo >actual.out 2>actual.err &&
	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	 * [new ref]         HEAD       -> foo
	EOF
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C head fetch origin HEAD:foo >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_cmp expect actual.err
'

test_expect_success 'fetch porcelain output with HEAD' '
	test_when_finished "rm -rf head" &&
	git clone . head &&
	COMMIT_ID=$(git rev-parse HEAD) &&

	git -C head fetch --porcelain --dry-run origin HEAD >actual &&
	cat >expect <<-EOF &&
	* $ZERO_OID $COMMIT_ID FETCH_HEAD
	EOF
	test_cmp expect actual &&

	git -C head fetch --porcelain origin HEAD >actual &&
	test_cmp expect actual &&

	git -C head fetch --porcelain --dry-run origin HEAD:foo >actual &&
	cat >expect <<-EOF &&
	* $ZERO_OID $COMMIT_ID refs/heads/foo
	EOF
	test_cmp expect actual &&

	git -C head fetch --porcelain origin HEAD:foo >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch output with object ID' '
	test_when_finished "rm -rf object-id" &&
	git clone . object-id &&
	commit=$(git rev-parse HEAD) &&

	git -C object-id fetch --dry-run origin $commit:object-id >actual.out 2>actual.err &&
	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	 * [new ref]         $commit -> object-id
	EOF
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C object-id fetch origin $commit:object-id >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_cmp expect actual.err
'

test_expect_success '--no-show-forced-updates' '
	mkdir forced-updates &&
	(
		cd forced-updates &&
		git init &&
		test_commit 1 &&
		test_commit 2
	) &&
	git clone forced-updates forced-update-clone &&
	git clone forced-updates no-forced-update-clone &&
	git -C forced-updates reset --hard HEAD~1 &&
	(
		cd forced-update-clone &&
		git fetch --show-forced-updates origin 2>output &&
		test_grep "(forced update)" output
	) &&
	(
		cd no-forced-update-clone &&
		git fetch --no-show-forced-updates origin 2>output &&
		test_grep ! "(forced update)" output
	)
'

test_done
