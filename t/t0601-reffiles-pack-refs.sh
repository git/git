#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
# Copyright (c) 2006 Christian Couder
#

test_description='git pack-refs should not change the branch semantic

This test runs git pack-refs and git show-ref and checks that the branch
semantic is still the same.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=files
export GIT_TEST_DEFAULT_REF_FORMAT

. ./test-lib.sh

test_expect_success 'enable reflogs' '
	git config core.logallrefupdates true
'

test_expect_success 'prepare a trivial repository' '
	echo Hello > A &&
	git update-index --add A &&
	git commit -m "Initial commit." &&
	HEAD=$(git rev-parse --verify HEAD)
'

test_expect_success 'pack-refs --prune --all' '
	test_path_is_missing .git/packed-refs &&
	git pack-refs --no-prune --all &&
	test_path_is_file .git/packed-refs &&
	N=$(find .git/refs -type f | wc -l) &&
	test "$N" != 0 &&

	git pack-refs --prune --all &&
	test_path_is_file .git/packed-refs &&
	N=$(find .git/refs -type f) &&
	test -z "$N"
'

SHA1=

test_expect_success 'see if git show-ref works as expected' '
	git branch a &&
	SHA1=$(cat .git/refs/heads/a) &&
	echo "$SHA1 refs/heads/a" >expect &&
	git show-ref a >result &&
	test_cmp expect result
'

test_expect_success 'see if a branch still exists when packed' '
	git branch b &&
	git pack-refs --all &&
	rm -f .git/refs/heads/b &&
	echo "$SHA1 refs/heads/b" >expect &&
	git show-ref b >result &&
	test_cmp expect result
'

test_expect_success 'git branch c/d should barf if branch c exists' '
	git branch c &&
	git pack-refs --all &&
	rm -f .git/refs/heads/c &&
	test_must_fail git branch c/d
'

test_expect_success 'see if a branch still exists after git pack-refs --prune' '
	git branch e &&
	git pack-refs --all --prune &&
	echo "$SHA1 refs/heads/e" >expect &&
	git show-ref e >result &&
	test_cmp expect result
'

test_expect_success 'see if git pack-refs --prune remove ref files' '
	git branch f &&
	git pack-refs --all --prune &&
	! test -f .git/refs/heads/f
'

test_expect_success 'see if git pack-refs --prune removes empty dirs' '
	git branch r/s/t &&
	git pack-refs --all --prune &&
	! test -e .git/refs/heads/r
'

test_expect_success 'git branch g should work when git branch g/h has been deleted' '
	git branch g/h &&
	git pack-refs --all --prune &&
	git branch -d g/h &&
	git branch g &&
	git pack-refs --all &&
	git branch -d g
'

test_expect_success 'git branch i/j/k should barf if branch i exists' '
	git branch i &&
	git pack-refs --all --prune &&
	test_must_fail git branch i/j/k
'

test_expect_success 'test git branch k after branch k/l/m and k/lm have been deleted' '
	git branch k/l &&
	git branch k/lm &&
	git branch -d k/l &&
	git branch k/l/m &&
	git branch -d k/l/m &&
	git branch -d k/lm &&
	git branch k
'

test_expect_success 'test git branch n after some branch deletion and pruning' '
	git branch n/o &&
	git branch n/op &&
	git branch -d n/o &&
	git branch n/o/p &&
	git branch -d n/op &&
	git pack-refs --all --prune &&
	git branch -d n/o/p &&
	git branch n
'

test_expect_success 'test excluded refs are not packed' '
	git branch dont_pack1 &&
	git branch dont_pack2 &&
	git branch pack_this &&
	git pack-refs --all --exclude "refs/heads/dont_pack*" &&
	test -f .git/refs/heads/dont_pack1 &&
	test -f .git/refs/heads/dont_pack2 &&
	! test -f .git/refs/heads/pack_this'

test_expect_success 'test --no-exclude refs clears excluded refs' '
	git branch dont_pack3 &&
	git branch dont_pack4 &&
	git pack-refs --all --exclude "refs/heads/dont_pack*" --no-exclude &&
	! test -f .git/refs/heads/dont_pack3 &&
	! test -f .git/refs/heads/dont_pack4'

test_expect_success 'test only included refs are packed' '
	git branch pack_this1 &&
	git branch pack_this2 &&
	git tag dont_pack5 &&
	git pack-refs --include "refs/heads/pack_this*" &&
	test -f .git/refs/tags/dont_pack5 &&
	! test -f .git/refs/heads/pack_this1 &&
	! test -f .git/refs/heads/pack_this2'

test_expect_success 'test --no-include refs clears included refs' '
	git branch pack1 &&
	git branch pack2 &&
	git pack-refs --include "refs/heads/pack*" --no-include &&
	test -f .git/refs/heads/pack1 &&
	test -f .git/refs/heads/pack2'

test_expect_success 'test --exclude takes precedence over --include' '
	git branch dont_pack5 &&
	git pack-refs --include "refs/heads/pack*" --exclude "refs/heads/pack*" &&
	test -f .git/refs/heads/dont_pack5'

test_expect_success 'see if up-to-date packed refs are preserved' '
	git branch q &&
	git pack-refs --all --prune &&
	git update-ref refs/heads/q refs/heads/q &&
	! test -f .git/refs/heads/q
'

test_expect_success 'pack, prune and repack' '
	git tag foo &&
	git pack-refs --all --prune &&
	git show-ref >all-of-them &&
	git pack-refs &&
	git show-ref >again &&
	test_cmp all-of-them again
'

test_expect_success 'explicit pack-refs with dangling packed reference' '
	git commit --allow-empty -m "soon to be garbage-collected" &&
	git pack-refs --all &&
	git reset --hard HEAD^ &&
	git reflog expire --expire=all --all &&
	git prune --expire=all &&
	git pack-refs --all 2>result &&
	test_must_be_empty result
'

test_expect_success 'delete ref with dangling packed version' '
	git checkout -b lamb &&
	git commit --allow-empty -m "future garbage" &&
	git pack-refs --all &&
	git reset --hard HEAD^ &&
	git checkout main &&
	git reflog expire --expire=all --all &&
	git prune --expire=all &&
	git branch -d lamb 2>result &&
	test_must_be_empty result
'

test_expect_success 'delete ref while another dangling packed ref' '
	git branch lamb &&
	git commit --allow-empty -m "future garbage" &&
	git pack-refs --all &&
	git reset --hard HEAD^ &&
	git reflog expire --expire=all --all &&
	git prune --expire=all &&
	git branch -d lamb 2>result &&
	test_must_be_empty result
'

test_expect_success 'pack ref directly below refs/' '
	git update-ref refs/top HEAD &&
	git pack-refs --all --prune &&
	grep refs/top .git/packed-refs &&
	test_path_is_missing .git/refs/top
'

test_expect_success 'do not pack ref in refs/bisect' '
	git update-ref refs/bisect/local HEAD &&
	git pack-refs --all --prune &&
	! grep refs/bisect/local .git/packed-refs >/dev/null &&
	test_path_is_file .git/refs/bisect/local
'

test_expect_success 'disable reflogs' '
	git config core.logallrefupdates false &&
	rm -rf .git/logs
'

test_expect_success 'create packed foo/bar/baz branch' '
	git branch foo/bar/baz &&
	git pack-refs --all --prune &&
	test_path_is_missing .git/refs/heads/foo/bar/baz &&
	test_must_fail git reflog exists refs/heads/foo/bar/baz
'

test_expect_success 'notice d/f conflict with existing directory' '
	test_must_fail git branch foo &&
	test_must_fail git branch foo/bar
'

test_expect_success 'existing directory reports concrete ref' '
	test_must_fail git branch foo 2>stderr &&
	test_grep refs/heads/foo/bar/baz stderr
'

test_expect_success 'notice d/f conflict with existing ref' '
	test_must_fail git branch foo/bar/baz/extra &&
	test_must_fail git branch foo/bar/baz/lots/of/extra/components
'

test_expect_success 'reject packed-refs with unterminated line' '
	cp .git/packed-refs .git/packed-refs.bak &&
	test_when_finished "mv .git/packed-refs.bak .git/packed-refs" &&
	printf "%s" "$HEAD refs/zzzzz" >>.git/packed-refs &&
	echo "fatal: unterminated line in .git/packed-refs: $HEAD refs/zzzzz" >expected_err &&
	test_must_fail git for-each-ref >out 2>err &&
	test_cmp expected_err err
'

test_expect_success 'reject packed-refs containing junk' '
	cp .git/packed-refs .git/packed-refs.bak &&
	test_when_finished "mv .git/packed-refs.bak .git/packed-refs" &&
	printf "%s\n" "bogus content" >>.git/packed-refs &&
	echo "fatal: unexpected line in .git/packed-refs: bogus content" >expected_err &&
	test_must_fail git for-each-ref >out 2>err &&
	test_cmp expected_err err
'

test_expect_success 'reject packed-refs with a short SHA-1' '
	cp .git/packed-refs .git/packed-refs.bak &&
	test_when_finished "mv .git/packed-refs.bak .git/packed-refs" &&
	printf "%.7s %s\n" $HEAD refs/zzzzz >>.git/packed-refs &&
	printf "fatal: unexpected line in .git/packed-refs: %.7s %s\n" $HEAD refs/zzzzz >expected_err &&
	test_must_fail git for-each-ref >out 2>err &&
	test_cmp expected_err err
'

test_expect_success 'timeout if packed-refs.lock exists' '
	LOCK=.git/packed-refs.lock &&
	>"$LOCK" &&
	test_when_finished "rm -f $LOCK" &&
	test_must_fail git pack-refs --all --prune
'

test_expect_success 'retry acquiring packed-refs.lock' '
	LOCK=.git/packed-refs.lock &&
	>"$LOCK" &&
	test_when_finished "wait && rm -f $LOCK" &&
	{
		( sleep 1 && rm -f $LOCK ) &
	} &&
	git -c core.packedrefstimeout=3000 pack-refs --all --prune
'

test_expect_success SYMLINKS 'pack symlinked packed-refs' '
	# First make sure that symlinking works when reading:
	git update-ref refs/heads/lossy refs/heads/main &&
	git for-each-ref >all-refs-before &&
	mv .git/packed-refs .git/my-deviant-packed-refs &&
	ln -s my-deviant-packed-refs .git/packed-refs &&
	git for-each-ref >all-refs-linked &&
	test_cmp all-refs-before all-refs-linked &&
	git pack-refs --all --prune &&
	git for-each-ref >all-refs-packed &&
	test_cmp all-refs-before all-refs-packed &&
	test -h .git/packed-refs &&
	test "$(test_readlink .git/packed-refs)" = "my-deviant-packed-refs"
'

# The 'packed-refs' file is stored directly in .git/. This means it is global
# to the repository, and can only contain refs that are shared across all
# worktrees.
test_expect_success 'refs/worktree must not be packed' '
	test_commit initial &&
	test_commit wt1 &&
	test_commit wt2 &&
	git worktree add wt1 wt1 &&
	git worktree add wt2 wt2 &&
	git checkout initial &&
	git update-ref refs/worktree/foo HEAD &&
	git -C wt1 update-ref refs/worktree/foo HEAD &&
	git -C wt2 update-ref refs/worktree/foo HEAD &&
	git pack-refs --all &&
	test_path_is_missing .git/refs/tags/wt1 &&
	test_path_is_file .git/refs/worktree/foo &&
	test_path_is_file .git/worktrees/wt1/refs/worktree/foo &&
	test_path_is_file .git/worktrees/wt2/refs/worktree/foo
'

# we do not want to count on running pack-refs to
# actually pack it, as it is perfectly reasonable to
# skip processing a broken ref
test_expect_success 'create packed-refs file with broken ref' '
	test_tick && git commit --allow-empty -m one &&
	recoverable=$(git rev-parse HEAD) &&
	test_tick && git commit --allow-empty -m two &&
	missing=$(git rev-parse HEAD) &&
	rm -f .git/refs/heads/main &&
	cat >.git/packed-refs <<-EOF &&
	$missing refs/heads/main
	$recoverable refs/heads/other
	EOF
	echo $missing >expect &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'pack-refs does not silently delete broken packed ref' '
	git pack-refs --all --prune &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'pack-refs does not drop broken refs during deletion' '
	git update-ref -d refs/heads/other &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

for command in "git pack-refs --all --auto" "git maintenance run --task=pack-refs --auto"
do
	test_expect_success "$command does not repack below 16 refs without packed-refs" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			cd repo &&
			git config set maintenance.auto false &&
			git commit --allow-empty --message "initial" &&

			# Create 14 additional references, which brings us to
			# 15 together with the default branch.
			printf "create refs/heads/loose-%d HEAD\n" $(test_seq 14) >stdin &&
			git update-ref --stdin <stdin &&
			test_path_is_missing .git/packed-refs &&
			git pack-refs --auto --all &&
			test_path_is_missing .git/packed-refs &&

			# Create the 16th reference, which should cause us to repack.
			git update-ref refs/heads/loose-15 HEAD &&
			git pack-refs --auto --all &&
			test_path_is_file .git/packed-refs
		)
	'

	test_expect_success "$command does not repack below 16 refs with small packed-refs" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			cd repo &&
			git config set maintenance.auto false &&
			git commit --allow-empty --message "initial" &&

			git pack-refs --all &&
			test_line_count = 2 .git/packed-refs &&

			# Create 15 loose references.
			printf "create refs/heads/loose-%d HEAD\n" $(test_seq 15) >stdin &&
			git update-ref --stdin <stdin &&
			git pack-refs --auto --all &&
			test_line_count = 2 .git/packed-refs &&

			# Create the 16th loose reference, which should cause us to repack.
			git update-ref refs/heads/loose-17 HEAD &&
			git pack-refs --auto --all &&
			test_line_count = 18 .git/packed-refs
		)
	'

	test_expect_success "$command scales with size of packed-refs" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			cd repo &&
			git config set maintenance.auto false &&
			git commit --allow-empty --message "initial" &&

			# Create 99 packed refs. This should cause the heuristic
			# to require more than the minimum amount of loose refs.
			test_seq 99 |
			while read i
			do
				printf "create refs/heads/packed-%d HEAD\n" $i || return 1
			done >stdin &&
			git update-ref --stdin <stdin &&
			git pack-refs --all &&
			test_line_count = 101 .git/packed-refs &&

			# Create 24 loose refs, which should not yet cause us to repack.
			printf "create refs/heads/loose-%d HEAD\n" $(test_seq 24) >stdin &&
			git update-ref --stdin <stdin &&
			git pack-refs --auto --all &&
			test_line_count = 101 .git/packed-refs &&

			# Create another handful of refs to cross the border.
			# Note that we explicitly do not check for strict
			# boundaries here, as this also depends on the size of
			# the object hash.
			printf "create refs/heads/addn-%d HEAD\n" $(test_seq 10) >stdin &&
			git update-ref --stdin <stdin &&
			git pack-refs --auto --all &&
			test_line_count = 135 .git/packed-refs
		)
	'
done

test_done
