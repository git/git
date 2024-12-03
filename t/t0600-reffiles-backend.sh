#!/bin/sh

test_description='Test reffiles backend'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=files
export GIT_TEST_DEFAULT_REF_FORMAT

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	git commit --allow-empty -m Initial &&
	C=$(git rev-parse HEAD) &&
	git commit --allow-empty -m Second &&
	D=$(git rev-parse HEAD) &&
	git commit --allow-empty -m Third &&
	E=$(git rev-parse HEAD)
'

test_expect_success 'empty directory should not fool rev-parse' '
	prefix=refs/e-rev-parse &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	echo "$C" >expected &&
	git rev-parse $prefix/foo >actual &&
	test_cmp expected actual
'

test_expect_success 'empty directory should not fool for-each-ref' '
	prefix=refs/e-for-each-ref &&
	git update-ref $prefix/foo $C &&
	git for-each-ref $prefix >expected &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	git for-each-ref $prefix >actual &&
	test_cmp expected actual
'

test_expect_success 'empty directory should not fool create' '
	prefix=refs/e-create &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "create %s $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool verify' '
	prefix=refs/e-verify &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "verify %s $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 1-arg update' '
	prefix=refs/e-update-1 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "update %s $D\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 2-arg update' '
	prefix=refs/e-update-2 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "update %s $D $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 0-arg delete' '
	prefix=refs/e-delete-0 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "delete %s\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 1-arg delete' '
	prefix=refs/e-delete-1 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "delete %s $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'non-empty directory blocks create' - <<\EOT
	prefix=refs/ne-create &&
	mkdir -p .git/$prefix/foo/bar &&
	: >.git/$prefix/foo/bar/baz.lock &&
	test_when_finished "rm -f .git/$prefix/foo/bar/baz.lock" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/foo': there is a non-empty directory '.git/$prefix/foo' blocking reference '$prefix/foo'
	EOF
	printf "%s\n" "update $prefix/foo $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/foo': unable to resolve reference '$prefix/foo'
	EOF
	printf "%s\n" "update $prefix/foo $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
EOT

test_expect_success 'broken reference blocks create' - <<\EOT
	prefix=refs/broken-create &&
	mkdir -p .git/$prefix &&
	echo "gobbledigook" >.git/$prefix/foo &&
	test_when_finished "rm -f .git/$prefix/foo" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/foo': unable to resolve reference '$prefix/foo': reference broken
	EOF
	printf "%s\n" "update $prefix/foo $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/foo': unable to resolve reference '$prefix/foo': reference broken
	EOF
	printf "%s\n" "update $prefix/foo $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
EOT

test_expect_success 'non-empty directory blocks indirect create' - <<\EOT
	prefix=refs/ne-indirect-create &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	mkdir -p .git/$prefix/foo/bar &&
	: >.git/$prefix/foo/bar/baz.lock &&
	test_when_finished "rm -f .git/$prefix/foo/bar/baz.lock" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/symref': there is a non-empty directory '.git/$prefix/foo' blocking reference '$prefix/foo'
	EOF
	printf "%s\n" "update $prefix/symref $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/symref': unable to resolve reference '$prefix/foo'
	EOF
	printf "%s\n" "update $prefix/symref $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
EOT

test_expect_success 'broken reference blocks indirect create' - <<\EOT
	prefix=refs/broken-indirect-create &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	echo "gobbledigook" >.git/$prefix/foo &&
	test_when_finished "rm -f .git/$prefix/foo" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/symref': unable to resolve reference '$prefix/foo': reference broken
	EOF
	printf "%s\n" "update $prefix/symref $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref '$prefix/symref': unable to resolve reference '$prefix/foo': reference broken
	EOF
	printf "%s\n" "update $prefix/symref $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
EOT

test_expect_success 'no bogus intermediate values during delete' '
	prefix=refs/slow-transaction &&
	# Set up a reference with differing loose and packed versions:
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	git update-ref $prefix/foo $D &&
	# Now try to update the reference, but hold the `packed-refs` lock
	# for a while to see what happens while the process is blocked:
	: >.git/packed-refs.lock &&
	test_when_finished "rm -f .git/packed-refs.lock" &&
	{
		# Note: the following command is intentionally run in the
		# background. We increase the timeout so that `update-ref`
		# attempts to acquire the `packed-refs` lock for much longer
		# than it takes for us to do the check then delete it:
		git -c core.packedrefstimeout=30000 update-ref -d $prefix/foo &
	} &&
	pid2=$! &&
	# Give update-ref plenty of time to get to the point where it tries
	# to lock packed-refs:
	sleep 1 &&
	# Make sure that update-ref did not complete despite the lock:
	kill -0 $pid2 &&
	# Verify that the reference still has its old value:
	sha1=$(git rev-parse --verify --quiet $prefix/foo || echo undefined) &&
	case "$sha1" in
	$D)
		# This is what we hope for; it means that nothing
		# user-visible has changed yet.
		: ;;
	undefined)
		# This is not correct; it means the deletion has happened
		# already even though update-ref should not have been
		# able to acquire the lock yet.
		echo "$prefix/foo deleted prematurely" &&
		break
		;;
	$C)
		# This value should never be seen. Probably the loose
		# reference has been deleted but the packed reference
		# is still there:
		echo "$prefix/foo incorrectly observed to be C" &&
		break
		;;
	*)
		# WTF?
		echo "unexpected value observed for $prefix/foo: $sha1" &&
		break
		;;
	esac >out &&
	rm -f .git/packed-refs.lock &&
	wait $pid2 &&
	test_must_be_empty out &&
	test_must_fail git rev-parse --verify --quiet $prefix/foo
'

test_expect_success 'delete fails cleanly if packed-refs file is locked' - <<\EOT
	prefix=refs/locked-packed-refs &&
	# Set up a reference with differing loose and packed versions:
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	git update-ref $prefix/foo $D &&
	git for-each-ref $prefix >unchanged &&
	# Now try to delete it while the `packed-refs` lock is held:
	: >.git/packed-refs.lock &&
	test_when_finished "rm -f .git/packed-refs.lock" &&
	test_must_fail git update-ref -d $prefix/foo >out 2>err &&
	git for-each-ref $prefix >actual &&
	test_grep "Unable to create '.*packed-refs.lock': " err &&
	test_cmp unchanged actual
EOT

test_expect_success 'delete fails cleanly if packed-refs.new write fails' '
	# Setup and expectations are similar to the test above.
	prefix=refs/failed-packed-refs &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	git update-ref $prefix/foo $D &&
	git for-each-ref $prefix >unchanged &&
	# This should not happen in practice, but it is an easy way to get a
	# reliable error (we open with create_tempfile(), which uses O_EXCL).
	: >.git/packed-refs.new &&
	test_when_finished "rm -f .git/packed-refs.new" &&
	test_must_fail git update-ref -d $prefix/foo &&
	git for-each-ref $prefix >actual &&
	test_cmp unchanged actual
'

RWT="test-tool ref-store worktree:wt"
RMAIN="test-tool ref-store worktree:main"

test_expect_success 'setup worktree' '
	test_commit first &&
	git worktree add -b wt-main wt &&
	(
		cd wt &&
		test_commit second
	)
'

# Some refs (refs/bisect/*, pseudorefs) are kept per worktree, so they should
# only appear in the for-each-reflog output if it is called from the correct
# worktree, which is exercised in this test. This test is poorly written for
# multiple reasons: 1) it creates invalidly formatted log entries. 2) it uses
# direct FS access for creating the reflogs. 3) PSEUDO-WT and refs/bisect/random
# do not create reflogs by default, so it is not testing a realistic scenario.
test_expect_success 'for_each_reflog()' '
	echo $ZERO_OID >.git/logs/PSEUDO_MAIN_HEAD &&
	mkdir -p     .git/logs/refs/bisect &&
	echo $ZERO_OID >.git/logs/refs/bisect/random &&

	echo $ZERO_OID >.git/worktrees/wt/logs/PSEUDO_WT_HEAD &&
	mkdir -p     .git/worktrees/wt/logs/refs/bisect &&
	echo $ZERO_OID >.git/worktrees/wt/logs/refs/bisect/wt-random &&

	$RWT for-each-reflog >actual &&
	cat >expected <<-\EOF &&
	HEAD
	PSEUDO_WT_HEAD
	refs/bisect/wt-random
	refs/heads/main
	refs/heads/wt-main
	EOF
	test_cmp expected actual &&

	$RMAIN for-each-reflog >actual &&
	cat >expected <<-\EOF &&
	HEAD
	PSEUDO_MAIN_HEAD
	refs/bisect/random
	refs/heads/main
	refs/heads/wt-main
	EOF
	test_cmp expected actual
'

# Triggering the bug detected by this test requires a newline to fall
# exactly BUFSIZ-1 bytes from the end of the file. We don't know
# what that value is, since it's platform dependent. However, if
# we choose some value N, we also catch any D which divides N evenly
# (since we will read backwards in chunks of D). So we choose 8K,
# which catches glibc (with an 8K BUFSIZ) and *BSD (1K).
#
# Each line is 114 characters, so we need 75 to still have a few before the
# last 8K. The 89-character padding on the final entry lines up our
# newline exactly.
test_expect_success SHA1 'parsing reverse reflogs at BUFSIZ boundaries' '
	git checkout -b reflogskip &&
	zf=$(test_oid zero_2) &&
	ident="abc <xyz> 0000000001 +0000" &&
	for i in $(test_seq 1 75); do
		printf "$zf%02d $zf%02d %s\t" $i $(($i+1)) "$ident" &&
		if test $i = 75; then
			for j in $(test_seq 1 89); do
				printf X || return 1
			done
		else
			printf X
		fi &&
		printf "\n" || return 1
	done >.git/logs/refs/heads/reflogskip &&
	git rev-parse reflogskip@{73} >actual &&
	echo ${zf}03 >expect &&
	test_cmp expect actual
'

# This test takes a lock on an individual ref; this is not supported in
# reftable.
test_expect_success 'reflog expire operates on symref not referrent' '
	git branch --create-reflog the_symref &&
	git branch --create-reflog referrent &&
	git update-ref referrent HEAD &&
	git symbolic-ref refs/heads/the_symref refs/heads/referrent &&
	test_when_finished "rm -f .git/refs/heads/referrent.lock" &&
	touch .git/refs/heads/referrent.lock &&
	git reflog expire --expire=all the_symref
'

test_expect_success 'empty reflog' '
	test_when_finished "rm -rf empty" &&
	git init empty &&
	test_commit -C empty A &&
	>empty/.git/logs/refs/heads/foo &&
	git -C empty reflog expire --all 2>err &&
	test_must_be_empty err
'

test_expect_success SYMLINKS 'ref resolution not confused by broken symlinks' '
       ln -s does-not-exist .git/refs/heads/broken &&
       test_must_fail git rev-parse --verify broken
'

test_expect_success 'log diagnoses bogus HEAD hash' '
	git init empty &&
	test_when_finished "rm -rf empty" &&
	echo 1234abcd >empty/.git/refs/heads/main &&
	test_must_fail git -C empty log 2>stderr &&
	test_grep broken stderr
'

test_expect_success 'log diagnoses bogus HEAD symref' '
	git init empty &&
	test-tool -C empty ref-store main create-symref HEAD refs/heads/invalid.lock &&
	test_must_fail git -C empty log 2>stderr &&
	test_grep broken stderr &&
	test_must_fail git -C empty log --default totally-bogus 2>stderr &&
	test_grep broken stderr
'

test_expect_success 'empty directory removal' '
	git branch d1/d2/r1 HEAD &&
	git branch d1/r2 HEAD &&
	test_path_is_file .git/refs/heads/d1/d2/r1 &&
	test_path_is_file .git/logs/refs/heads/d1/d2/r1 &&
	git branch -d d1/d2/r1 &&
	test_must_fail git show-ref --verify -q refs/heads/d1/d2 &&
	test_must_fail git show-ref --verify -q logs/refs/heads/d1/d2 &&
	test_path_is_file .git/refs/heads/d1/r2 &&
	test_path_is_file .git/logs/refs/heads/d1/r2
'

test_expect_success 'symref empty directory removal' '
	git branch e1/e2/r1 HEAD &&
	git branch e1/r2 HEAD &&
	git checkout e1/e2/r1 &&
	test_when_finished "git checkout main" &&
	test_path_is_file .git/refs/heads/e1/e2/r1 &&
	test_path_is_file .git/logs/refs/heads/e1/e2/r1 &&
	git update-ref -d HEAD &&
	test_must_fail git show-ref --verify -q refs/heads/e1/e2 &&
	test_must_fail git show-ref --verify -q logs/refs/heads/e1/e2 &&
	test_path_is_file .git/refs/heads/e1/r2 &&
	test_path_is_file .git/logs/refs/heads/e1/r2 &&
	test_path_is_file .git/logs/HEAD
'

test_expect_success 'directory not created deleting packed ref' '
	git branch d1/d2/r1 HEAD &&
	git pack-refs --all &&
	test_path_is_missing .git/refs/heads/d1/d2 &&
	git update-ref -d refs/heads/d1/d2/r1 &&
	test_path_is_missing .git/refs/heads/d1/d2 &&
	test_path_is_missing .git/refs/heads/d1
'

test_expect_success SYMLINKS 'git branch -m u v should fail when the reflog for u is a symlink' '
	git branch --create-reflog u &&
	mv .git/logs/refs/heads/u real-u &&
	ln -s real-u .git/logs/refs/heads/u &&
	test_must_fail git branch -m u v
'

test_expect_success SYMLINKS 'git branch -m with symlinked .git/refs' '
	test_when_finished "rm -rf subdir" &&
	git init --bare subdir &&

	rm -rf subdir/refs subdir/objects subdir/packed-refs &&
	ln -s ../.git/refs subdir/refs &&
	ln -s ../.git/objects subdir/objects &&
	ln -s ../.git/packed-refs subdir/packed-refs &&

	git -C subdir rev-parse --absolute-git-dir >subdir.dir &&
	git rev-parse --absolute-git-dir >our.dir &&
	! test_cmp subdir.dir our.dir &&

	git -C subdir log &&
	git -C subdir branch rename-src &&
	git rev-parse rename-src >expect &&
	git -C subdir branch -m rename-src rename-dest &&
	git rev-parse rename-dest >actual &&
	test_cmp expect actual &&
	git branch -D rename-dest
'

test_expect_success MINGW,SYMLINKS_WINDOWS 'rebase when .git/logs is a symlink' '
	git checkout main &&
	mv .git/logs actual_logs &&
	cmd //c "mklink /D .git\logs ..\actual_logs" &&
	git rebase -f HEAD^ &&
	test -L .git/logs &&
	rm .git/logs &&
	mv actual_logs .git/logs
'

test_expect_success POSIXPERM 'git reflog expire honors core.sharedRepository' '
	umask 077 &&
	git config core.sharedRepository group &&
	git reflog expire --all &&
	actual="$(ls -l .git/logs/refs/heads/main)" &&
	case "$actual" in
	-rw-rw-*)
		: happy
		;;
	*)
		echo Ooops, .git/logs/refs/heads/main is not 066x [$actual]
		false
		;;
	esac
'

test_expect_success SYMLINKS 'symref transaction supports symlinks' '
	test_when_finished "git symbolic-ref -d TEST_SYMREF_HEAD" &&
	git update-ref refs/heads/new @ &&
	test_config core.prefersymlinkrefs true &&
	cat >stdin <<-EOF &&
	start
	symref-create TEST_SYMREF_HEAD refs/heads/new
	prepare
	commit
	EOF
	git update-ref --no-deref --stdin <stdin &&
	test_path_is_symlink .git/TEST_SYMREF_HEAD &&
	test "$(test_readlink .git/TEST_SYMREF_HEAD)" = refs/heads/new
'

test_expect_success 'symref transaction supports false symlink config' '
	test_when_finished "git symbolic-ref -d TEST_SYMREF_HEAD" &&
	git update-ref refs/heads/new @ &&
	test_config core.prefersymlinkrefs false &&
	cat >stdin <<-EOF &&
	start
	symref-create TEST_SYMREF_HEAD refs/heads/new
	prepare
	commit
	EOF
	git update-ref --no-deref --stdin <stdin &&
	test_path_is_file .git/TEST_SYMREF_HEAD &&
	git symbolic-ref TEST_SYMREF_HEAD >actual &&
	echo refs/heads/new >expect &&
	test_cmp expect actual
'

test_done
