#!/bin/sh

test_description='add -i basic tests'
. ./test-lib.sh

if ! test_have_prereq PERL
then
	skip_all='skipping add -i tests, perl not available'
	test_done
fi

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
cat >expected <<EOF
new file mode 100644
index 0000000..d95f3ad
--- /dev/null
+++ b/file
@@ -0,0 +1 @@
+content
EOF
'

test_expect_success 'diff works (initial)' '
	(echo d; echo 1) | git add -i >output &&
	sed -ne "/new file/,/content/p" <output >diff &&
	test_cmp expected diff
'
test_expect_success 'revert works (initial)' '
	git add file &&
	(echo r; echo 1) | git add -i &&
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
cat >expected <<EOF
index 180b47c..b6f2c08 100644
--- a/file
+++ b/file
@@ -1 +1,2 @@
 baseline
+content
EOF
'

test_expect_success 'diff works (commit)' '
	(echo d; echo 1) | git add -i >output &&
	sed -ne "/^index/,/content/p" <output >diff &&
	test_cmp expected diff
'
test_expect_success 'revert works (commit)' '
	git add file &&
	(echo r; echo 1) | git add -i &&
	git add -i </dev/null >output &&
	grep "unchanged *+3/-0 file" output
'


test_expect_success 'setup expected' '
cat >expected <<EOF
EOF
'

test_expect_success 'setup fake editor' '
	>fake_editor.sh &&
	chmod a+x fake_editor.sh &&
	test_set_editor "$(pwd)/fake_editor.sh"
'

test_expect_success 'dummy edit works' '
	(echo e; echo a) | git add -p &&
	git diff > diff &&
	test_cmp expected diff
'

test_expect_success 'setup patch' '
cat >patch <<EOF
@@ -1,1 +1,4 @@
 this
+patch
-does not
 apply
EOF
'

test_expect_success 'setup fake editor' '
	echo "#!$SHELL_PATH" >fake_editor.sh &&
	cat >>fake_editor.sh <<\EOF &&
mv -f "$1" oldpatch &&
mv -f patch "$1"
EOF
	chmod a+x fake_editor.sh &&
	test_set_editor "$(pwd)/fake_editor.sh"
'

test_expect_success 'bad edit rejected' '
	git reset &&
	(echo e; echo n; echo d) | git add -p >output &&
	grep "hunk does not apply" output
'

test_expect_success 'setup patch' '
cat >patch <<EOF
this patch
is garbage
EOF
'

test_expect_success 'garbage edit rejected' '
	git reset &&
	(echo e; echo n; echo d) | git add -p >output &&
	grep "hunk does not apply" output
'

test_expect_success 'setup patch' '
cat >patch <<EOF
@@ -1,0 +1,0 @@
 baseline
+content
+newcontent
+lines
EOF
'

test_expect_success 'setup expected' '
cat >expected <<EOF
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
	(echo e; echo n; echo d) | git add -p &&
	git diff >output &&
	test_cmp expected output
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
	test_cmp expected output &&
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
cat >patch <<EOF
index 180b47c..b6f2c08 100644
--- a/file
+++ b/file
@@ -1,2 +1,4 @@
+firstline
 baseline
 content
+lastline
EOF
'

# Expected output, similar to the patch but w/ diff at the top
test_expect_success 'setup expected' '
cat >expected <<EOF
diff --git a/file b/file
index b6f2c08..61b9053 100755
--- a/file
+++ b/file
@@ -1,2 +1,4 @@
+firstline
 baseline
 content
+lastline
EOF
'

# Test splitting the first patch, then adding both
test_expect_success 'add first line works' '
	git commit -am "clear local changes" &&
	git apply patch &&
	(echo s; echo y; echo y) | git add -p file &&
	git diff --cached > diff &&
	test_cmp expected diff
'

test_expect_success 'setup expected' '
cat >expected <<EOF
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
	test_cmp expected diff
'

test_expect_success 'setup expected' '
cat >expected <<EOF
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
	test_cmp expected diff
'

test_expect_success 'split hunk setup' '
	git reset --hard &&
	for i in 10 20 30 40 50 60
	do
		echo $i
	done >test &&
	git add test &&
	test_tick &&
	git commit -m test &&

	for i in 10 15 20 21 22 23 24 30 40 50 60
	do
		echo $i
	done >test
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
	cat >test <<-\EOF &&
	5
	10
	20
	21
	30
	31
	40
	50
	60
	EOF
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
	test_cmp expected diff
'

test_expect_success 'diffs can be colorized' '
	git reset --hard &&

	# force color even though the test script has no terminal
	test_config color.ui always &&

	echo content >test &&
	printf y | git add -p >output 2>&1 &&

	# We do not want to depend on the exact coloring scheme
	# git uses for diffs, so just check that we saw some kind of color.
	grep "$(printf "\\033")" output
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
	test_cmp expect actual
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
	! grep not-changed trace.out
'

test_expect_success 'hunk-editing handles custom comment char' '
	git reset --hard &&
	echo change >>file &&
	test_config core.commentChar "\$" &&
	echo e | GIT_EDITOR=true git add -p &&
	git diff --exit-code
'

test_expect_success EXPENSIVE 'add -i with a lot of files' '
	git reset --hard &&
	x160=0123456789012345678901234567890123456789 &&
	x160=$x160$x160$x160$x160 &&
	y= &&
	i=0 &&
	while test $i -le 200
	do
		name=$(printf "%s%03d" $x160 $i) &&
		echo $name >$name &&
		git add -N $name &&
		y="${y}y$LF" &&
		i=$(($i+1)) ||
		break
	done &&
	echo "$y" | git add -p -- . &&
	git diff --cached >staged &&
	test_line_count = 1407 staged &&
	git reset --hard
'

test_done
