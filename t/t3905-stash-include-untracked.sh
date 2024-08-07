#!/bin/sh
#
# Copyright (c) 2011 David Caldwell
#

test_description='Test git stash --include-untracked'

. ./test-lib.sh

test_expect_success 'stash save --include-untracked some dirty working directory' '
	echo 1 >file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo 2 >file &&
	git add file &&
	echo 3 >file &&
	test_tick &&
	echo 1 >file2 &&
	echo 1 >HEAD &&
	mkdir untracked &&
	echo untracked >untracked/untracked &&
	git stash --include-untracked &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD
'

test_expect_success 'stash save --include-untracked cleaned the untracked files' '
	cat >expect <<-EOF &&
	?? actual
	?? expect
	EOF

	git status --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success 'stash save --include-untracked stashed the untracked files' '
	one_blob=$(echo 1 | git hash-object --stdin) &&
	tracked=$(git rev-parse --short "$one_blob") &&
	untracked_blob=$(echo untracked | git hash-object --stdin) &&
	untracked=$(git rev-parse --short "$untracked_blob") &&
	cat >expect.diff <<-EOF &&
	diff --git a/HEAD b/HEAD
	new file mode 100644
	index 0000000..$tracked
	--- /dev/null
	+++ b/HEAD
	@@ -0,0 +1 @@
	+1
	diff --git a/file2 b/file2
	new file mode 100644
	index 0000000..$tracked
	--- /dev/null
	+++ b/file2
	@@ -0,0 +1 @@
	+1
	diff --git a/untracked/untracked b/untracked/untracked
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
	git diff HEAD stash^3 -- HEAD file2 untracked >actual &&
	test_cmp expect.diff actual &&
	git ls-tree --name-only stash^3: >actual &&
	test_cmp expect.lstree actual
'
test_expect_success 'stash save --patch --include-untracked fails' '
	test_must_fail git stash --patch --include-untracked
'

test_expect_success 'stash save --patch --all fails' '
	test_must_fail git stash --patch --all
'

test_expect_success 'clean up untracked/untracked file to prepare for next tests' '
	git clean --force --quiet

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

	git stash pop &&
	git status --porcelain >actual &&
	test_cmp expect actual &&
	echo 1 >expect_file2 &&
	test_cmp expect_file2 file2 &&
	echo untracked >untracked_expect &&
	test_cmp untracked_expect untracked/untracked
'

test_expect_success 'clean up untracked/ directory to prepare for next tests' '
	git clean --force --quiet -d
'

test_expect_success 'stash save -u dirty index' '
	echo 4 >file3 &&
	git add file3 &&
	test_tick &&
	git stash -u
'

test_expect_success 'stash save --include-untracked dirty index got stashed' '
	four_blob=$(echo 4 | git hash-object --stdin) &&
	blob=$(git rev-parse --short "$four_blob") &&
	cat >expect <<-EOF &&
	diff --git a/file3 b/file3
	new file mode 100644
	index 0000000..$blob
	--- /dev/null
	+++ b/file3
	@@ -0,0 +1 @@
	+4
	EOF

	git stash pop --index &&
	test_when_finished "git reset" &&
	git diff --cached >actual &&
	test_cmp expect actual
'

# Must direct output somewhere where it won't be considered an untracked file
test_expect_success 'stash save --include-untracked -q is quiet' '
	echo 1 >file5 &&
	git stash save --include-untracked --quiet >.git/stash-output.out 2>&1 &&
	test_line_count = 0 .git/stash-output.out &&
	rm -f .git/stash-output.out
'

test_expect_success 'stash save --include-untracked removed files' '
	rm -f file &&
	git stash save --include-untracked &&
	echo 1 >expect &&
	test_when_finished "rm -f expect" &&
	test_cmp expect file
'

test_expect_success 'stash save --include-untracked removed files got stashed' '
	git stash pop &&
	test_path_is_missing file
'

test_expect_success 'stash save --include-untracked respects .gitignore' '
	cat >.gitignore <<-EOF &&
	.gitignore
	ignored
	ignored.d/
	EOF

	echo ignored >ignored &&
	mkdir ignored.d &&
	echo ignored >ignored.d/untracked &&
	git stash -u &&
	test_file_not_empty ignored &&
	test_file_not_empty ignored.d/untracked &&
	test_file_not_empty .gitignore
'

test_expect_success 'stash save -u can stash with only untracked files different' '
	echo 4 >file4 &&
	git stash -u &&
	test_path_is_missing file4
'

test_expect_success 'stash save --all does not respect .gitignore' '
	git stash -a &&
	test_path_is_missing ignored &&
	test_path_is_missing ignored.d &&
	test_path_is_missing .gitignore
'

test_expect_success 'stash save --all is stash poppable' '
	git stash pop &&
	test_file_not_empty ignored &&
	test_file_not_empty ignored.d/untracked &&
	test_file_not_empty .gitignore
'

test_expect_success 'stash push --include-untracked with pathspec' '
	>foo &&
	>bar &&
	git stash push --include-untracked -- foo &&
	test_path_is_file bar &&
	test_path_is_missing foo &&
	git stash pop &&
	test_path_is_file bar &&
	test_path_is_file foo
'

test_expect_success 'stash push with $IFS character' '
	>"foo bar" &&
	>foo &&
	>bar &&
	git add foo* &&
	git stash push --include-untracked -- "foo b*" &&
	test_path_is_missing "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	git stash pop &&
	test_path_is_file "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash previously ignored file' '
	cat >.gitignore <<-EOF &&
	ignored
	ignored.d/*
	EOF

	git reset HEAD &&
	git add .gitignore &&
	git commit -m "Add .gitignore" &&
	>ignored.d/foo &&
	echo "!ignored.d/foo" >>.gitignore &&
	git stash save --include-untracked &&
	test_path_is_missing ignored.d/foo &&
	git stash pop &&
	test_path_is_file ignored.d/foo
'

test_expect_success 'stash -u -- <untracked> doesnt print error' '
	>untracked &&
	git stash push -u -- untracked 2>actual &&
	test_path_is_missing untracked &&
	test_line_count = 0 actual
'

test_expect_success 'stash -u -- <untracked> leaves rest of working tree in place' '
	>tracked &&
	git add tracked &&
	>untracked &&
	git stash push -u -- untracked &&
	test_path_is_missing untracked &&
	test_path_is_file tracked
'

test_expect_success 'stash -u -- <tracked> <untracked> clears changes in both' '
	>tracked &&
	git add tracked &&
	>untracked &&
	git stash push -u -- tracked untracked &&
	test_path_is_missing tracked &&
	test_path_is_missing untracked
'

test_expect_success 'stash --all -- <ignored> stashes ignored file' '
	>ignored.d/bar &&
	git stash push --all -- ignored.d/bar &&
	test_path_is_missing ignored.d/bar
'

test_expect_success 'stash --all -- <tracked> <ignored> clears changes in both' '
	>tracked &&
	git add tracked &&
	>ignored.d/bar &&
	git stash push --all -- tracked ignored.d/bar &&
	test_path_is_missing tracked &&
	test_path_is_missing ignored.d/bar
'

test_expect_success 'stash -u -- <ignored> leaves ignored file alone' '
	>ignored.d/bar &&
	git stash push -u -- ignored.d/bar &&
	test_path_is_file ignored.d/bar
'

test_expect_success 'stash -u -- <non-existent> shows no changes when there are none' '
	git stash push -u -- non-existent >actual &&
	echo "No local changes to save" >expect &&
	test_cmp expect actual
'

test_expect_success 'stash -u with globs' '
	>untracked.txt &&
	git stash -u -- ":(glob)**/*.txt" &&
	test_path_is_missing untracked.txt
'

test_expect_success 'stash show --include-untracked shows untracked files' '
	git reset --hard &&
	git clean -xf &&
	>untracked &&
	>tracked &&
	git add tracked &&
	empty_blob_oid=$(git rev-parse --short :tracked) &&
	git stash -u &&

	cat >expect <<-EOF &&
	 tracked   | 0
	 untracked | 0
	 2 files changed, 0 insertions(+), 0 deletions(-)
	EOF
	git stash show --include-untracked >actual &&
	test_cmp expect actual &&
	git stash show -u >actual &&
	test_cmp expect actual &&
	git stash show --no-include-untracked --include-untracked >actual &&
	test_cmp expect actual &&
	git stash show --only-untracked --include-untracked >actual &&
	test_cmp expect actual &&
	git -c stash.showIncludeUntracked=true stash show >actual &&
	test_cmp expect actual &&

	cat >expect <<-EOF &&
	diff --git a/tracked b/tracked
	new file mode 100644
	index 0000000..$empty_blob_oid
	diff --git a/untracked b/untracked
	new file mode 100644
	index 0000000..$empty_blob_oid
	EOF
	git stash show -p --include-untracked >actual &&
	test_cmp expect actual &&
	git stash show --include-untracked -p >actual &&
	test_cmp expect actual &&
	git -c stash.showIncludeUntracked=true stash show -p >actual &&
	test_cmp expect actual
'

test_expect_success 'stash show --only-untracked only shows untracked files' '
	git reset --hard &&
	git clean -xf &&
	>untracked &&
	>tracked &&
	git add tracked &&
	empty_blob_oid=$(git rev-parse --short :tracked) &&
	git stash -u &&

	cat >expect <<-EOF &&
	 untracked | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	git stash show --only-untracked >actual &&
	test_cmp expect actual &&
	git stash show --no-include-untracked --only-untracked >actual &&
	test_cmp expect actual &&
	git stash show --include-untracked --only-untracked >actual &&
	test_cmp expect actual &&

	cat >expect <<-EOF &&
	diff --git a/untracked b/untracked
	new file mode 100644
	index 0000000..$empty_blob_oid
	EOF
	git stash show -p --only-untracked >actual &&
	test_cmp expect actual &&
	git stash show --only-untracked -p >actual &&
	test_cmp expect actual
'

test_expect_success 'stash show --no-include-untracked cancels --{include,only}-untracked' '
	git reset --hard &&
	git clean -xf &&
	>untracked &&
	>tracked &&
	git add tracked &&
	git stash -u &&

	cat >expect <<-EOF &&
	 tracked | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	git stash show --only-untracked --no-include-untracked >actual &&
	test_cmp expect actual &&
	git stash show --include-untracked --no-include-untracked >actual &&
	test_cmp expect actual
'

test_expect_success 'stash show --include-untracked errors on duplicate files' '
	git reset --hard &&
	git clean -xf &&
	>tracked &&
	git add tracked &&
	tree=$(git write-tree) &&
	i_commit=$(git commit-tree -p HEAD -m "index on any-branch" "$tree") &&
	test_when_finished "rm -f untracked_index" &&
	u_commit=$(
		GIT_INDEX_FILE="untracked_index" &&
		export GIT_INDEX_FILE &&
		git update-index --add tracked &&
		u_tree=$(git write-tree) &&
		git commit-tree -m "untracked files on any-branch" "$u_tree"
	) &&
	w_commit=$(git commit-tree -p HEAD -p "$i_commit" -p "$u_commit" -m "WIP on any-branch" "$tree") &&
	test_must_fail git stash show --include-untracked "$w_commit" 2>err &&
	test_grep "worktree and untracked commit have duplicate entries: tracked" err
'

test_expect_success 'stash show --{include,only}-untracked on stashes without untracked entries' '
	git reset --hard &&
	git clean -xf &&
	>tracked &&
	git add tracked &&
	git stash &&

	git stash show >expect &&
	git stash show --include-untracked >actual &&
	test_cmp expect actual &&

	git stash show --only-untracked >actual &&
	test_must_be_empty actual
'

test_expect_success 'stash -u ignores sub-repository' '
	test_when_finished "rm -rf sub-repo" &&
	git init sub-repo &&
	git stash -u
'

test_done
