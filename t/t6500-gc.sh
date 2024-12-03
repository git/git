#!/bin/sh

test_description='basic git gc tests
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'setup' '
	# do not let the amount of physical memory affects gc
	# behavior, make sure we always pack everything to one pack by
	# default
	git config gc.bigPackThreshold 2g &&
	test_oid_init
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
	test_grep "[Uu]sage: git gc" err
'

test_expect_success 'gc -h with invalid configuration' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		echo "[gc] pruneexpire = CORRUPT" >>.git/config &&
		test_expect_code 129 git gc -h >usage 2>&1
	) &&
	test_grep "[Uu]sage" broken/usage
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
		test_commit "$(test_oid blob17_1)" &&
		test_commit "$(test_oid blob17_2)" &&

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
	test_commit "$(test_oid blob17_1)" &&
	test_commit "$(test_oid blob17_2)" &&
	# Our first gc will create a pack; our second will create a second pack
	git gc --auto &&
	ls .git/objects/pack/pack-*.pack | sort >existing_packs &&
	test_commit "$(test_oid blob17_3)" &&
	test_commit "$(test_oid blob17_4)" &&

	git gc --auto 2>err &&
	test_grep ! "^warning:" err &&
	ls .git/objects/pack/pack-*.pack | sort >post_packs &&
	comm -1 -3 existing_packs post_packs >new &&
	comm -2 -3 existing_packs post_packs >del &&
	test_line_count = 0 del && # No packs are deleted
	test_line_count = 1 new # There is one new pack
'

test_expect_success 'gc --no-quiet' '
	GIT_PROGRESS_DELAY=0 git -c gc.writeCommitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_grep "Computing commit graph generation numbers" stderr
'

test_expect_success TTY 'with TTY: gc --no-quiet' '
	test_terminal env GIT_PROGRESS_DELAY=0 \
		git -c gc.writeCommitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_grep "Enumerating objects" stderr &&
	test_grep "Computing commit graph generation numbers: 100% (4/4), done." stderr
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

test_expect_success 'gc.repackFilter launches repack with a filter' '
	git clone --no-local --bare . bare.git &&

	git -C bare.git -c gc.cruftPacks=false gc &&
	test_stdout_line_count = 1 ls bare.git/objects/pack/*.pack &&

	GIT_TRACE=$(pwd)/trace.out git -C bare.git -c gc.repackFilter=blob:none \
		-c repack.writeBitmaps=false -c gc.cruftPacks=false gc &&
	test_stdout_line_count = 2 ls bare.git/objects/pack/*.pack &&
	grep -E "^trace: (built-in|exec|run_command): git repack .* --filter=blob:none ?.*" trace.out
'

test_expect_success 'gc.repackFilterTo store filtered out objects' '
	test_when_finished "rm -rf bare.git filtered.git" &&

	git init --bare filtered.git &&
	git -C bare.git -c gc.repackFilter=blob:none \
		-c gc.repackFilterTo=../filtered.git/objects/pack/pack \
		-c repack.writeBitmaps=false -c gc.cruftPacks=false gc &&

	test_stdout_line_count = 1 ls bare.git/objects/pack/*.pack &&
	test_stdout_line_count = 1 ls filtered.git/objects/pack/*.pack
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

cruft_max_size_opts="git repack -d -l --cruft --cruft-expiration=2.weeks.ago"

test_expect_success 'setup for --max-cruft-size tests' '
	git init cruft--max-size &&
	(
		cd cruft--max-size &&
		prepare_cruft_history
	)
'

test_expect_success '--max-cruft-size sets appropriate repack options' '
	GIT_TRACE2_EVENT=$(pwd)/trace2.txt git -C cruft--max-size \
		gc --cruft --max-cruft-size=1M &&
	test_subcommand $cruft_max_size_opts --max-cruft-size=1048576 <trace2.txt
'

test_expect_success 'gc.maxCruftSize sets appropriate repack options' '
	GIT_TRACE2_EVENT=$(pwd)/trace2.txt \
		git -C cruft--max-size -c gc.maxCruftSize=2M gc --cruft &&
	test_subcommand $cruft_max_size_opts --max-cruft-size=2097152 <trace2.txt &&

	GIT_TRACE2_EVENT=$(pwd)/trace2.txt \
		git -C cruft--max-size -c gc.maxCruftSize=2M gc --cruft \
		--max-cruft-size=3M &&
	test_subcommand $cruft_max_size_opts --max-cruft-size=3145728 <trace2.txt
'

run_and_wait_for_gc () {
	# We read stdout from gc for the side effect of waiting until the
	# background gc process exits, closing its fd 9.  Furthermore, the
	# variable assignment from a command substitution preserves the
	# exit status of the main gc process.
	# Note: this fd trickery doesn't work on Windows, but there is no
	# need to, because on Win the auto gc always runs in the foreground.
	doesnt_matter=$(git gc "$@" 9>&1)
}

test_expect_success 'background auto gc does not run if gc.log is present and recent but does if it is old' '
	test_commit foo &&
	test_commit bar &&
	git repack &&
	test_config gc.autopacklimit 1 &&
	test_config gc.autodetach true &&
	echo fleem >.git/gc.log &&
	git gc --auto 2>err &&
	test_grep "^warning:" err &&
	test_config gc.logexpiry 5.days &&
	test-tool chmtime =-345600 .git/gc.log &&
	git gc --auto &&
	test_config gc.logexpiry 2.days &&
	run_and_wait_for_gc --auto &&
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
	run_and_wait_for_gc --auto &&
	(ls -1 .git/refs/heads .git/reftable >actual || true) &&
	test_cmp expect actual
'

test_expect_success '--detach overrides gc.autoDetach=false' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		# Prepare the repository such that git-gc(1) ends up repacking.
		test_commit "$(test_oid blob17_1)" &&
		test_commit "$(test_oid blob17_2)" &&
		git config gc.autodetach false &&
		git config gc.auto 2 &&

		# Note that we cannot use `test_cmp` here to compare stderr
		# because it may contain output from `set -x`.
		run_and_wait_for_gc --auto --detach 2>actual &&
		test_grep "Auto packing the repository in background for optimum performance." actual
	)
'

test_expect_success '--no-detach overrides gc.autoDetach=true' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		# Prepare the repository such that git-gc(1) ends up repacking.
		test_commit "$(test_oid blob17_1)" &&
		test_commit "$(test_oid blob17_2)" &&
		git config gc.autodetach true &&
		git config gc.auto 2 &&

		GIT_PROGRESS_DELAY=0 git gc --auto --no-detach 2>output &&
		test_grep "Auto packing the repository for optimum performance." output &&
		test_grep "Collecting referenced commits: 2, done." output
	)
'

# DO NOT leave a detached auto gc process running near the end of the
# test script: it can run long enough in the background to racily
# interfere with the cleanup in 'test_done'.

test_done
