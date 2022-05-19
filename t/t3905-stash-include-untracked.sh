#!/bin/sh
#
# Copyright (c) 2011 David Caldwell
#

test_description='Test but stash --include-untracked'

. ./test-lib.sh

test_expect_success 'stash save --include-untracked some dirty working directory' '
	echo 1 >file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	echo 2 >file &&
	but add file &&
	echo 3 >file &&
	test_tick &&
	echo 1 >file2 &&
	echo 1 >HEAD &&
	mkdir untracked &&
	echo untracked >untracked/untracked &&
	but stash --include-untracked &&
	but diff-files --quiet &&
	but diff-index --cached --quiet HEAD
'

test_expect_success 'stash save --include-untracked cleaned the untracked files' '
	cat >expect <<-EOF &&
	?? actual
	?? expect
	EOF

	but status --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success 'stash save --include-untracked stashed the untracked files' '
	one_blob=$(echo 1 | but hash-object --stdin) &&
	tracked=$(but rev-parse --short "$one_blob") &&
	untracked_blob=$(echo untracked | but hash-object --stdin) &&
	untracked=$(but rev-parse --short "$untracked_blob") &&
	cat >expect.diff <<-EOF &&
	diff --but a/HEAD b/HEAD
	new file mode 100644
	index 0000000..$tracked
	--- /dev/null
	+++ b/HEAD
	@@ -0,0 +1 @@
	+1
	diff --but a/file2 b/file2
	new file mode 100644
	index 0000000..$tracked
	--- /dev/null
	+++ b/file2
	@@ -0,0 +1 @@
	+1
	diff --but a/untracked/untracked b/untracked/untracked
	new file mode 100644
	index 0000000..$untracked
	--- /dev/null
	+++ b/untracked/untracked
	@@ -0,0 +1 @@
	+untracked
	EOF
	cat >expect.lstree <<-EOF &&
	HEAD
	file2
	untracked
	EOF

	test_path_is_missing file2 &&
	test_path_is_missing untracked &&
	test_path_is_missing HEAD &&
	but diff HEAD stash^3 -- HEAD file2 untracked >actual &&
	test_cmp expect.diff actual &&
	but ls-tree --name-only stash^3: >actual &&
	test_cmp expect.lstree actual
'
test_expect_success 'stash save --patch --include-untracked fails' '
	test_must_fail but stash --patch --include-untracked
'

test_expect_success 'stash save --patch --all fails' '
	test_must_fail but stash --patch --all
'

test_expect_success 'clean up untracked/untracked file to prepare for next tests' '
	but clean --force --quiet

'

test_expect_success 'stash pop after save --include-untracked leaves files untracked again' '
	cat >expect <<-EOF &&
	 M file
	?? HEAD
	?? actual
	?? expect
	?? file2
	?? untracked/
	EOF

	but stash pop &&
	but status --porcelain >actual &&
	test_cmp expect actual &&
	echo 1 >expect_file2 &&
	test_cmp expect_file2 file2 &&
	echo untracked >untracked_expect &&
	test_cmp untracked_expect untracked/untracked
'

test_expect_success 'clean up untracked/ directory to prepare for next tests' '
	but clean --force --quiet -d
'

test_expect_success 'stash save -u dirty index' '
	echo 4 >file3 &&
	but add file3 &&
	test_tick &&
	but stash -u
'

test_expect_success 'stash save --include-untracked dirty index got stashed' '
	four_blob=$(echo 4 | but hash-object --stdin) &&
	blob=$(but rev-parse --short "$four_blob") &&
	cat >expect <<-EOF &&
	diff --but a/file3 b/file3
	new file mode 100644
	index 0000000..$blob
	--- /dev/null
	+++ b/file3
	@@ -0,0 +1 @@
	+4
	EOF

	but stash pop --index &&
	test_when_finished "but reset" &&
	but diff --cached >actual &&
	test_cmp expect actual
'

# Must direct output somewhere where it won't be considered an untracked file
test_expect_success 'stash save --include-untracked -q is quiet' '
	echo 1 >file5 &&
	but stash save --include-untracked --quiet >.but/stash-output.out 2>&1 &&
	test_line_count = 0 .but/stash-output.out &&
	rm -f .but/stash-output.out
'

test_expect_success 'stash save --include-untracked removed files' '
	rm -f file &&
	but stash save --include-untracked &&
	echo 1 >expect &&
	test_when_finished "rm -f expect" &&
	test_cmp expect file
'

test_expect_success 'stash save --include-untracked removed files got stashed' '
	but stash pop &&
	test_path_is_missing file
'

test_expect_success 'stash save --include-untracked respects .butignore' '
	cat >.butignore <<-EOF &&
	.butignore
	ignored
	ignored.d/
	EOF

	echo ignored >ignored &&
	mkdir ignored.d &&
	echo ignored >ignored.d/untracked &&
	but stash -u &&
	test_file_not_empty ignored &&
	test_file_not_empty ignored.d/untracked &&
	test_file_not_empty .butignore
'

test_expect_success 'stash save -u can stash with only untracked files different' '
	echo 4 >file4 &&
	but stash -u &&
	test_path_is_missing file4
'

test_expect_success 'stash save --all does not respect .butignore' '
	but stash -a &&
	test_path_is_missing ignored &&
	test_path_is_missing ignored.d &&
	test_path_is_missing .butignore
'

test_expect_success 'stash save --all is stash poppable' '
	but stash pop &&
	test_file_not_empty ignored &&
	test_file_not_empty ignored.d/untracked &&
	test_file_not_empty .butignore
'

test_expect_success 'stash push --include-untracked with pathspec' '
	>foo &&
	>bar &&
	but stash push --include-untracked -- foo &&
	test_path_is_file bar &&
	test_path_is_missing foo &&
	but stash pop &&
	test_path_is_file bar &&
	test_path_is_file foo
'

test_expect_success 'stash push with $IFS character' '
	>"foo bar" &&
	>foo &&
	>bar &&
	but add foo* &&
	but stash push --include-untracked -- "foo b*" &&
	test_path_is_missing "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	but stash pop &&
	test_path_is_file "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash previously ignored file' '
	cat >.butignore <<-EOF &&
	ignored
	ignored.d/*
	EOF

	but reset HEAD &&
	but add .butignore &&
	but cummit -m "Add .butignore" &&
	>ignored.d/foo &&
	echo "!ignored.d/foo" >>.butignore &&
	but stash save --include-untracked &&
	test_path_is_missing ignored.d/foo &&
	but stash pop &&
	test_path_is_file ignored.d/foo
'

test_expect_success 'stash -u -- <untracked> doesnt print error' '
	>untracked &&
	but stash push -u -- untracked 2>actual &&
	test_path_is_missing untracked &&
	test_line_count = 0 actual
'

test_expect_success 'stash -u -- <untracked> leaves rest of working tree in place' '
	>tracked &&
	but add tracked &&
	>untracked &&
	but stash push -u -- untracked &&
	test_path_is_missing untracked &&
	test_path_is_file tracked
'

test_expect_success 'stash -u -- <tracked> <untracked> clears changes in both' '
	>tracked &&
	but add tracked &&
	>untracked &&
	but stash push -u -- tracked untracked &&
	test_path_is_missing tracked &&
	test_path_is_missing untracked
'

test_expect_success 'stash --all -- <ignored> stashes ignored file' '
	>ignored.d/bar &&
	but stash push --all -- ignored.d/bar &&
	test_path_is_missing ignored.d/bar
'

test_expect_success 'stash --all -- <tracked> <ignored> clears changes in both' '
	>tracked &&
	but add tracked &&
	>ignored.d/bar &&
	but stash push --all -- tracked ignored.d/bar &&
	test_path_is_missing tracked &&
	test_path_is_missing ignored.d/bar
'

test_expect_success 'stash -u -- <ignored> leaves ignored file alone' '
	>ignored.d/bar &&
	but stash push -u -- ignored.d/bar &&
	test_path_is_file ignored.d/bar
'

test_expect_success 'stash -u -- <non-existent> shows no changes when there are none' '
	but stash push -u -- non-existent >actual &&
	echo "No local changes to save" >expect &&
	test_cmp expect actual
'

test_expect_success 'stash -u with globs' '
	>untracked.txt &&
	but stash -u -- ":(glob)**/*.txt" &&
	test_path_is_missing untracked.txt
'

test_expect_success 'stash show --include-untracked shows untracked files' '
	but reset --hard &&
	but clean -xf &&
	>untracked &&
	>tracked &&
	but add tracked &&
	empty_blob_oid=$(but rev-parse --short :tracked) &&
	but stash -u &&

	cat >expect <<-EOF &&
	 tracked   | 0
	 untracked | 0
	 2 files changed, 0 insertions(+), 0 deletions(-)
	EOF
	but stash show --include-untracked >actual &&
	test_cmp expect actual &&
	but stash show -u >actual &&
	test_cmp expect actual &&
	but stash show --no-include-untracked --include-untracked >actual &&
	test_cmp expect actual &&
	but stash show --only-untracked --include-untracked >actual &&
	test_cmp expect actual &&
	but -c stash.showIncludeUntracked=true stash show >actual &&
	test_cmp expect actual &&

	cat >expect <<-EOF &&
	diff --but a/tracked b/tracked
	new file mode 100644
	index 0000000..$empty_blob_oid
	diff --but a/untracked b/untracked
	new file mode 100644
	index 0000000..$empty_blob_oid
	EOF
	but stash show -p --include-untracked >actual &&
	test_cmp expect actual &&
	but stash show --include-untracked -p >actual &&
	test_cmp expect actual &&
	but -c stash.showIncludeUntracked=true stash show -p >actual &&
	test_cmp expect actual
'

test_expect_success 'stash show --only-untracked only shows untracked files' '
	but reset --hard &&
	but clean -xf &&
	>untracked &&
	>tracked &&
	but add tracked &&
	empty_blob_oid=$(but rev-parse --short :tracked) &&
	but stash -u &&

	cat >expect <<-EOF &&
	 untracked | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	but stash show --only-untracked >actual &&
	test_cmp expect actual &&
	but stash show --no-include-untracked --only-untracked >actual &&
	test_cmp expect actual &&
	but stash show --include-untracked --only-untracked >actual &&
	test_cmp expect actual &&

	cat >expect <<-EOF &&
	diff --but a/untracked b/untracked
	new file mode 100644
	index 0000000..$empty_blob_oid
	EOF
	but stash show -p --only-untracked >actual &&
	test_cmp expect actual &&
	but stash show --only-untracked -p >actual &&
	test_cmp expect actual
'

test_expect_success 'stash show --no-include-untracked cancels --{include,only}-untracked' '
	but reset --hard &&
	but clean -xf &&
	>untracked &&
	>tracked &&
	but add tracked &&
	but stash -u &&

	cat >expect <<-EOF &&
	 tracked | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	but stash show --only-untracked --no-include-untracked >actual &&
	test_cmp expect actual &&
	but stash show --include-untracked --no-include-untracked >actual &&
	test_cmp expect actual
'

test_expect_success 'stash show --include-untracked errors on duplicate files' '
	but reset --hard &&
	but clean -xf &&
	>tracked &&
	but add tracked &&
	tree=$(but write-tree) &&
	i_cummit=$(but cummit-tree -p HEAD -m "index on any-branch" "$tree") &&
	test_when_finished "rm -f untracked_index" &&
	u_cummit=$(
		BUT_INDEX_FILE="untracked_index" &&
		export BUT_INDEX_FILE &&
		but update-index --add tracked &&
		u_tree=$(but write-tree) &&
		but cummit-tree -m "untracked files on any-branch" "$u_tree"
	) &&
	w_cummit=$(but cummit-tree -p HEAD -p "$i_cummit" -p "$u_cummit" -m "WIP on any-branch" "$tree") &&
	test_must_fail but stash show --include-untracked "$w_cummit" 2>err &&
	test_i18ngrep "worktree and untracked commit have duplicate entries: tracked" err
'

test_expect_success 'stash show --{include,only}-untracked on stashes without untracked entries' '
	but reset --hard &&
	but clean -xf &&
	>tracked &&
	but add tracked &&
	but stash &&

	but stash show >expect &&
	but stash show --include-untracked >actual &&
	test_cmp expect actual &&

	but stash show --only-untracked >actual &&
	test_must_be_empty actual
'

test_expect_success 'stash -u ignores sub-repository' '
	test_when_finished "rm -rf sub-repo" &&
	but init sub-repo &&
	but stash -u
'

test_done
