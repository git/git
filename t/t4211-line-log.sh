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

test_expect_success '--raw is forbidden' '
	test_must_fail git log -L1,24:b.c --raw
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
	grep "^index $head_blob_old\.\.$head_blob_new 100644" actual &&

	# Root commit should show new file mode and null index
	grep "^new file mode 100644" actual &&
	grep "^index $null_blob\.\.$root_blob$" actual &&

	# Hunk headers should include funcname context
	grep "^@@ .* @@ int func1()" actual
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
	grep "^diff --git file.c file.c" actual &&
	grep "^--- file.c" actual &&
	! grep "^--- a/" actual
'

test_expect_success '-L with --full-index' '
	git log -L:func2:file.c --full-index --format= >actual &&
	grep "^index $head_blob_old_full\.\.$head_blob_new_full 100644" actual &&
	grep "^index $null_blob_full\.\.$root_blob_full$" actual
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
	|Z
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
	ZZ
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

# Commit-level filtering with pickaxe does not yet work for -L.
# show_log() prints the commit header before diffcore_std() runs
# pickaxe, so commits cannot be suppressed even when no diff pairs
# survive filtering.  Fixing this would require deferring show_log()
# until after diffcore_std(), which is a larger restructuring of the
# log-tree output pipeline.
test_expect_failure '-L -G should filter commits by pattern' '
	git log --format="%s" --no-patch -L 1,1:file -G "nomatch" >actual &&
	test_must_be_empty actual
'

test_expect_failure '-L -S should filter commits by pattern' '
	git log --format="%s" --no-patch -L 1,1:file -S "nomatch" >actual &&
	test_must_be_empty actual
'

test_expect_failure '-L --find-object should filter commits by object' '
	git log --format="%s" --no-patch -L 1,1:file \
		--find-object=$ZERO_OID >actual &&
	test_must_be_empty actual
'

test_expect_success '-L with --word-diff-regex' '
	git checkout parent-oids &&
	git log -L:func2:file.c --word-diff \
		--word-diff-regex="[a-zA-Z0-9_]+" --format= >actual &&
	# Word-diff markers must be present
	grep "{+" actual &&
	grep "+}" actual &&
	# No line-level +/- markers (word-diff replaces them);
	# exclude --- header lines from the check
	! grep "^+[^+]" actual &&
	! grep "^-[^-]" actual
'

test_expect_success '-L with --src-prefix and --dst-prefix' '
	git checkout parent-oids &&
	git log -L:func2:file.c --src-prefix=old/ --dst-prefix=new/ \
		--format= >actual &&
	grep "^diff --git old/file.c new/file.c" actual &&
	grep "^--- old/file.c" actual &&
	grep "^+++ new/file.c" actual &&
	! grep "^--- a/" actual
'

test_expect_success '-L with --abbrev' '
	git checkout parent-oids &&
	git log -L:func2:file.c --abbrev=4 --format= -1 >actual &&
	# 4-char abbreviated hashes on index line
	grep "^index [0-9a-f]\{4\}\.\.[0-9a-f]\{4\}" actual
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
	grep "^>" actual &&
	grep "^<" actual &&
	grep "^|" actual &&
	# No standard +/-/space content markers; exclude ---/+++ headers
	! grep "^+[^+]" actual &&
	! grep "^-[^-]" actual &&
	! grep "^ " actual
'

test_expect_success '-L with -R reverses diff' '
	git checkout parent-oids &&
	git log -L:func2:file.c -R --format= -1 >actual &&
	grep "^diff --git b/file.c a/file.c" actual &&
	grep "^--- b/file.c" actual &&
	grep "^+++ a/file.c" actual &&
	# The modification added "F2 + 2", so reversed it is removed
	grep "^-.*F2 + 2" actual &&
	grep "^+.*return F2;" actual
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
	grep "BOLD;MAGENTA" actual &&
	grep "BOLD;CYAN" actual
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
	grep "No newline at end of file" actual
'

# When tracking a function that ends before the no-newline content,
# the marker should not appear in the output.
test_expect_success '-L no-newline-at-eof suppressed outside range' '
	git log -L:top:noeol.c --format= >actual &&
	! grep "No newline at end of file" actual
'

# When a commit removes a no-newline last line and replaces it with
# a newline-terminated line, the marker should still appear (on the
# old side of the diff).
test_expect_success '-L no-newline-at-eof marker with deleted line' '
	git log -L:bot:noeol.c --format= -1 >actual &&
	grep "No newline at end of file" actual
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
	grep "F2 + 2" actual
'

test_expect_success '-L with -G filters to diff-text matches' '
	git checkout parent-oids &&
	git log -L:func2:file.c -G "F2 [+] 2" --format= >actual &&
	# -G greps the whole-file diff text, not just the tracked range;
	# combined with -L, this selects commits that both touch func2
	# and have "F2 + 2" in their diff.
	test $(grep -c "^diff --git" actual) = 1 &&
	grep "F2 + 2" actual
'

test_done
