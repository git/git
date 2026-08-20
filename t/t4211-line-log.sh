#!/bin/sh

test_description='test log -L'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup (import history)' '
	git fast-import < "$TEST_DIRECTORY"/t4211/history.export &&
	git reset --hard
'

test_expect_success 'basic command line parsing' '
	# This may fail due to "no such path a.c in commit", or
	# "-L is incompatible with pathspec", depending on the
	# order the error is checked.  Either is acceptable.
	test_must_fail git log -L1,1:a.c -- a.c &&

	# -L requires there is no pathspec
	test_must_fail git log -L1,1:b.c -- b.c 2>error &&
	test_grep "cannot be used with pathspec" error &&

	# This would fail because --follow wants a single path, but
	# we may fail due to incompatibility between -L/--follow in
	# the future.  Either is acceptable.
	test_must_fail git log -L1,1:b.c --follow &&
	test_must_fail git log --follow -L1,1:b.c &&

	# This would fail because -L wants no pathspec, but
	# we may fail due to incompatibility between -L/--follow in
	# the future.  Either is acceptable.
	test_must_fail git log --follow -L1,1:b.c -- b.c
'

canned_test_1 () {
	test_expect_$1 "$2" "
		git log $2 >actual &&
		test_cmp \"\$TEST_DIRECTORY\"/t4211/$(test_oid algo)/expect.$3 actual
	"
}

canned_test () {
	canned_test_1 success "$@"
}
canned_test_failure () {
	canned_test_1 failure "$@"
}

test_bad_opts () {
	test_expect_success "invalid args: $1" "
		test_must_fail git log $1 2>errors &&
		test_grep '$2' errors
	"
}

canned_test "-L 4,12:a.c simple" simple-f
canned_test "-L 4,+9:a.c simple" simple-f
canned_test "-L '/long f/,/^}/:a.c' simple" simple-f
canned_test "-L :f:a.c simple" simple-f-to-main

canned_test "-L '/main/,/^}/:a.c' simple" simple-main
canned_test "-L :main:a.c simple" simple-main-to-end

canned_test "-L 1,+4:a.c simple" beginning-of-file

canned_test "-L 20:a.c simple" end-of-file

canned_test "-L '/long f/',/^}/:a.c -L /main/,/^}/:a.c simple" two-ranges
canned_test "-L 24,+1:a.c simple" vanishes-early

canned_test "-M -L '/long f/,/^}/:b.c' move-support" move-support-f
canned_test "-M -L ':f:b.c' parallel-change" parallel-change-f-to-main

canned_test "-L 4,12:a.c -L :main:a.c simple" multiple
canned_test "-L 4,18:a.c -L ^:main:a.c simple" multiple-overlapping
canned_test "-L :main:a.c -L 4,18:a.c simple" multiple-overlapping
canned_test "-L 4:a.c -L 8,12:a.c simple" multiple-superset
canned_test "-L 8,12:a.c -L 4:a.c simple" multiple-superset

canned_test "-L 10,16:b.c -L 18,26:b.c main" no-assertion-error

test_bad_opts "-L" "switch.*requires a value"
test_bad_opts "-L b.c" "argument not .start,end:file"
test_bad_opts "-L 1:" "argument not .start,end:file"
test_bad_opts "-L 1:nonexistent" "There is no path"
test_bad_opts "-L 1:simple" "There is no path"
test_bad_opts "-L '/foo:b.c'" "argument not .start,end:file"
test_bad_opts "-L 1000:b.c" "has only.*lines"
test_bad_opts "-L :b.c" "argument not .start,end:file"
test_bad_opts "-L :foo:b.c" "no match"

test_expect_success '-L X (X == nlines)' '
	n=$(wc -l <b.c) &&
	git log -L $n:b.c
'

test_expect_success '-L X (X == nlines + 1)' '
	n=$(expr $(wc -l <b.c) + 1) &&
	test_must_fail git log -L $n:b.c
'

test_expect_success '-L X (X == nlines + 2)' '
	n=$(expr $(wc -l <b.c) + 2) &&
	test_must_fail git log -L $n:b.c
'

test_expect_success '-L ,Y (Y == nlines)' '
	n=$(printf "%d" $(wc -l <b.c)) &&
	git log -L ,$n:b.c
'

test_expect_success '-L ,Y (Y == nlines + 1)' '
	n=$(expr $(wc -l <b.c) + 1) &&
	git log -L ,$n:b.c
'

test_expect_success '-L ,Y (Y == nlines + 2)' '
	n=$(expr $(wc -l <b.c) + 2) &&
	git log -L ,$n:b.c
'

test_expect_success '-L with --first-parent and a merge' '
	git checkout parallel-change &&
	git log --first-parent -L 1,1:b.c
'

test_expect_success '-L with --output' '
	git checkout parallel-change &&
	git log --output=log -L :main:b.c >output &&
	test_must_be_empty output &&
	test_line_count = 75 log
'

test_expect_success 'range_set_union' '
	test_seq 500 > c.c &&
	git add c.c &&
	git commit -m "many lines" &&
	test_seq 1000 > c.c &&
	git add c.c &&
	git commit -m "modify many lines" &&
	git log $(for x in $(test_seq 200); do echo -L $((2*x)),+1:c.c || return 1; done)
'

test_expect_success '-s shows only line-log commits' '
	git log --format="commit %s" -L1,24:b.c >expect.raw &&
	grep ^commit expect.raw >expect &&
	git log --format="commit %s" -L1,24:b.c -s >actual &&
	test_cmp expect actual
'

test_expect_success '-p shows the default patch output' '
	git log -L1,24:b.c >expect &&
	git log -L1,24:b.c -p >actual &&
	test_cmp expect actual
'

test_expect_success '--raw shows mode, oid, status and path' '
	git log -L1,24:b.c --raw --format= >actual &&
	test_grep "^:100644 100644 [0-9a-f]\{7\} [0-9a-f]\{7\} M	b.c$" actual &&
	test_grep ! "^diff --git" actual &&
	test_grep ! "^@@" actual
'

test_expect_success '--name-only shows path' '
	git log -L1,24:b.c --name-only --format= >actual &&
	test_grep "^b.c$" actual &&
	test_grep ! "^diff --git" actual &&
	test_grep ! "^@@" actual
'

test_expect_success '--name-status shows status and path' '
	git log -L1,24:b.c --name-status --format= >actual &&
	test_grep "^M	b.c$" actual &&
	test_grep ! "^diff --git" actual &&
	test_grep ! "^@@" actual
'

test_expect_success '--dirstat is not supported with -L' '
	# --dirstat is not supported with -L: its default mode measures
	# whole-file change, not the tracked lines, and the
	# --dirstat=lines variant is deferred too, so both forms are
	# rejected like any other unsupported format.
	test_must_fail git log -L1,24:b.c --dirstat 2>err &&
	test_grep "does not support" err &&
	test_must_fail git log -L1,24:b.c --dirstat=lines 2>err &&
	test_grep "does not support" err
'

test_expect_success 'setup for checking fancy rename following' '
	git checkout --orphan moves-start &&
	git reset --hard &&

	printf "%s\n"    12 13 14 15      b c d e   >file-1 &&
	printf "%s\n"    22 23 24 25      B C D E   >file-2 &&
	git add file-1 file-2 &&
	test_tick &&
	git commit -m "Add file-1 and file-2" &&
	oid_add_f1_f2=$(git rev-parse --short HEAD) &&

	git checkout -b moves-main &&
	printf "%s\n" 11 12 13 14 15      b c d e   >file-1 &&
	git commit -a -m "Modify file-1 on main" &&
	oid_mod_f1_main=$(git rev-parse --short HEAD) &&

	printf "%s\n" 21 22 23 24 25      B C D E   >file-2 &&
	git commit -a -m "Modify file-2 on main #1" &&
	oid_mod_f2_main_1=$(git rev-parse --short HEAD) &&

	git mv file-1 renamed-1 &&
	git commit -m "Rename file-1 to renamed-1 on main" &&

	printf "%s\n" 11 12 13 14 15      b c d e f >renamed-1 &&
	git commit -a -m "Modify renamed-1 on main" &&
	oid_mod_r1_main=$(git rev-parse --short HEAD) &&

	printf "%s\n" 21 22 23 24 25      B C D E F >file-2 &&
	git commit -a -m "Modify file-2 on main #2" &&
	oid_mod_f2_main_2=$(git rev-parse --short HEAD) &&

	git checkout -b moves-side moves-start &&
	printf "%s\n"    12 13 14 15 16   b c d e   >file-1 &&
	git commit -a -m "Modify file-1 on side #1" &&
	oid_mod_f1_side_1=$(git rev-parse --short HEAD) &&

	printf "%s\n"    22 23 24 25 26   B C D E   >file-2 &&
	git commit -a -m "Modify file-2 on side" &&
	oid_mod_f2_side=$(git rev-parse --short HEAD) &&

	git mv file-2 renamed-2 &&
	git commit -m "Rename file-2 to renamed-2 on side" &&

	printf "%s\n"    12 13 14 15 16 a b c d e   >file-1 &&
	git commit -a -m "Modify file-1 on side #2" &&
	oid_mod_f1_side_2=$(git rev-parse --short HEAD) &&

	printf "%s\n"    22 23 24 25 26 A B C D E   >renamed-2 &&
	git commit -a -m "Modify renamed-2 on side" &&
	oid_mod_r2_side=$(git rev-parse --short HEAD) &&

	git checkout moves-main &&
	git merge moves-side &&
	oid_merge=$(git rev-parse --short HEAD)
'

test_expect_success 'fancy rename following #1' '
	cat >expect <<-EOF &&
	$oid_merge Merge branch '\''moves-side'\'' into moves-main
	$oid_mod_f1_side_2 Modify file-1 on side #2
	$oid_mod_f1_side_1 Modify file-1 on side #1
	$oid_mod_r1_main Modify renamed-1 on main
	$oid_mod_f1_main Modify file-1 on main
	$oid_add_f1_f2 Add file-1 and file-2
	EOF
	git log -L1:renamed-1 --oneline --no-patch >actual &&
	test_cmp expect actual
'

test_expect_success 'fancy rename following #2' '
	cat >expect <<-EOF &&
	$oid_merge Merge branch '\''moves-side'\'' into moves-main
	$oid_mod_r2_side Modify renamed-2 on side
	$oid_mod_f2_side Modify file-2 on side
	$oid_mod_f2_main_2 Modify file-2 on main #2
	$oid_mod_f2_main_1 Modify file-2 on main #1
	$oid_add_f1_f2 Add file-1 and file-2
	EOF
	git log -L1:renamed-2 --oneline --no-patch >actual &&
	test_cmp expect actual
'

# Create the following linear history, where each commit does what its
# subject line promises:
#
#   * 66c6410 Modify func2() in file.c
#   * 50834e5 Modify other-file
#   * fe5851c Modify func1() in file.c
#   * 8c7c7dd Add other-file
#   * d5f4417 Add func1() and func2() in file.c
test_expect_success 'setup for checking line-log and parent oids' '
	git checkout --orphan parent-oids &&
	git reset --hard &&

	cat >file.c <<-\EOF &&
	int func1()
	{
	    return F1;
	}

	int func2()
	{
	    return F2;
	}
	EOF
	git add file.c &&
	test_tick &&
	first_tick=$test_tick &&
	git commit -m "Add func1() and func2() in file.c" &&

	echo 1 >other-file &&
	git add other-file &&
	test_tick &&
	git commit -m "Add other-file" &&

	sed -e "s/F1/F1 + 1/" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "Modify func1() in file.c" &&

	echo 2 >other-file &&
	git commit -a -m "Modify other-file" &&

	sed -e "s/F2/F2 + 2/" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "Modify func2() in file.c" &&

	head_oid=$(git rev-parse --short HEAD) &&
	prev_oid=$(git rev-parse --short HEAD^) &&
	root_oid=$(git rev-parse --short HEAD~4)
'

# Parent oid should be from immediate parent.
test_expect_success 'parent oids without parent rewriting' '
	cat >expect <<-EOF &&
	$head_oid $prev_oid Modify func2() in file.c
	$root_oid  Add func1() and func2() in file.c
	EOF
	git log --format="%h %p %s" --no-patch -L:func2:file.c >actual &&
	test_cmp expect actual
'

# Parent oid should be from the most recent ancestor touching func2(),
# i.e. in this case from the root commit.
test_expect_success 'parent oids with parent rewriting' '
	cat >expect <<-EOF &&
	$head_oid $root_oid Modify func2() in file.c
	$root_oid  Add func1() and func2() in file.c
	EOF
	git log --format="%h %p %s" --no-patch -L:func2:file.c --parents >actual &&
	test_cmp expect actual
'

test_expect_success 'line-log with --before' '
	echo $root_oid >expect &&
	git log --format=%h --no-patch -L:func2:file.c --before=$first_tick >actual &&
	test_cmp expect actual
'

test_expect_success 'setup tests for zero-width regular expressions' '
	cat >expect <<-EOF
	Modify func1() in file.c
	Add func1() and func2() in file.c
	EOF
'

test_expect_success 'zero-width regex $ matches any function name' '
	git log --format="%s" --no-patch "-L:$:file.c" >actual &&
	test_cmp expect actual
'

test_expect_success 'zero-width regex ^ matches any function name' '
	git log --format="%s" --no-patch "-L:^:file.c" >actual &&
	test_cmp expect actual
'

test_expect_success 'zero-width regex .* matches any function name' '
	git log --format="%s" --no-patch "-L:.*:file.c" >actual &&
	test_cmp expect actual
'

test_expect_success 'setup for diff pipeline tests' '
	git checkout parent-oids &&

	head_blob_old=$(git rev-parse --short HEAD^:file.c) &&
	head_blob_new=$(git rev-parse --short HEAD:file.c) &&
	root_blob=$(git rev-parse --short HEAD~4:file.c) &&
	null_blob=$(test_oid zero | cut -c1-7) &&
	head_blob_old_full=$(git rev-parse HEAD^:file.c) &&
	head_blob_new_full=$(git rev-parse HEAD:file.c) &&
	root_blob_full=$(git rev-parse HEAD~4:file.c) &&
	null_blob_full=$(test_oid zero)
'

test_expect_success '-L diff output includes index and new file mode' '
	git log -L:func2:file.c --format= >actual &&

	# Output should contain index headers (not present in old code path)
	test_grep "^index $head_blob_old\.\.$head_blob_new 100644" actual &&

	# Root commit should show new file mode and null index
	test_grep "^new file mode 100644" actual &&
	test_grep "^index $null_blob\.\.$root_blob$" actual &&

	# Hunk headers should include funcname context
	test_grep "^@@ .* @@ int func1()" actual
'

test_expect_success '-L with --word-diff' '
	cat >expect <<-\EOF &&
	diff --git a/file.c b/file.c
	--- a/file.c
	+++ b/file.c
	@@ -6,4 +6,4 @@ int func1()
	int func2()
	{
	    return [-F2;-]{+F2 + 2;+}
	}
	diff --git a/file.c b/file.c
	new file mode 100644
	--- /dev/null
	+++ b/file.c
	@@ -0,0 +6,4 @@
	{+int func2()+}
	{+{+}
	{+    return F2;+}
	{+}+}
	EOF
	git log -L:func2:file.c --word-diff --format= >actual &&
	grep -v "^index " actual >actual.filtered &&
	grep -v "^index " expect >expect.filtered &&
	test_cmp expect.filtered actual.filtered
'

test_expect_success '-L with --no-prefix' '
	git log -L:func2:file.c --no-prefix --format= >actual &&
	test_grep "^diff --git file.c file.c" actual &&
	test_grep "^--- file.c" actual &&
	test_grep ! "^--- a/" actual
'

test_expect_success '-L with --full-index' '
	git log -L:func2:file.c --full-index --format= >actual &&
	test_grep "^index $head_blob_old_full\.\.$head_blob_new_full 100644" actual &&
	test_grep "^index $null_blob_full\.\.$root_blob_full$" actual
'

test_expect_success 'setup -L with whitespace change' '
	git checkout -b ws-change parent-oids &&
	sed "s/    return F2 + 2;/	return F2 + 2;/" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "Whitespace change in func2()"
'

test_expect_success '-L with --ignore-all-space suppresses whitespace-only diff' '
	git log -L:func2:file.c --format= >without_w &&
	git log -L:func2:file.c --format= -w >with_w &&

	# Without -w: three commits produce diffs (whitespace, modify, root)
	test $(grep -c "^diff --git" without_w) = 3 &&

	# With -w: whitespace-only commit produces no hunk, so only two diffs
	test $(grep -c "^diff --git" with_w) = 2
'

test_expect_success 'show line-log with graph' '
	git checkout parent-oids &&
	head_blob_old=$(git rev-parse --short HEAD^:file.c) &&
	head_blob_new=$(git rev-parse --short HEAD:file.c) &&
	root_blob=$(git rev-parse --short HEAD~4:file.c) &&
	null_blob=$(test_oid zero | cut -c1-7) &&
	qz_to_tab_space >expect <<-EOF &&
	* $head_oid Modify func2() in file.c
	| diff --git a/file.c b/file.c
	| index $head_blob_old..$head_blob_new 100644
	| --- a/file.c
	| +++ b/file.c
	| @@ -6,4 +6,4 @@ int func1()
	|  int func2()
	|  {
	| -    return F2;
	| +    return F2 + 2;
	|  }
	* $root_oid Add func1() and func2() in file.c
	  diff --git a/file.c b/file.c
	  new file mode 100644
	  index $null_blob..$root_blob
	  --- /dev/null
	  +++ b/file.c
	  @@ -0,0 +6,4 @@
	  +int func2()
	  +{
	  +    return F2;
	  +}
	EOF
	git log --graph --oneline -L:func2:file.c >actual &&
	test_cmp expect actual
'

test_expect_success 'setup for -L with -G/-S/--find-object and a merge with rename' '
	git checkout --orphan pickaxe-rename &&
	git reset --hard &&

	echo content >file &&
	git add file &&
	git commit -m "add file" &&

	git checkout -b pickaxe-rename-side &&
	git mv file renamed-file &&
	git commit -m "rename file" &&

	git checkout pickaxe-rename &&
	git commit --allow-empty -m "diverge" &&
	git merge --no-edit pickaxe-rename-side &&

	git mv renamed-file file &&
	git commit -m "rename back"
'

test_expect_success '-L -G does not crash with merge and rename' '
	git log --format="%s" --no-patch -L 1,1:file -G "." >actual
'

test_expect_success '-L -S does not crash with merge and rename' '
	git log --format="%s" --no-patch -L 1,1:file -S content >actual
'

test_expect_success '-L --find-object does not crash with merge and rename' '
	git log --format="%s" --no-patch -L 1,1:file \
		--find-object=$(git rev-parse HEAD:file) >actual
'

test_expect_success '-L -G should filter commits by pattern' '
	git log --format="%s" --no-patch -L 1,1:file -G "nomatch" >actual &&
	test_must_be_empty actual
'

test_expect_success '-L -S should filter commits by pattern' '
	git log --format="%s" --no-patch -L 1,1:file -S "nomatch" >actual &&
	test_must_be_empty actual
'

test_expect_success '-L --find-object should filter commits by object' '
	git log --format="%s" --no-patch -L 1,1:file \
		--find-object=$ZERO_OID >actual &&
	test_must_be_empty actual
'

test_expect_success '-L with --word-diff-regex' '
	git checkout parent-oids &&
	git log -L:func2:file.c --word-diff \
		--word-diff-regex="[a-zA-Z0-9_]+" --format= >actual &&
	# Word-diff markers must be present
	test_grep "{+" actual &&
	test_grep "+}" actual &&
	# No line-level +/- markers (word-diff replaces them);
	# exclude --- header lines from the check
	test_grep ! "^+[^+]" actual &&
	test_grep ! "^-[^-]" actual
'

test_expect_success '-L with --src-prefix and --dst-prefix' '
	git checkout parent-oids &&
	git log -L:func2:file.c --src-prefix=old/ --dst-prefix=new/ \
		--format= >actual &&
	test_grep "^diff --git old/file.c new/file.c" actual &&
	test_grep "^--- old/file.c" actual &&
	test_grep "^+++ new/file.c" actual &&
	test_grep ! "^--- a/" actual
'

test_expect_success '-L with --abbrev' '
	git checkout parent-oids &&
	git log -L:func2:file.c --abbrev=4 --format= -1 >actual &&
	# 4-char abbreviated hashes on index line
	test_grep "^index [0-9a-f]\{4\}\.\.[0-9a-f]\{4\}" actual
'

test_expect_success '-L with -b suppresses whitespace-only diff' '
	git checkout ws-change &&
	git log -L:func2:file.c --format= >without_b &&
	git log -L:func2:file.c --format= -b >with_b &&
	test $(grep -c "^diff --git" without_b) = 3 &&
	test $(grep -c "^diff --git" with_b) = 2
'

test_expect_success '-L with --output-indicator-*' '
	git checkout parent-oids &&
	git log -L:func2:file.c --output-indicator-new=">" \
		--output-indicator-old="<" --output-indicator-context="|" \
		--format= -1 >actual &&
	test_grep "^>" actual &&
	test_grep "^<" actual &&
	test_grep "^|" actual &&
	# No standard +/-/space content markers; exclude ---/+++ headers
	test_grep ! "^+[^+]" actual &&
	test_grep ! "^-[^-]" actual &&
	test_grep ! "^ " actual
'

test_expect_success '-L with -R reverses diff' '
	git checkout parent-oids &&
	git log -L:func2:file.c -R --format= -1 >actual &&
	test_grep "^diff --git b/file.c a/file.c" actual &&
	test_grep "^--- b/file.c" actual &&
	test_grep "^+++ a/file.c" actual &&
	# The modification added "F2 + 2", so reversed it is removed
	test_grep "^-.*F2 + 2" actual &&
	test_grep "^+.*return F2;" actual
'

test_expect_success 'setup for color-moved test' '
	git checkout -b color-moved-test parent-oids &&
	cat >big.c <<-\EOF &&
	int bigfunc()
	{
	    int a = 1;
	    int b = 2;
	    int c = 3;
	    return a + b + c;
	}
	EOF
	git add big.c &&
	git commit -m "add bigfunc" &&
	sed "s/    /	/" big.c >tmp && mv tmp big.c &&
	git commit -a -m "reindent bigfunc"
'

test_expect_success '-L with --color-moved' '
	git log -L:bigfunc:big.c --color-moved=zebra \
		--color-moved-ws=ignore-all-space \
		--color=always --format= -1 >actual.raw &&
	test_decode_color <actual.raw >actual &&
	# Old moved lines: bold magenta; new moved lines: bold cyan
	test_grep "BOLD;MAGENTA" actual &&
	test_grep "BOLD;CYAN" actual
'

test_expect_success 'setup for no-newline-at-eof tests' '
	git checkout --orphan no-newline &&
	git reset --hard &&
	printf "int top()\n{\n    return 1;\n}\n\nint bot()\n{\n    return 2;\n}" >noeol.c &&
	git add noeol.c &&
	test_tick &&
	git commit -m "add noeol.c (no trailing newline)" &&
	sed "s/return 2/return 22/" noeol.c >tmp && mv tmp noeol.c &&
	git commit -a -m "modify bot()" &&
	printf "int top()\n{\n    return 1;\n}\n\nint bot()\n{\n    return 33;\n}\n" >noeol.c &&
	git commit -a -m "modify bot() and add trailing newline"
'

# When the tracked function is at the end of a file with no trailing
# newline, the "\ No newline at end of file" marker should appear.
test_expect_success '-L no-newline-at-eof appears in tracked range' '
	git log -L:bot:noeol.c --format= -1 HEAD~1 >actual &&
	test_grep "No newline at end of file" actual
'

# When tracking a function that ends before the no-newline content,
# the marker should not appear in the output.
test_expect_success '-L no-newline-at-eof suppressed outside range' '
	git log -L:top:noeol.c --format= >actual &&
	test_grep ! "No newline at end of file" actual
'

# When a commit removes a no-newline last line and replaces it with
# a newline-terminated line, the marker should still appear (on the
# old side of the diff).
test_expect_success '-L no-newline-at-eof marker with deleted line' '
	git log -L:bot:noeol.c --format= -1 >actual &&
	test_grep "No newline at end of file" actual
'

test_expect_success 'setup for range boundary deletion test' '
	git checkout --orphan range-boundary &&
	git reset --hard &&
	cat >boundary.c <<-\EOF &&
	void above()
	{
	    return;
	}

	void tracked()
	{
	    int x = 1;
	    int y = 2;
	}

	void below()
	{
	    return;
	}
	EOF
	git add boundary.c &&
	test_tick &&
	git commit -m "add boundary.c" &&
	cat >boundary.c <<-\EOF &&
	void above()
	{
	    return;
	}

	void tracked()
	{
	    int x = 1;
	    int y = 2;
	}

	void below_renamed()
	{
	    return 0;
	}
	EOF
	git commit -a -m "modify below() only"
'

# When only a function below the tracked range is modified, the
# tracked function should not produce a diff.
test_expect_success '-L suppresses deletions outside tracked range' '
	git log -L:tracked:boundary.c --format= >actual &&
	test $(grep -c "^diff --git" actual) = 1
'

test_expect_success '-L with -S filters to string-count changes' '
	git checkout parent-oids &&
	git log -L:func2:file.c -S "F2 + 2" --format= >actual &&
	# -S searches the whole file, not just the tracked range;
	# combined with the -L range walk, this selects commits that
	# both touch func2 and change the count of "F2 + 2" in the file.
	test $(grep -c "^diff --git" actual) = 1 &&
	test_grep "F2 + 2" actual
'

test_expect_success '-L with -G filters to diff-text matches' '
	git checkout parent-oids &&
	git log -L:func2:file.c -G "F2 [+] 2" --format= >actual &&
	# -G greps the diff text, and under -L only the lines in the
	# tracked range (unlike -S above, which searches the whole file);
	# this selects commits whose change to func2 contains "F2 + 2".
	test $(grep -c "^diff --git" actual) = 1 &&
	test_grep "F2 + 2" actual
'

test_expect_success 'setup for trailing deletion test' '
	git checkout --orphan trailing-del &&
	git reset --hard &&
	cat >file.c <<-\EOF &&
	void tracked()
	{
	    return 1;
	}
	// trailing comment
	EOF
	git add file.c &&
	test_tick &&
	git commit -m "add file with trailing comment" &&
	# Modify tracked() AND delete the trailing comment in
	# one commit, so the commit touches the tracked range
	# and is not filtered out by the revision walker.
	cat >file.c <<-\EOF &&
	void tracked()
	{
	    return 2;
	}
	EOF
	git commit -a -m "modify tracked and delete trailing comment"
'

test_expect_success '-L does not include deletions past end of tracked range' '
	git log -L:tracked:file.c --format= -1 -p >actual &&
	# The trailing comment deletion is outside the tracked
	# range and should not appear in the patch output.
	test_grep "return 2" actual &&
	test_grep ! "trailing comment" actual
'

test_expect_success '-L includes leading deletions resolved by in-range line' '
	git checkout --orphan leading-del &&
	git reset --hard &&
	cat >file.c <<-\EOF &&
	// leading comment
	void tracked()
	{
	    return 1;
	}
	EOF
	git add file.c &&
	test_tick &&
	git commit -m "add file with leading comment" &&
	cat >file.c <<-\EOF &&
	void tracked()
	{
	    return 2;
	}
	EOF
	git commit -a -m "modify tracked and delete leading comment" &&
	git log -L:tracked:file.c --format= -1 -p >actual &&
	# The leading comment deletion is resolved by the next
	# non-removal line (void tracked), which is in range: a
	# removal is classified by the position of the following
	# line, so it joins the range that line falls in.
	test_grep "return 2" actual &&
	test_grep "leading comment" actual
'

test_expect_success 'setup for line-range filter edge cases' '
	git checkout --orphan filter-edge &&
	git reset --hard &&
	cat >file.c <<-\EOF &&
	void before()
	{
	    return 0;
	}

	void tracked()
	{
	    int a = 1;
	    int b = 2;
	    int c = 3;
	    return a + b + c;
	}

	void after()
	{
	    return 9;
	}
	EOF
	git add file.c &&
	test_tick &&
	git commit -m "initial"
'

test_expect_success '-L change at exact first line of range' '
	git checkout filter-edge &&
	# Change the function signature (first line of range)
	sed "s/void tracked/int tracked/" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "change first line" &&
	git log -L:tracked:file.c -p --format=%s -1 >actual &&
	test_grep "change first line" actual &&
	test_grep "+int tracked" actual &&
	test_grep "\\-void tracked" actual
'

test_expect_success '-L change at exact last line of range' '
	git checkout filter-edge &&
	git reset --hard HEAD~1 &&
	# Change the closing brace line (last line of range)
	sed "s/^}$/} \/\/ end tracked/" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "change last line" &&
	git log -L:tracked:file.c -p --format=%s -1 >actual &&
	test_grep "change last line" actual &&
	test_grep "end tracked" actual
'

test_expect_success '-L pure deletion in range (no additions)' '
	git checkout filter-edge &&
	git reset --hard HEAD~1 &&
	# Delete a line inside tracked() without adding anything
	sed "/int c/d" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "pure deletion" &&
	git log -L:tracked:file.c -p --format=%s -1 >actual &&
	test_grep "pure deletion" actual &&
	test_grep "\\-.*int c" actual
'

test_expect_success '-L with --diff-filter=M excludes root commit' '
	git checkout parent-oids &&
	git log -L:func2:file.c --diff-filter=M --format=%s --no-patch >actual &&
	# Root commit is an Add (A), not a Modify (M), so it should
	# be excluded; only the modification commit remains.
	echo "Modify func2() in file.c" >expect &&
	test_cmp expect actual
'

test_expect_success '-L with --diff-filter=A shows only root commit' '
	git checkout parent-oids &&
	git log -L:func2:file.c --diff-filter=A --format=%s --no-patch >actual &&
	echo "Add func1() and func2() in file.c" >expect &&
	test_cmp expect actual
'

test_expect_success '-L with -S suppresses non-matching commits' '
	git checkout parent-oids &&
	git log -L:func2:file.c -S "F2 + 2" --format=%s --no-patch >actual &&
	# Only the commit that changes the count of "F2 + 2" should appear.
	echo "Modify func2() in file.c" >expect &&
	test_cmp expect actual
'

test_expect_success '--full-diff is not supported with -L' '
	test_must_fail git log -L1,24:b.c --full-diff 2>err &&
	test_grep "does not support" err
'

test_expect_success '-L --oneline has no extra blank line before diff' '
	git checkout parent-oids &&
	git log --oneline -L:func2:file.c -1 >actual &&
	# Oneline header on line 1, diff starts immediately on line 2
	sed -n 2p actual >line2 &&
	test_grep "^diff --git" line2
'

test_expect_success 'setup for stat range-scoping tests' '
	git checkout --orphan stat-scoping &&
	git reset --hard &&
	cat >file.c <<-\EOF &&
	int func1()
	{
	    return F1;
	}

	int func2()
	{
	    return F2;
	}
	EOF
	git add file.c &&
	test_tick &&
	git commit -m "Add func1() and func2()" &&

	# Modify both functions in a single commit so that
	# whole-file stats differ from the counts for the tracked range.
	sed -e "s/F1/F1 + 1/" -e "s/F2/F2 + 2/" file.c >tmp &&
	mv tmp file.c &&
	git commit -a -m "Modify both functions"
'

test_expect_success '--numstat counts only lines in tracked range' '
	# "Modify both functions" changes one line in func1 and one in
	# func2.  Whole-file numstat would show 2 added, 2 deleted.
	# numstat for func2 within the tracked range should show only 1 and 1.
	git log -L:func2:file.c --numstat --format=%s -1 >actual &&
	test_grep "Modify both functions" actual &&
	test_grep "^1	1	file.c$" actual &&
	test_grep ! "^diff --git" actual
'

test_expect_success '--numstat counts only additions for root commit' '
	# Root commit creates both func1 (4 lines) and func2 (4 lines).
	# Whole-file numstat would show 9 lines added.  numstat for func2
	# within the tracked range should show only 4.
	git log -L:func2:file.c --numstat --format=%s >actual &&
	test_grep "Add func1() and func2()" actual &&
	test_grep "^4	0	file.c$" actual &&
	test_grep ! "^diff --git" actual
'

test_expect_success '--stat counts only lines in tracked range' '
	git log -L:func2:file.c --stat --format=%s -1 >actual &&
	test_grep "Modify both functions" actual &&
	test_grep "file.c |" actual &&
	test_grep "1 insertion" actual &&
	test_grep "1 deletion" actual &&
	test_grep ! "^diff --git" actual
'

test_expect_success '--shortstat counts only lines in tracked range' '
	# --shortstat prints only the summary line: no per-file "file.c |"
	# line.  Counts cover only the tracked range, as for --numstat above.
	git log -L:func2:file.c --shortstat --format=%s -1 >actual &&
	test_grep "Modify both functions" actual &&
	test_grep "1 insertion" actual &&
	test_grep "1 deletion" actual &&
	test_grep ! "file.c |" actual &&
	test_grep ! "^diff --git" actual
'

test_expect_success '--numstat across renames and multiple commits' '
	# parallel-change carries the tracked function f across an a.c -> b.c
	# rename and a merge of two parallel histories.  With -M, --numstat
	# follows the rename and reports added/removed counts for f within
	# the tracked range (not whole-file) per commit; the file column flips from
	# b.c to a.c at the rename as the walk goes back in time.  Commits
	# that do not change the range of f emit no row (the merge and the
	# pure file-move produce nothing), so there are fewer rows than
	# commits.
	git checkout parallel-change &&
	git log -M -L ":f:b.c" --format= --numstat >actual &&
	cat >expect <<-\EOF &&
	1	1	b.c
	1	1	a.c
	1	1	a.c
	1	1	a.c
	1	0	a.c
	13	0	a.c
	EOF
	test_cmp expect actual
'

test_expect_success '-L multiple ranges with --numstat excludes untracked change' '
	git checkout --orphan multi-range &&
	git reset --hard &&
	cat >m.c <<-\EOF &&
	int func1()
	{
	    return F1;
	}

	int func2()
	{
	    return F2;
	}

	int func3()
	{
	    return F3;
	}
	EOF
	git add m.c &&
	test_tick &&
	git commit -m "add m.c" &&
	# Change all three functions but track only func1 and func2.
	# Whole-file numstat would be 3 3; a 2 2 result proves the
	# untracked func3 change is excluded and the two ranges just sum.
	sed -e "s/F1/F1 + 1/" -e "s/F2/F2 + 2/" -e "s/F3/F3 + 3/" m.c >tmp &&
	mv tmp m.c &&
	git commit -a -m "Modify all three functions" &&
	git log -L:func1:m.c -L:func2:m.c --numstat --format=%s -1 >actual &&
	test_grep "Modify all three functions" actual &&
	test_grep "^2	2	m.c$" actual &&
	test_grep ! "^3	3	m.c$" actual
'

test_expect_success '--summary shows new file on root commit' '
	git checkout parent-oids &&
	git log -L:func2:file.c --summary --format= >actual &&
	test_grep "create mode 100644 file.c" actual
'

test_expect_success 'setup for --check test' '
	git checkout --orphan check-test &&
	git reset --hard &&
	cat >check.c <<-\EOF &&
	void tracked()
	{
	    return;
	}

	void other()
	{
	    return;
	}
	EOF
	git add check.c &&
	test_tick &&
	git commit -m "add check.c" &&
	# Introduce trailing whitespace errors in both functions
	sed "s/return;/return; /" check.c >check.c.tmp &&
	mv check.c.tmp check.c &&
	git commit -a -m "introduce trailing whitespace"
'

test_expect_success '--check scoped to tracked range with correct file line' '
	# tracked() trailing whitespace is at check.c:3; report it with the
	# real file line number, not a count from the start of the range
	# hunk.  other() at check.c:8 is outside the range and is excluded.
	test_must_fail git log -L:tracked:check.c --check --format= >actual &&
	test_grep "check.c:3: trailing whitespace" actual &&
	test_grep ! "check.c:8:" actual
'

test_expect_success '--check reports each of several tracked ranges' '
	# Track both functions as separate ranges.  Each range is flushed
	# as its own hunk, so the second error must report its real file
	# line (check.c:8), not continue the numbering from the first
	# range (check.c:3).
	test_must_fail git log -L:tracked:check.c -L:other:check.c \
		--check --format= >actual &&
	test_grep "check.c:3: trailing whitespace" actual &&
	test_grep "check.c:8: trailing whitespace" actual
'

test_expect_success '--check line numbers stay correct across a gap in one range' '
	git checkout --orphan check-gap &&
	git reset --hard &&
	cat >gap.c <<-\EOF &&
	void tracked()
	{
	    int a = 1;
	    int b = 2;
	    int c = 3;
	    int d = 4;
	    int e = 5;
	    int g = 7;
	    return;
	}
	EOF
	git add gap.c &&
	test_tick &&
	git commit -m "add gap.c" &&
	# Two trailing-whitespace errors within one tracked range,
	# separated by clean lines.  ctxlen is inflated to the range span,
	# so they land in a single xdiff hunk with the gap as context;
	# both must report their real file line number, with the context
	# lines between them counted.
	sed -e "s/int a = 1;/int a = 1; /" -e "s/int g = 7;/int g = 7; /" gap.c >tmp &&
	mv tmp gap.c &&
	git commit -a -m "ws errors with a gap" &&
	test_must_fail git log -L:tracked:gap.c --check --format= >actual &&
	test_grep "gap.c:3: trailing whitespace" actual &&
	test_grep "gap.c:8: trailing whitespace" actual
'

test_expect_success '--check does not report blank-at-eof outside the range' '
	git checkout --orphan check-eof &&
	git reset --hard &&
	printf "void tracked()\n{\n    return;\n}\n\nint tail = 1;\n" >eof.c &&
	git add eof.c &&
	test_tick &&
	git commit -m "add eof.c" &&
	# One commit introduces a trailing-whitespace error inside tracked()
	# (line 3) and a blank line at end of file (line 7, outside the
	# range).  The blank-at-eof check scans the whole file, so it must be
	# scoped: report the in-range error, not the out-of-range EOF blank.
	printf "void tracked()\n{\n    return; \n}\n\nint tail = 1;\n\n" >eof.c &&
	git commit -a -m "ws in range, blank at eof out of range" &&
	test_must_fail git log -L:tracked:eof.c --check --format= >actual &&
	test_grep "eof.c:3: trailing whitespace" actual &&
	test_grep ! "blank line at EOF" actual
'

test_expect_success '-L -G is scoped to the tracked range' '
	git checkout --orphan grep-scope &&
	git reset --hard &&
	cat >gp.c <<-\EOF &&
	int func1()
	{
	    return ALPHA;
	}

	int func2()
	{
	    return BETA;
	}
	EOF
	git add gp.c &&
	test_tick &&
	git commit -m "add gp.c" &&
	sed -e "s/ALPHA/ALPHA2/" -e "s/BETA/BETA2/" gp.c >tmp &&
	mv tmp gp.c &&
	git commit -a -m "touch both functions" &&
	# The commit changes ALPHA (func1) and BETA (func2).  Tracking func2,
	# -G BETA matches its in-range change; -G ALPHA must not, since ALPHA
	# changes only outside the tracked range.
	git log -L:func2:gp.c -G BETA --format=%s >actual &&
	test_grep "touch both functions" actual &&
	git log -L:func2:gp.c -G ALPHA --format=%s >actual &&
	test_grep ! "touch both functions" actual
'

test_expect_success '-L -G searches the whole file under textconv' '
	git checkout --orphan grep-textconv &&
	git reset --hard &&
	cat >tc.c <<-\EOF &&
	int func1()
	{
	    return F1;
	}

	int func2()
	{
	    return F2;
	}
	EOF
	git add tc.c &&
	test_tick &&
	git commit -m "add tc.c" &&
	# One commit changes func1 and func2; MAGIC lands only in the
	# func2 change, outside func1.
	sed -e "s/F1/F1 + 1/" -e "s/return F2/return MAGIC/" tc.c >tmp &&
	mv tmp tc.c &&
	git commit -a -m "change both funcs" &&
	echo "tc.c diff=tc" >.gitattributes &&

	# Without a textconv driver, -G is scoped to func1, so MAGIC (only
	# in the func2 change) does not select the commit.
	git log -L:func1:tc.c -G MAGIC --format=%s --no-patch >actual &&
	test_must_be_empty actual &&

	# A textconv driver makes the range (original-file line numbers)
	# meaningless against the driver output, so -G falls back to the
	# whole file and MAGIC now selects the commit.
	git config diff.tc.textconv cat &&
	git log -L:func1:tc.c -G MAGIC --format=%s --no-patch >actual &&
	test_grep "change both funcs" actual
'

test_expect_success 'get_commit_action() does not mutate a not-yet-walked commit' '
	git init peek &&
	(
		cd peek &&
		test_write_lines 1 2 3 4 5 >f.c &&
		git add f.c && test_tick && git commit -m base &&
		test_write_lines 1 two 3 4 5 >f.c &&
		test_tick && git commit -am change &&

		# Peek HEAD^, which the walk has not reached (the out-of-order
		# call a lookahead makes), and confirm get_commit_action() leaves
		# it untouched.  A side effect is invisible in the commit list
		# (range merges are idempotent), so the helper reports whether the
		# call mutated the peeked commit at all.
		echo "mutated 0" >expect &&
		test-tool revision-walking line-log-peek HEAD^ 1,3:f.c HEAD >actual &&
		test_cmp expect actual
	)
'

test_done
