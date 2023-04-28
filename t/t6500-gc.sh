#!/bin/sh

test_description='basic git gc tests
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'setup' '
	# do not let the amount of physical memory affects gc
	# behavior, make sure we always pack everything to one pack by
	# default
	git config gc.bigPackThreshold 2g &&

	# These are simply values which, when hashed as a blob with a newline,
	# produce a hash where the first byte is 0x17 in their respective
	# algorithms.
	test_oid_cache <<-EOF
	obj1 sha1:263
	obj1 sha256:34

	obj2 sha1:410
	obj2 sha256:174

	obj3 sha1:523
	obj3 sha256:313

	obj4 sha1:790
	obj4 sha256:481
	EOF
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
		cp pack-list base-pack-list &&
		test_commit four &&
		git repack -d &&
		test_commit five &&
		git repack -d &&
		( cd .git/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 3 pack-list &&
		git gc --keep-largest-pack &&
		( cd .git/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 2 pack-list &&
		awk "/^P /{print \$2}" <.git/objects/info/packs >pack-info &&
		test_line_count = 2 pack-info &&
		test_path_is_file .git/objects/pack/$(cat base-pack-list) &&
		git fsck
	)
'

test_expect_success 'pre-auto-gc hook can stop auto gc' '
	cat >err.expect <<-\EOF &&
	no gc for you
	EOF

	git init pre-auto-gc-hook &&
	test_hook -C pre-auto-gc-hook pre-auto-gc <<-\EOF &&
	echo >&2 no gc for you &&
	exit 1
	EOF
	(
		cd pre-auto-gc-hook &&

		git config gc.auto 3 &&
		git config gc.autoDetach false &&

		# We need to create two object whose sha1s start with 17
		# since this is what git gc counts.  As it happens, these
		# two blobs will do so.
		test_commit "$(test_oid obj1)" &&
		test_commit "$(test_oid obj2)" &&

		git gc --auto >../out.actual 2>../err.actual
	) &&
	test_must_be_empty out.actual &&
	test_cmp err.expect err.actual &&

	cat >err.expect <<-\EOF &&
	will gc for you
	Auto packing the repository for optimum performance.
	See "git help gc" for manual housekeeping.
	EOF

	test_hook -C pre-auto-gc-hook --clobber pre-auto-gc <<-\EOF &&
	echo >&2 will gc for you &&
	exit 0
	EOF

	git -C pre-auto-gc-hook gc --auto >out.actual 2>err.actual &&

	test_must_be_empty out.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'auto gc with too many loose objects does not attempt to create bitmaps' '
	test_config gc.auto 3 &&
	test_config gc.autodetach false &&
	test_config pack.writebitmaps true &&
	# We need to create two object whose sha1s start with 17
	# since this is what git gc counts.  As it happens, these
	# two blobs will do so.
	test_commit "$(test_oid obj1)" &&
	test_commit "$(test_oid obj2)" &&
	# Our first gc will create a pack; our second will create a second pack
	git gc --auto &&
	ls .git/objects/pack/pack-*.pack | sort >existing_packs &&
	test_commit "$(test_oid obj3)" &&
	test_commit "$(test_oid obj4)" &&

	git gc --auto 2>err &&
	test_i18ngrep ! "^warning:" err &&
	ls .git/objects/pack/pack-*.pack | sort >post_packs &&
	comm -1 -3 existing_packs post_packs >new &&
	comm -2 -3 existing_packs post_packs >del &&
	test_line_count = 0 del && # No packs are deleted
	test_line_count = 1 new # There is one new pack
'

test_expect_success 'gc --no-quiet' '
	GIT_PROGRESS_DELAY=0 git -c gc.writeCommitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_i18ngrep "Computing commit graph generation numbers" stderr
'

test_expect_success TTY 'with TTY: gc --no-quiet' '
	test_terminal env GIT_PROGRESS_DELAY=0 \
		git -c gc.writeCommitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_i18ngrep "Enumerating objects" stderr &&
	test_i18ngrep "Computing commit graph generation numbers" stderr
'

test_expect_success 'gc --quiet' '
	git -c gc.writeCommitGraph=true gc --quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_must_be_empty stderr
'

test_expect_success 'gc.reflogExpire{Unreachable,}=never skips "expire" via "gc"' '
	test_config gc.reflogExpire never &&
	test_config gc.reflogExpireUnreachable never &&

	GIT_TRACE=$(pwd)/trace.out git gc &&

	# Check that git-pack-refs is run as a sanity check (done via
	# gc_before_repack()) but that git-expire is not.
	grep -E "^trace: (built-in|exec|run_command): git pack-refs --" trace.out &&
	! grep -E "^trace: (built-in|exec|run_command): git reflog expire --" trace.out
'

test_expect_success 'one of gc.reflogExpire{Unreachable,}=never does not skip "expire" via "gc"' '
	>trace.out &&
	test_config gc.reflogExpire never &&
	GIT_TRACE=$(pwd)/trace.out git gc &&
	grep -E "^trace: (built-in|exec|run_command): git reflog expire --" trace.out
'

prepare_cruft_history () {
	test_commit base &&

	test_commit --no-tag foo &&
	test_commit --no-tag bar &&
	git reset HEAD^^
}

assert_no_cruft_packs () {
	find .git/objects/pack -name "*.mtimes" >mtimes &&
	test_must_be_empty mtimes
}

for argv in \
	"gc" \
	"-c gc.cruftPacks=true gc" \
	"-c gc.cruftPacks=false gc --cruft"
do
	test_expect_success "git $argv generates a cruft pack" '
		test_when_finished "rm -fr repo" &&
		git init repo &&
		(
			cd repo &&

			prepare_cruft_history &&
			git $argv &&

			find .git/objects/pack -name "*.mtimes" >mtimes &&
			sed -e 's/\.mtimes$/\.pack/g' mtimes >packs &&

			test_file_not_empty packs &&
			while read pack
			do
				test_path_is_file "$pack" || return 1
			done <packs
		)
	'
done

for argv in \
	"gc --no-cruft" \
	"-c gc.cruftPacks=false gc" \
	"-c gc.cruftPacks=true gc --no-cruft"
do
	test_expect_success "git $argv does not generate a cruft pack" '
		test_when_finished "rm -fr repo" &&
		git init repo &&
		(
			cd repo &&

			prepare_cruft_history &&
			git $argv &&

			assert_no_cruft_packs
		)
	'
done

test_expect_success '--keep-largest-pack ignores cruft packs' '
	test_when_finished "rm -fr repo" &&
	git init repo &&
	(
		cd repo &&

		# Generate a pack for reachable objects (of which there
		# are 3), and one for unreachable objects (of which
		# there are 6).
		prepare_cruft_history &&
		git gc --cruft &&

		mtimes="$(find .git/objects/pack -type f -name "pack-*.mtimes")" &&
		sz="$(test_file_size "${mtimes%.mtimes}.pack")" &&

		# Ensure that the cruft pack gets removed (due to
		# `--prune=now`) despite it being the largest pack.
		git -c gc.bigPackThreshold=$sz gc --cruft --prune=now &&

		assert_no_cruft_packs
	)
'

test_expect_success 'gc.bigPackThreshold ignores cruft packs' '
	test_when_finished "rm -fr repo" &&
	git init repo &&
	(
		cd repo &&

		# Generate a pack for reachable objects (of which there
		# are 3), and one for unreachable objects (of which
		# there are 6).
		prepare_cruft_history &&
		git gc --cruft &&

		# Ensure that the cruft pack gets removed (due to
		# `--prune=now`) despite it being the largest pack.
		git gc --cruft --prune=now --keep-largest-pack &&

		assert_no_cruft_packs
	)
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
	(ls -1 .git/refs/heads .git/reftable >expect || true) &&

	# now fake a concurrent gc that holds the lock; we can use our
	# shell pid so that it looks valid.
	hostname=$(hostname || echo unknown) &&
	shell_pid=$$ &&
	if test_have_prereq MINGW && test -f /proc/$shell_pid/winpid
	then
		# In Git for Windows, Bash (actually, the MSYS2 runtime) has a
		# different idea of PIDs than git.exe (actually Windows). Use
		# the Windows PID in this case.
		shell_pid=$(cat /proc/$shell_pid/winpid)
	fi &&
	printf "%d %s" "$shell_pid" "$hostname" >.git/gc.pid &&

	# our gc should exit zero without doing anything
	run_and_wait_for_auto_gc &&
	(ls -1 .git/refs/heads .git/reftable >actual || true) &&
	test_cmp expect actual
'

# DO NOT leave a detached auto gc process running near the end of the
# test script: it can run long enough in the background to racily
# interfere with the cleanup in 'test_done'.

test_done
