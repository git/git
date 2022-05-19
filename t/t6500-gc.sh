#!/bin/sh

test_description='basic but gc tests
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'setup' '
	# do not let the amount of physical memory affects gc
	# behavior, make sure we always pack everything to one pack by
	# default
	but config gc.bigPackThreshold 2g &&

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
	but gc
'

test_expect_success 'gc does not leave behind pid file' '
	but gc &&
	test_path_is_missing .but/gc.pid
'

test_expect_success 'gc --gobbledegook' '
	test_expect_code 129 but gc --nonsense 2>err &&
	test_i18ngrep "[Uu]sage: but gc" err
'

test_expect_success 'gc -h with invalid configuration' '
	mkdir broken &&
	(
		cd broken &&
		but init &&
		echo "[gc] pruneexpire = CORRUPT" >>.but/config &&
		test_expect_code 129 but gc -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'gc is not aborted due to a stale symref' '
	but init remote &&
	(
		cd remote &&
		test_cummit initial &&
		but clone . ../client &&
		but branch -m develop &&
		cd ../client &&
		but fetch --prune &&
		but gc
	)
'

test_expect_success 'gc --keep-largest-pack' '
	test_create_repo keep-pack &&
	(
		cd keep-pack &&
		test_cummit one &&
		test_cummit two &&
		test_cummit three &&
		but gc &&
		( cd .but/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 1 pack-list &&
		cp pack-list base-pack-list &&
		test_cummit four &&
		but repack -d &&
		test_cummit five &&
		but repack -d &&
		( cd .but/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 3 pack-list &&
		but gc --keep-largest-pack &&
		( cd .but/objects/pack && ls *.pack ) >pack-list &&
		test_line_count = 2 pack-list &&
		awk "/^P /{print \$2}" <.but/objects/info/packs >pack-info &&
		test_line_count = 2 pack-info &&
		test_path_is_file .but/objects/pack/$(cat base-pack-list) &&
		but fsck
	)
'

test_expect_success 'pre-auto-gc hook can stop auto gc' '
	cat >err.expect <<-\EOF &&
	no gc for you
	EOF

	but init pre-auto-gc-hook &&
	test_hook -C pre-auto-gc-hook pre-auto-gc <<-\EOF &&
	echo >&2 no gc for you &&
	exit 1
	EOF
	(
		cd pre-auto-gc-hook &&

		but config gc.auto 3 &&
		but config gc.autoDetach false &&

		# We need to create two object whose sha1s start with 17
		# since this is what but gc counts.  As it happens, these
		# two blobs will do so.
		test_cummit "$(test_oid obj1)" &&
		test_cummit "$(test_oid obj2)" &&

		but gc --auto >../out.actual 2>../err.actual
	) &&
	test_must_be_empty out.actual &&
	test_cmp err.expect err.actual &&

	cat >err.expect <<-\EOF &&
	will gc for you
	Auto packing the repository for optimum performance.
	See "but help gc" for manual housekeeping.
	EOF

	test_hook -C pre-auto-gc-hook --clobber pre-auto-gc <<-\EOF &&
	echo >&2 will gc for you &&
	exit 0
	EOF

	but -C pre-auto-gc-hook gc --auto >out.actual 2>err.actual &&

	test_must_be_empty out.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'auto gc with too many loose objects does not attempt to create bitmaps' '
	test_config gc.auto 3 &&
	test_config gc.autodetach false &&
	test_config pack.writebitmaps true &&
	# We need to create two object whose sha1s start with 17
	# since this is what but gc counts.  As it happens, these
	# two blobs will do so.
	test_cummit "$(test_oid obj1)" &&
	test_cummit "$(test_oid obj2)" &&
	# Our first gc will create a pack; our second will create a second pack
	but gc --auto &&
	ls .but/objects/pack/pack-*.pack | sort >existing_packs &&
	test_cummit "$(test_oid obj3)" &&
	test_cummit "$(test_oid obj4)" &&

	but gc --auto 2>err &&
	test_i18ngrep ! "^warning:" err &&
	ls .but/objects/pack/pack-*.pack | sort >post_packs &&
	comm -1 -3 existing_packs post_packs >new &&
	comm -2 -3 existing_packs post_packs >del &&
	test_line_count = 0 del && # No packs are deleted
	test_line_count = 1 new # There is one new pack
'

test_expect_success 'gc --no-quiet' '
	BUT_PROGRESS_DELAY=0 but -c gc.writecummitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_i18ngrep "Computing cummit graph generation numbers" stderr
'

test_expect_success TTY 'with TTY: gc --no-quiet' '
	test_terminal env BUT_PROGRESS_DELAY=0 \
		but -c gc.writecummitGraph=true gc --no-quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_i18ngrep "Enumerating objects" stderr &&
	test_i18ngrep "Computing cummit graph generation numbers" stderr
'

test_expect_success 'gc --quiet' '
	but -c gc.writecummitGraph=true gc --quiet >stdout 2>stderr &&
	test_must_be_empty stdout &&
	test_must_be_empty stderr
'

test_expect_success 'gc.reflogExpire{Unreachable,}=never skips "expire" via "gc"' '
	test_config gc.reflogExpire never &&
	test_config gc.reflogExpireUnreachable never &&

	BUT_TRACE=$(pwd)/trace.out but gc &&

	# Check that but-pack-refs is run as a sanity check (done via
	# gc_before_repack()) but that but-expire is not.
	grep -E "^trace: (built-in|exec|run_command): but pack-refs --" trace.out &&
	! grep -E "^trace: (built-in|exec|run_command): but reflog expire --" trace.out
'

test_expect_success 'one of gc.reflogExpire{Unreachable,}=never does not skip "expire" via "gc"' '
	>trace.out &&
	test_config gc.reflogExpire never &&
	BUT_TRACE=$(pwd)/trace.out but gc &&
	grep -E "^trace: (built-in|exec|run_command): but reflog expire --" trace.out
'

run_and_wait_for_auto_gc () {
	# We read stdout from gc for the side effect of waiting until the
	# background gc process exits, closing its fd 9.  Furthermore, the
	# variable assignment from a command substitution preserves the
	# exit status of the main gc process.
	# Note: this fd trickery doesn't work on Windows, but there is no
	# need to, because on Win the auto gc always runs in the foreground.
	doesnt_matter=$(but gc --auto 9>&1)
}

test_expect_success 'background auto gc does not run if gc.log is present and recent but does if it is old' '
	test_cummit foo &&
	test_cummit bar &&
	but repack &&
	test_config gc.autopacklimit 1 &&
	test_config gc.autodetach true &&
	echo fleem >.but/gc.log &&
	but gc --auto 2>err &&
	test_i18ngrep "^warning:" err &&
	test_config gc.logexpiry 5.days &&
	test-tool chmtime =-345600 .but/gc.log &&
	but gc --auto &&
	test_config gc.logexpiry 2.days &&
	run_and_wait_for_auto_gc &&
	ls .but/objects/pack/pack-*.pack >packs &&
	test_line_count = 1 packs
'

test_expect_success 'background auto gc respects lock for all operations' '
	# make sure we run a background auto-gc
	test_cummit make-pack &&
	but repack &&
	test_config gc.autopacklimit 1 &&
	test_config gc.autodetach true &&

	# create a ref whose loose presence we can use to detect a pack-refs run
	but update-ref refs/heads/should-be-loose HEAD &&
	(ls -1 .but/refs/heads .but/reftable >expect || true) &&

	# now fake a concurrent gc that holds the lock; we can use our
	# shell pid so that it looks valid.
	hostname=$(hostname || echo unknown) &&
	shell_pid=$$ &&
	if test_have_prereq MINGW && test -f /proc/$shell_pid/winpid
	then
		# In Git for Windows, Bash (actually, the MSYS2 runtime) has a
		# different idea of PIDs than but.exe (actually Windows). Use
		# the Windows PID in this case.
		shell_pid=$(cat /proc/$shell_pid/winpid)
	fi &&
	printf "%d %s" "$shell_pid" "$hostname" >.but/gc.pid &&

	# our gc should exit zero without doing anything
	run_and_wait_for_auto_gc &&
	(ls -1 .but/refs/heads .but/reftable >actual || true) &&
	test_cmp expect actual
'

# DO NOT leave a detached auto gc process running near the end of the
# test script: it can run long enough in the background to racily
# interfere with the cleanup in 'test_done'.

test_done
