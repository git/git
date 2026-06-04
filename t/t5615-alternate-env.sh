#!/bin/sh

test_description='handling of alternates in environment variables'

. ./test-lib.sh

check_obj () {
	alt=$1; shift
	while read obj expect
	do
		echo "$obj" >&5 &&
		echo "$obj $expect" >&6
	done 5>input 6>expect &&
	GIT_ALTERNATE_OBJECT_DIRECTORIES=$alt \
		git "$@" cat-file --batch-check='%(objectname) %(objecttype)' \
		<input >actual &&
	test_cmp expect actual
}

test_expect_success 'create alternate repositories' '
	git init --bare one.git &&
	one=$(echo one | git -C one.git hash-object -w --stdin) &&
	git init --bare two.git &&
	two=$(echo two | git -C two.git hash-object -w --stdin)
'

test_expect_success 'objects inaccessible without alternates' '
	check_obj "" <<-EOF
	$one missing
	$two missing
	EOF
'

test_expect_success 'access alternate via absolute path' '
	check_obj "$PWD/one.git/objects" <<-EOF
	$one blob
	$two missing
	EOF
'

test_expect_success 'access multiple alternates' '
	check_obj "$PWD/one.git/objects:$PWD/two.git/objects" <<-EOF
	$one blob
	$two blob
	EOF
'

# bare paths are relative from $GIT_DIR
test_expect_success 'access alternate via relative path (bare)' '
	git init --bare bare.git &&
	check_obj "../one.git/objects" -C bare.git <<-EOF
	$one blob
	EOF
'

# non-bare paths are relative to top of worktree
test_expect_success 'access alternate via relative path (worktree)' '
	git init worktree &&
	check_obj "../one.git/objects" -C worktree <<-EOF
	$one blob
	EOF
'

# path is computed after moving to top-level of worktree
test_expect_success 'access alternate via relative path (subdir)' '
	mkdir subdir &&
	check_obj "one.git/objects" -C subdir <<-EOF
	$one blob
	EOF
'

# set variables outside test to avoid quote insanity; the \057 is '/',
# which doesn't need quoting, but just confirms that de-quoting
# is working.
quoted='"one.git\057objects"'
unquoted='two.git/objects'
test_expect_success 'mix of quoted and unquoted alternates' '
	check_obj "$quoted:$unquoted" <<-EOF
	$one blob
	$two blob
	EOF
'

test_expect_success !MINGW 'broken quoting falls back to interpreting raw' '
	mv one.git \"one.git &&
	check_obj \"one.git/objects <<-EOF
	$one blob
	EOF
'

test_done
