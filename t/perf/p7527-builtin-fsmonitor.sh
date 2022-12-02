#!/bin/sh

test_description="Perf test for the builtin FSMonitor"

. ./perf-lib.sh

if ! test_have_prereq FSMONITOR_DAEMON
then
	skip_all="fsmonitor--daemon is not supported on this platform"
	test_done
fi

test_lazy_prereq UNTRACKED_CACHE '
	{ git update-index --test-untracked-cache; ret=$?; } &&
	test $ret -ne 1
'

# Lie to perf-lib and ask for a new empty repo and avoid
# the complaints about GIT_PERF_REPO not being big enough
# the perf hit when GIT_PERF_LARGE_REPO is copied into
# the trash directory.
#
# NEEDSWORK: It would be nice if perf-lib had an option to
# "borrow" an existing large repo (especially for gigantic
# monorepos) and use it in-place.  For now, fake it here.
#
test_perf_fresh_repo


# Use a generated synthetic monorepo.  If it doesn't exist, we will
# generate it.  If it does exist, we will put it in a known state
# before we start our timings.
#
PARAM_D=5
PARAM_W=10
PARAM_F=9

PARAMS="$PARAM_D"."$PARAM_W"."$PARAM_F"

BALLAST_BR=p0006-ballast
export BALLAST_BR

TMP_BR=tmp_br
export TMP_BR

REPO=../repos/gen-many-files-"$PARAMS".git
export REPO

if ! test -d $REPO
then
	(cd ../repos; ./many-files.sh -d $PARAM_D -w $PARAM_W -f $PARAM_F)
fi


enable_uc () {
	git -C $REPO config core.untrackedcache true
	git -C $REPO update-index --untracked-cache
	git -C $REPO status >/dev/null 2>&1
}

disable_uc () {
	git -C $REPO config core.untrackedcache false
	git -C $REPO update-index --no-untracked-cache
	git -C $REPO status >/dev/null 2>&1
}

start_fsm () {
	git -C $REPO fsmonitor--daemon start
	git -C $REPO fsmonitor--daemon status
	git -C $REPO config core.fsmonitor true
	git -C $REPO update-index --fsmonitor
	git -C $REPO status >/dev/null 2>&1
}

stop_fsm () {
	git -C $REPO config --unset core.fsmonitor
	git -C $REPO update-index --no-fsmonitor
	test_might_fail git -C $REPO fsmonitor--daemon stop 2>/dev/null
	git -C $REPO status >/dev/null 2>&1
}


# Ensure that FSMonitor is turned off on the borrowed repo.
#
test_expect_success "Setup borrowed repo (fsm+uc)" "
	stop_fsm &&
	disable_uc
"

# Also ensure that it starts in a known state.
#
# Because we assume that $GIT_PERF_REPEAT_COUNT > 1, we are not going to time
# the ballast checkout, since only the first invocation does any work and the
# subsequent ones just print "already on branch" and quit, so the reported
# time is not useful.
#
# Create a temp branch and do all work relative to it so that we don't
# accidentially alter the real ballast branch.
#
test_expect_success "Setup borrowed repo (temp ballast branch)" "
	test_might_fail git -C $REPO checkout $BALLAST_BR &&
	test_might_fail git -C $REPO reset --hard &&
	git -C $REPO clean -d -f &&
	test_might_fail git -C $REPO branch -D $TMP_BR &&
	git -C $REPO branch $TMP_BR $BALLAST_BR &&
	git -C $REPO checkout $TMP_BR
"


echo Data >data.txt

# NEEDSWORK: We assume that $GIT_PERF_REPEAT_COUNT > 1.  With
# FSMonitor enabled, we can get a skewed view of status times, since
# the index MAY (or may not) be updated after the first invocation
# which will update the FSMonitor Token, so the subsequent invocations
# may get a smaller response from the daemon.
#
do_status () {
	msg=$1

	test_perf "$msg" "
		git -C $REPO status >/dev/null 2>&1
	"
}

do_matrix () {
	uc=$1
	fsm=$2

	t="[uc $uc][fsm $fsm]"
	MATRIX_BR="$TMP_BR-$uc-$fsm"

	test_expect_success "$t Setup matrix branch" "
		git -C $REPO clean -d -f &&
		git -C $REPO checkout $TMP_BR &&
		test_might_fail git -C $REPO branch -D $MATRIX_BR &&
		git -C $REPO branch $MATRIX_BR $TMP_BR &&
		git -C $REPO checkout $MATRIX_BR
	"

	if test $uc = true
	then
		enable_uc
	else
		disable_uc
	fi

	if test $fsm = true
	then
		start_fsm
	else
		stop_fsm
	fi

	do_status "$t status after checkout"

	# Modify many files in the matrix branch.
	# Stage them.
	# Commit them.
	# Rollback.
	#
	test_expect_success "$t modify tracked files" "
		find $REPO -name file1 -exec cp data.txt {} \\;
	"

	do_status "$t status after big change"

	# Don't bother timing the "add" because _REPEAT_COUNT
	# issue described above.
	#
	test_expect_success "$t add all" "
		git -C $REPO add -A
	"

	do_status "$t status after add all"

	test_expect_success "$t add dot" "
		git -C $REPO add .
	"

	do_status "$t status after add dot"

	test_expect_success "$t commit staged" "
		git -C $REPO commit -a -m data
	"

	do_status "$t status after commit"

	test_expect_success "$t reset HEAD~1 hard" "
		git -C $REPO reset --hard HEAD~1 >/dev/null 2>&1
	"

	do_status "$t status after reset hard"

	# Create some untracked files.
	#
	test_expect_success "$t create untracked files" "
		cp -R $REPO/ballast/dir1 $REPO/ballast/xxx1
	"

	do_status "$t status after create untracked files"

	# Remove the new untracked files.
	#
	test_expect_success "$t clean -df" "
		git -C $REPO clean -d -f
	"

	do_status "$t status after clean"

	if test $fsm = true
	then
		stop_fsm
	fi
}

# Begin testing each case in the matrix that we care about.
#
uc_values="false"
test_have_prereq UNTRACKED_CACHE && uc_values="false true"

fsm_values="false true"

for uc_val in $uc_values
do
	for fsm_val in $fsm_values
	do
		do_matrix $uc_val $fsm_val
	done
done

cleanup () {
	uc=$1
	fsm=$2

	MATRIX_BR="$TMP_BR-$uc-$fsm"

	test_might_fail git -C $REPO branch -D $MATRIX_BR
}


# We're borrowing this repo.  We should leave it in a clean state.
#
test_expect_success "Cleanup temp and matrix branches" "
	git -C $REPO clean -d -f &&
	test_might_fail git -C $REPO checkout $BALLAST_BR &&
	test_might_fail git -C $REPO branch -D $TMP_BR &&
	for uc_val in $uc_values
	do
		for fsm_val in $fsm_values
		do
			cleanup $uc_val $fsm_val || return 1
		done
	done
"

test_done
