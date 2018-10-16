#!/bin/sh

test_description='basic git gc tests
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'setup' '
	# do not let the amount of physical memory affects gc
	# behavior, make sure we always pack everything to one pack by
	# default
	git config gc.bigPackThreshold 2g
'

test_expect_success 'gc empty repository' '
	git gc
'

test_expect_success 'gc does not leave behind pid file' '
	git gc &&
	test_path_is_missing .git/gc.pid
'

test_expect_success 'gc --gobbledegook' '
	test_expect_code 129 git gc --nonsense 2>err &&
	test_i18ngrep "[Uu]sage: git gc" err
'

test_expect_success 'gc -h with invalid configuration' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		echo "[gc] pruneexpire = CORRUPT" >>.git/config &&
		test_expect_code 129 git gc -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'gc is not aborted due to a stale symref' '
	git init remote &&
	(
		cd remote &&
		test_commit initial &&
		git clone . ../client &&
		git branch -m develop &&
		cd ../client &&
		git fetch --prune &&
		git gc
	)
'

test_expect_success 'gc --keep-largest-pack' '
	test_create_repo keep-pack &&
	(
		cd keep-pack &&
		test_commit one &&
		test_commit two &&
		test_commit three &&
		git gc &&
		( cd .git/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 1 pack-list &&
		BASE_PACK=.git/objects/pack/pack-*.pack &&
		test_commit four &&
		git repack -d &&
		test_commit five &&
		git repack -d &&
		( cd .git/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 3 pack-list &&
		git gc --keep-largest-pack &&
		( cd .git/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 2 pack-list &&
		test_path_is_file $BASE_PACK &&
		git fsck
	)
'

test_expect_success 'auto gc with too many loose objects does not attempt to create bitmaps' '
	test_config gc.auto 3 &&
	test_config gc.autodetach false &&
	test_config pack.writebitmaps true &&
	# We need to create two object whose sha1s start with 17
	# since this is what git gc counts.  As it happens, these
	# two blobs will do so.
	test_commit 263 &&
	test_commit 410 &&
	# Our first gc will create a pack; our second will create a second pack
	git gc --auto &&
	ls .git/objects/pack | sort >existing_packs &&
	test_commit 523 &&
	test_commit 790 &&

	git gc --auto 2>err &&
	test_i18ngrep ! "^warning:" err &&
	ls .git/objects/pack/ | sort >post_packs &&
	comm -1 -3 existing_packs post_packs >new &&
	comm -2 -3 existing_packs post_packs >del &&
	test_line_count = 0 del && # No packs are deleted
	test_line_count = 2 new # There is one new pack and its .idx
'

test_expect_success 'gc --no-quiet' '
	git -c gc.writeCommitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_line_count = 1 stderr &&
	test_i18ngrep "Computing commit graph generation numbers" stderr
'

test_expect_success TTY 'with TTY: gc --no-quiet' '
	test_terminal git -c gc.writeCommitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_i18ngrep "Enumerating objects" stderr &&
	test_i18ngrep "Computing commit graph generation numbers" stderr
'

test_expect_success 'gc --quiet' '
	git -c gc.writeCommitGraph=true gc --quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_must_be_empty stderr
'

run_and_wait_for_auto_gc () {
	# We read stdout from gc for the side effect of waiting until the
	# background gc process exits, closing its fd 9.  Furthermore, the
	# variable assignment from a command substitution preserves the
	# exit status of the main gc process.
	# Note: this fd trickery doesn't work on Windows, but there is no
	# need to, because on Win the auto gc always runs in the foreground.
	doesnt_matter=$(git gc --auto 9>&1)
}

test_expect_success 'background auto gc does not run if gc.log is present and recent but does if it is old' '
	test_commit foo &&
	test_commit bar &&
	git repack &&
	test_config gc.autopacklimit 1 &&
	test_config gc.autodetach true &&
	echo fleem >.git/gc.log &&
	git gc --auto 2>err &&
	test_i18ngrep "^warning:" err &&
	test_config gc.logexpiry 5.days &&
	test-tool chmtime =-345600 .git/gc.log &&
	git gc --auto &&
	test_config gc.logexpiry 2.days &&
	run_and_wait_for_auto_gc &&
	ls .git/objects/pack/pack-*.pack >packs &&
	test_line_count = 1 packs
'

test_expect_success 'background auto gc respects lock for all operations' '
	# make sure we run a background auto-gc
	test_commit make-pack &&
	git repack &&
	test_config gc.autopacklimit 1 &&
	test_config gc.autodetach true &&

	# create a ref whose loose presence we can use to detect a pack-refs run
	git update-ref refs/heads/should-be-loose HEAD &&
	test_path_is_file .git/refs/heads/should-be-loose &&

	# now fake a concurrent gc that holds the lock; we can use our
	# shell pid so that it looks valid.
	hostname=$(hostname || echo unknown) &&
	printf "$$ %s" "$hostname" >.git/gc.pid &&

	# our gc should exit zero without doing anything
	run_and_wait_for_auto_gc &&
	test_path_is_file .git/refs/heads/should-be-loose
'

# DO NOT leave a detached auto gc process running near the end of the
# test script: it can run long enough in the background to racily
# interfere with the cleanup in 'test_done'.

test_done
