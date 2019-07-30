#!/bin/sh

test_description='add -i basic tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

if ! test_have_prereq PERL
then
	skip_all='skipping add -i tests, perl not available'
	test_done
fi

diff_cmp () {
	for x
	do
		sed  -e '/^index/s/[0-9a-f]*[1-9a-f][0-9a-f]*\.\./1234567../' \
		     -e '/^index/s/\.\.[0-9a-f]*[1-9a-f][0-9a-f]*/..9abcdef/' \
		     -e '/^index/s/ 00*\.\./ 0000000../' \
		     -e '/^index/s/\.\.00*$/..0000000/' \
		     -e '/^index/s/\.\.00* /..0000000 /' \
		     "$x" >"$x.filtered"
	done
	test_cmp "$1.filtered" "$2.filtered"
}

test_expect_success 'setup (initial)' '
	echo content >file &&
	git add file &&
	echo more >>file &&
	echo lines >>file
'
test_expect_success 'status works (initial)' '
	git add -i </dev/null >output &&
	grep "+1/-0 *+2/-0 file" output
'

test_expect_success 'setup expected' '
	cat >expected <<-\EOF
	new file mode 100644
	index 0000000..d95f3ad
	--- /dev/null
	+++ b/file
	@@ -0,0 +1 @@
	+content
	EOF
'

test_expect_success 'diff works (initial)' '
	test_write_lines d 1 | git add -i >output &&
	sed -ne "/new file/,/content/p" <output >diff &&
	diff_cmp expected diff
'
test_expect_success 'revert works (initial)' '
	git add file &&
	test_write_lines r 1 | git add -i &&
	git ls-files >output &&
	! grep . output
'

test_expect_success 'setup (commit)' '
	echo baseline >file &&
	git add file &&
	git commit -m commit &&
	echo content >>file &&
	git add file &&
	echo more >>file &&
	echo lines >>file
'
test_expect_success 'status works (commit)' '
	git add -i </dev/null >output &&
	grep "+1/-0 *+2/-0 file" output
'

test_expect_success 'setup expected' '
	cat >expected <<-\EOF
	index 180b47c..b6f2c08 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baseline
	+content
	EOF
'

test_expect_success 'diff works (commit)' '
	test_write_lines d 1 | git add -i >output &&
	sed -ne "/^index/,/content/p" <output >diff &&
	diff_cmp expected diff
'
test_expect_success 'revert works (commit)' '
	git add file &&
	test_write_lines r 1 | git add -i &&
	git add -i </dev/null >output &&
	grep "unchanged *+3/-0 file" output
'


test_expect_success 'setup expected' '
	cat >expected <<-\EOF
	EOF
'

test_expect_success 'dummy edit works' '
	test_set_editor : &&
	test_write_lines e a | git add -p &&
	git diff > diff &&
	diff_cmp expected diff
'

test_expect_success 'setup patch' '
	cat >patch <<-\EOF
	@@ -1,1 +1,4 @@
	 this
	+patch
	-does not
	 apply
	EOF
'

test_expect_success 'setup fake editor' '
	write_script "fake_editor.sh" <<-\EOF &&
	mv -f "$1" oldpatch &&
	mv -f patch "$1"
	EOF
	test_set_editor "$(pwd)/fake_editor.sh"
'

test_expect_success 'bad edit rejected' '
	git reset &&
	test_write_lines e n d | git add -p >output &&
	grep "hunk does not apply" output
'

test_expect_success 'setup patch' '
	cat >patch <<-\EOF
	this patch
	is garbage
	EOF
'

test_expect_success 'garbage edit rejected' '
	git reset &&
	test_write_lines e n d | git add -p >output &&
	grep "hunk does not apply" output
'

test_expect_success 'setup patch' '
	cat >patch <<-\EOF
	@@ -1,0 +1,0 @@
	 baseline
	+content
	+newcontent
	+lines
	EOF
'

test_expect_success 'setup expected' '
	cat >expected <<-\EOF
	diff --git a/file b/file
	index b5dd6c9..f910ae9 100644
	--- a/file
	+++ b/file
	@@ -1,4 +1,4 @@
	 baseline
	 content
	-newcontent
	+more
	 lines
	EOF
'

test_expect_success 'real edit works' '
	test_write_lines e n d | git add -p &&
	git diff >output &&
	diff_cmp expected output
'

test_expect_success 'setup file' '
	test_write_lines a "" b "" c >file &&
	git add file &&
	test_write_lines a "" d "" c >file
'

test_expect_success 'setup patch' '
	SP=" " &&
	NULL="" &&
	cat >patch <<-EOF
	@@ -1,4 +1,4 @@
	 a
	$NULL
	-b
	+f
	$SP
	c
	EOF
'

test_expect_success 'setup expected' '
	cat >expected <<-EOF
	diff --git a/file b/file
	index b5dd6c9..f910ae9 100644
	--- a/file
	+++ b/file
	@@ -1,5 +1,5 @@
	 a
	$SP
	-f
	+d
	$SP
	 c
	EOF
'

test_expect_success 'edit can strip spaces from empty context lines' '
	test_write_lines e n q | git add -p 2>error &&
	test_must_be_empty error &&
	git diff >output &&
	diff_cmp expected output
'

test_expect_success 'skip files similarly as commit -a' '
	git reset &&
	echo file >.gitignore &&
	echo changed >file &&
	echo y | git add -p file &&
	git diff >output &&
	git reset &&
	git commit -am commit &&
	git diff >expected &&
	diff_cmp expected output &&
	git reset --hard HEAD^
'
rm -f .gitignore

test_expect_success FILEMODE 'patch does not affect mode' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "n\\ny\\n" | git add -p &&
	git show :file | grep content &&
	git diff file | grep "new mode"
'

test_expect_success FILEMODE 'stage mode but not hunk' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "y\\nn\\n" | git add -p &&
	git diff --cached file | grep "new mode" &&
	git diff          file | grep "+content"
'


test_expect_success FILEMODE 'stage mode and hunk' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "y\\ny\\n" | git add -p &&
	git diff --cached file | grep "new mode" &&
	git diff --cached file | grep "+content" &&
	test -z "$(git diff file)"
'

# end of tests disabled when filemode is not usable

test_expect_success 'setup again' '
	git reset --hard &&
	test_chmod +x file &&
	echo content >>file
'

# Write the patch file with a new line at the top and bottom
test_expect_success 'setup patch' '
	cat >patch <<-\EOF
	index 180b47c..b6f2c08 100644
	--- a/file
	+++ b/file
	@@ -1,2 +1,4 @@
	+firstline
	 baseline
	 content
	+lastline
	\ No newline at end of file
	EOF
'

# Expected output, diff is similar to the patch but w/ diff at the top
test_expect_success 'setup expected' '
	echo diff --git a/file b/file >expected &&
	cat patch |sed "/^index/s/ 100644/ 100755/" >>expected &&
	cat >expected-output <<-\EOF
	--- a/file
	+++ b/file
	@@ -1,2 +1,4 @@
	+firstline
	 baseline
	 content
	+lastline
	\ No newline at end of file
	@@ -1,2 +1,3 @@
	+firstline
	 baseline
	 content
	@@ -1,2 +2,3 @@
	 baseline
	 content
	+lastline
	\ No newline at end of file
	EOF
'

# Test splitting the first patch, then adding both
test_expect_success C_LOCALE_OUTPUT 'add first line works' '
	git commit -am "clear local changes" &&
	git apply patch &&
	printf "%s\n" s y y | git add -p file 2>error |
		sed -n -e "s/^Stage this hunk[^@]*\(@@ .*\)/\1/" \
		       -e "/^[-+@ \\\\]"/p  >output &&
	test_must_be_empty error &&
	git diff --cached >diff &&
	diff_cmp expected diff &&
	test_cmp expected-output output
'

test_expect_success 'setup expected' '
	cat >expected <<-\EOF
	diff --git a/non-empty b/non-empty
	deleted file mode 100644
	index d95f3ad..0000000
	--- a/non-empty
	+++ /dev/null
	@@ -1 +0,0 @@
	-content
	EOF
'

test_expect_success 'deleting a non-empty file' '
	git reset --hard &&
	echo content >non-empty &&
	git add non-empty &&
	git commit -m non-empty &&
	rm non-empty &&
	echo y | git add -p non-empty &&
	git diff --cached >diff &&
	diff_cmp expected diff
'

test_expect_success 'setup expected' '
	cat >expected <<-\EOF
	diff --git a/empty b/empty
	deleted file mode 100644
	index e69de29..0000000
	EOF
'

test_expect_success 'deleting an empty file' '
	git reset --hard &&
	> empty &&
	git add empty &&
	git commit -m empty &&
	rm empty &&
	echo y | git add -p empty &&
	git diff --cached >diff &&
	diff_cmp expected diff
'

test_expect_success 'split hunk setup' '
	git reset --hard &&
	test_write_lines 10 20 30 40 50 60 >test &&
	git add test &&
	test_tick &&
	git commit -m test &&

	test_write_lines 10 15 20 21 22 23 24 30 40 50 60 >test
'

test_expect_success 'split hunk "add -p (edit)"' '
	# Split, say Edit and do nothing.  Then:
	#
	# 1. Broken version results in a patch that does not apply and
	# only takes [y/n] (edit again) so the first q is discarded
	# and then n attempts to discard the edit. Repeat q enough
	# times to get out.
	#
	# 2. Correct version applies the (not)edited version, and asks
	#    about the next hunk, against which we say q and program
	#    exits.
	printf "%s\n" s e     q n q q |
	EDITOR=: git add -p &&
	git diff >actual &&
	! grep "^+15" actual
'

test_expect_failure 'split hunk "add -p (no, yes, edit)"' '
	test_write_lines 5 10 20 21 30 31 40 50 60 >test &&
	git reset &&
	# test sequence is s(plit), n(o), y(es), e(dit)
	# q n q q is there to make sure we exit at the end.
	printf "%s\n" s n y e   q n q q |
	EDITOR=: git add -p 2>error &&
	test_must_be_empty error &&
	git diff >actual &&
	! grep "^+31" actual
'

test_expect_success 'patch mode ignores unmerged entries' '
	git reset --hard &&
	test_commit conflict &&
	test_commit non-conflict &&
	git checkout -b side &&
	test_commit side conflict.t &&
	git checkout master &&
	test_commit master conflict.t &&
	test_must_fail git merge side &&
	echo changed >non-conflict.t &&
	echo y | git add -p >output &&
	! grep a/conflict.t output &&
	cat >expected <<-\EOF &&
	* Unmerged path conflict.t
	diff --git a/non-conflict.t b/non-conflict.t
	index f766221..5ea2ed4 100644
	--- a/non-conflict.t
	+++ b/non-conflict.t
	@@ -1 +1 @@
	-non-conflict
	+changed
	EOF
	git diff --cached >diff &&
	diff_cmp expected diff
'

test_expect_success TTY 'diffs can be colorized' '
	git reset --hard &&

	echo content >test &&
	printf y | test_terminal git add -p >output 2>&1 &&

	# We do not want to depend on the exact coloring scheme
	# git uses for diffs, so just check that we saw some kind of color.
	grep "$(printf "\\033")" output
'

test_expect_success TTY 'diffFilter filters diff' '
	git reset --hard &&

	echo content >test &&
	test_config interactive.diffFilter "sed s/^/foo:/" &&
	printf y | test_terminal git add -p >output 2>&1 &&

	# avoid depending on the exact coloring or content of the prompts,
	# and just make sure we saw our diff prefixed
	grep foo:.*content output
'

test_expect_success TTY 'detect bogus diffFilter output' '
	git reset --hard &&

	echo content >test &&
	test_config interactive.diffFilter "echo too-short" &&
	printf y | test_must_fail test_terminal git add -p
'

test_expect_success 'patch-mode via -i prompts for files' '
	git reset --hard &&

	echo one >file &&
	echo two >test &&
	git add -i <<-\EOF &&
	patch
	test

	y
	quit
	EOF

	echo test >expect &&
	git diff --cached --name-only >actual &&
	diff_cmp expect actual
'

test_expect_success 'add -p handles globs' '
	git reset --hard &&

	mkdir -p subdir &&
	echo base >one.c &&
	echo base >subdir/two.c &&
	git add "*.c" &&
	git commit -m base &&

	echo change >one.c &&
	echo change >subdir/two.c &&
	git add -p "*.c" <<-\EOF &&
	y
	y
	EOF

	cat >expect <<-\EOF &&
	one.c
	subdir/two.c
	EOF
	git diff --cached --name-only >actual &&
	test_cmp expect actual
'

test_expect_success 'add -p handles relative paths' '
	git reset --hard &&

	echo base >relpath.c &&
	git add "*.c" &&
	git commit -m relpath &&

	echo change >relpath.c &&
	mkdir -p subdir &&
	git -C subdir add -p .. 2>error <<-\EOF &&
	y
	EOF

	test_must_be_empty error &&

	cat >expect <<-\EOF &&
	relpath.c
	EOF
	git diff --cached --name-only >actual &&
	test_cmp expect actual
'

test_expect_success 'add -p does not expand argument lists' '
	git reset --hard &&

	echo content >not-changed &&
	git add not-changed &&
	git commit -m "add not-changed file" &&

	echo change >file &&
	GIT_TRACE=$(pwd)/trace.out git add -p . <<-\EOF &&
	y
	EOF

	# we know that "file" must be mentioned since we actually
	# update it, but we want to be sure that our "." pathspec
	# was not expanded into the argument list of any command.
	# So look only for "not-changed".
	! grep -E "^trace: (built-in|exec|run_command): .*not-changed" trace.out
'

test_expect_success 'hunk-editing handles custom comment char' '
	git reset --hard &&
	echo change >>file &&
	test_config core.commentChar "\$" &&
	echo e | GIT_EDITOR=true git add -p &&
	git diff --exit-code
'

test_expect_success 'add -p works even with color.ui=always' '
	git reset --hard &&
	echo change >>file &&
	test_config color.ui always &&
	echo y | git add -p &&
	echo file >expect &&
	git diff --cached --name-only >actual &&
	test_cmp expect actual
'

test_expect_success 'setup different kinds of dirty submodules' '
	test_create_repo for-submodules &&
	(
		cd for-submodules &&
		test_commit initial &&
		test_create_repo dirty-head &&
		(
			cd dirty-head &&
			test_commit initial
		) &&
		cp -R dirty-head dirty-otherwise &&
		cp -R dirty-head dirty-both-ways &&
		git add dirty-head &&
		git add dirty-otherwise dirty-both-ways &&
		git commit -m initial &&

		cd dirty-head &&
		test_commit updated &&
		cd ../dirty-both-ways &&
		test_commit updated &&
		echo dirty >>initial &&
		: >untracked &&
		cd ../dirty-otherwise &&
		echo dirty >>initial &&
		: >untracked
	) &&
	git -C for-submodules diff-files --name-only >actual &&
	cat >expected <<-\EOF &&
	dirty-both-ways
	dirty-head
	dirty-otherwise
	EOF
	test_cmp expected actual &&
	git -C for-submodules diff-files --name-only --ignore-submodules=dirty >actual &&
	cat >expected <<-\EOF &&
	dirty-both-ways
	dirty-head
	EOF
	test_cmp expected actual
'

test_expect_success 'status ignores dirty submodules (except HEAD)' '
	git -C for-submodules add -i </dev/null >output &&
	grep dirty-head output &&
	grep dirty-both-ways output &&
	! grep dirty-otherwise output
'

test_expect_success 'set up pathological context' '
	git reset --hard &&
	test_write_lines a a a a a a a a a a a >a &&
	git add a &&
	git commit -m a &&
	test_write_lines c b a a a a a a a b a a a a >a &&
	test_write_lines     a a a a a a a b a a a a >expected-1 &&
	test_write_lines   b a a a a a a a b a a a a >expected-2 &&
	# check editing can cope with missing header and deleted context lines
	# as well as changes to other lines
	test_write_lines +b " a" >patch
'

test_expect_success 'add -p works with pathological context lines' '
	git reset &&
	printf "%s\n" n y |
	git add -p &&
	git cat-file blob :a >actual &&
	test_cmp expected-1 actual
'

test_expect_success 'add -p patch editing works with pathological context lines' '
	git reset &&
	# n q q below is in case edit fails
	printf "%s\n" e y    n q q |
	git add -p &&
	git cat-file blob :a >actual &&
	test_cmp expected-2 actual
'

test_expect_success 'checkout -p works with pathological context lines' '
	test_write_lines a a a a a a >a &&
	git add a &&
	test_write_lines a b a b a b a b a b a > a&&
	test_write_lines s n n y q | git checkout -p &&
	test_write_lines a b a b a a b a b a >expect &&
	test_cmp expect a
'
test_done
