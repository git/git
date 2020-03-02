#!/bin/sh
#
# Copyright (c) 2007 Johannes E Schindelin
#

test_description='Test git stash'

. ./test-lib.sh

diff_cmp () {
	for i in "$1" "$2"
	do
		sed -e 's/^index 0000000\.\.[0-9a-f]*/index 0000000..1234567/' \
		-e 's/^index [0-9a-f]*\.\.[0-9a-f]*/index 1234567..89abcde/' \
		-e 's/^index [0-9a-f]*,[0-9a-f]*\.\.[0-9a-f]*/index 1234567,7654321..89abcde/' \
		"$i" >"$i.compare" || return 1
	done &&
	test_cmp "$1.compare" "$2.compare" &&
	rm -f "$1.compare" "$2.compare"
}

test_expect_success 'stash some dirty working directory' '
	echo 1 >file &&
	git add file &&
	echo unrelated >other-file &&
	git add other-file &&
	test_tick &&
	git commit -m initial &&
	echo 2 >file &&
	git add file &&
	echo 3 >file &&
	test_tick &&
	git stash &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD
'

cat >expect <<EOF
diff --git a/file b/file
index 0cfbf08..00750ed 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-2
+3
EOF

test_expect_success 'parents of stash' '
	test $(git rev-parse stash^) = $(git rev-parse HEAD) &&
	git diff stash^2..stash >output &&
	diff_cmp expect output
'

test_expect_success 'applying bogus stash does nothing' '
	test_must_fail git stash apply stash@{1} &&
	echo 1 >expect &&
	test_cmp expect file
'

test_expect_success 'apply does not need clean working directory' '
	echo 4 >other-file &&
	git stash apply &&
	echo 3 >expect &&
	test_cmp expect file
'

test_expect_success 'apply does not clobber working directory changes' '
	git reset --hard &&
	echo 4 >file &&
	test_must_fail git stash apply &&
	echo 4 >expect &&
	test_cmp expect file
'

test_expect_success 'apply stashed changes' '
	git reset --hard &&
	echo 5 >other-file &&
	git add other-file &&
	test_tick &&
	git commit -m other-file &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'apply stashed changes (including index)' '
	git reset --hard HEAD^ &&
	echo 6 >other-file &&
	git add other-file &&
	test_tick &&
	git commit -m other-file &&
	git stash apply --index &&
	test 3 = $(cat file) &&
	test 2 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'unstashing in a subdirectory' '
	git reset --hard HEAD &&
	mkdir subdir &&
	(
		cd subdir &&
		git stash apply
	)
'

test_expect_success 'stash drop complains of extra options' '
	test_must_fail git stash drop --foo
'

test_expect_success 'drop top stash' '
	git reset --hard &&
	git stash list >expected &&
	echo 7 >file &&
	git stash &&
	git stash drop &&
	git stash list >actual &&
	test_cmp expected actual &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'drop middle stash' '
	git reset --hard &&
	echo 8 >file &&
	git stash &&
	echo 9 >file &&
	git stash &&
	git stash drop stash@{1} &&
	test 2 = $(git stash list | wc -l) &&
	git stash apply &&
	test 9 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file) &&
	git reset --hard &&
	git stash drop &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'drop middle stash by index' '
	git reset --hard &&
	echo 8 >file &&
	git stash &&
	echo 9 >file &&
	git stash &&
	git stash drop 1 &&
	test 2 = $(git stash list | wc -l) &&
	git stash apply &&
	test 9 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file) &&
	git reset --hard &&
	git stash drop &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'stash pop' '
	git reset --hard &&
	git stash pop &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file) &&
	test 0 = $(git stash list | wc -l)
'

cat >expect <<EOF
diff --git a/file2 b/file2
new file mode 100644
index 0000000..1fe912c
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+bar2
EOF

cat >expect1 <<EOF
diff --git a/file b/file
index 257cc56..5716ca5 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-foo
+bar
EOF

cat >expect2 <<EOF
diff --git a/file b/file
index 7601807..5716ca5 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-baz
+bar
diff --git a/file2 b/file2
new file mode 100644
index 0000000..1fe912c
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+bar2
EOF

test_expect_success 'stash branch' '
	echo foo >file &&
	git commit file -m first &&
	echo bar >file &&
	echo bar2 >file2 &&
	git add file2 &&
	git stash &&
	echo baz >file &&
	git commit file -m second &&
	git stash branch stashbranch &&
	test refs/heads/stashbranch = $(git symbolic-ref HEAD) &&
	test $(git rev-parse HEAD) = $(git rev-parse master^) &&
	git diff --cached >output &&
	diff_cmp expect output &&
	git diff >output &&
	diff_cmp expect1 output &&
	git add file &&
	git commit -m alternate\ second &&
	git diff master..stashbranch >output &&
	diff_cmp output expect2 &&
	test 0 = $(git stash list | wc -l)
'

test_expect_success 'apply -q is quiet' '
	echo foo >file &&
	git stash &&
	git stash apply -q >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'save -q is quiet' '
	git stash save --quiet >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'pop -q works and is quiet' '
	git stash pop -q >output.out 2>&1 &&
	echo bar >expect &&
	git show :file >actual &&
	test_cmp expect actual &&
	test_must_be_empty output.out
'

test_expect_success 'pop -q --index works and is quiet' '
	echo foo >file &&
	git add file &&
	git stash save --quiet &&
	git stash pop -q --index >output.out 2>&1 &&
	git diff-files file2 >file2.diff &&
	test_must_be_empty file2.diff &&
	test foo = "$(git show :file)" &&
	test_must_be_empty output.out
'

test_expect_success 'drop -q is quiet' '
	git stash &&
	git stash drop -q >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'stash -k' '
	echo bar3 >file &&
	echo bar4 >file2 &&
	git add file2 &&
	git stash -k &&
	test bar,bar4 = $(cat file),$(cat file2)
'

test_expect_success 'stash --no-keep-index' '
	echo bar33 >file &&
	echo bar44 >file2 &&
	git add file2 &&
	git stash --no-keep-index &&
	test bar,bar2 = $(cat file),$(cat file2)
'

test_expect_success 'dont assume push with non-option args' '
	test_must_fail git stash -q drop 2>err &&
	test_i18ngrep -e "subcommand wasn'\''t specified; '\''push'\'' can'\''t be assumed due to unexpected token '\''drop'\''" err
'

test_expect_success 'stash --invalid-option' '
	echo bar5 >file &&
	echo bar6 >file2 &&
	git add file2 &&
	test_must_fail git stash --invalid-option &&
	test_must_fail git stash save --invalid-option &&
	test bar5,bar6 = $(cat file),$(cat file2)
'

test_expect_success 'stash an added file' '
	git reset --hard &&
	echo new >file3 &&
	git add file3 &&
	git stash save "added file" &&
	! test -r file3 &&
	git stash apply &&
	test new = "$(cat file3)"
'

test_expect_success 'stash --intent-to-add file' '
	git reset --hard &&
	echo new >file4 &&
	git add --intent-to-add file4 &&
	test_when_finished "git rm -f file4" &&
	test_must_fail git stash
'

test_expect_success 'stash rm then recreate' '
	git reset --hard &&
	git rm file &&
	echo bar7 >file &&
	git stash save "rm then recreate" &&
	test bar = "$(cat file)" &&
	git stash apply &&
	test bar7 = "$(cat file)"
'

test_expect_success 'stash rm and ignore' '
	git reset --hard &&
	git rm file &&
	echo file >.gitignore &&
	git stash save "rm and ignore" &&
	test bar = "$(cat file)" &&
	test file = "$(cat .gitignore)" &&
	git stash apply &&
	! test -r file &&
	test file = "$(cat .gitignore)"
'

test_expect_success 'stash rm and ignore (stage .gitignore)' '
	git reset --hard &&
	git rm file &&
	echo file >.gitignore &&
	git add .gitignore &&
	git stash save "rm and ignore (stage .gitignore)" &&
	test bar = "$(cat file)" &&
	! test -r .gitignore &&
	git stash apply &&
	! test -r file &&
	test file = "$(cat .gitignore)"
'

test_expect_success SYMLINKS 'stash file to symlink' '
	git reset --hard &&
	rm file &&
	ln -s file2 file &&
	git stash save "file to symlink" &&
	test -f file &&
	test bar = "$(cat file)" &&
	git stash apply &&
	case "$(ls -l file)" in *" file -> file2") :;; *) false;; esac
'

test_expect_success SYMLINKS 'stash file to symlink (stage rm)' '
	git reset --hard &&
	git rm file &&
	ln -s file2 file &&
	git stash save "file to symlink (stage rm)" &&
	test -f file &&
	test bar = "$(cat file)" &&
	git stash apply &&
	case "$(ls -l file)" in *" file -> file2") :;; *) false;; esac
'

test_expect_success SYMLINKS 'stash file to symlink (full stage)' '
	git reset --hard &&
	rm file &&
	ln -s file2 file &&
	git add file &&
	git stash save "file to symlink (full stage)" &&
	test -f file &&
	test bar = "$(cat file)" &&
	git stash apply &&
	case "$(ls -l file)" in *" file -> file2") :;; *) false;; esac
'

# This test creates a commit with a symlink used for the following tests

test_expect_success 'stash symlink to file' '
	git reset --hard &&
	test_ln_s_add file filelink &&
	git commit -m "Add symlink" &&
	rm filelink &&
	cp file filelink &&
	git stash save "symlink to file"
'

test_expect_success SYMLINKS 'this must have re-created the symlink' '
	test -h filelink &&
	case "$(ls -l filelink)" in *" filelink -> file") :;; *) false;; esac
'

test_expect_success 'unstash must re-create the file' '
	git stash apply &&
	! test -h filelink &&
	test bar = "$(cat file)"
'

test_expect_success 'stash symlink to file (stage rm)' '
	git reset --hard &&
	git rm filelink &&
	cp file filelink &&
	git stash save "symlink to file (stage rm)"
'

test_expect_success SYMLINKS 'this must have re-created the symlink' '
	test -h filelink &&
	case "$(ls -l filelink)" in *" filelink -> file") :;; *) false;; esac
'

test_expect_success 'unstash must re-create the file' '
	git stash apply &&
	! test -h filelink &&
	test bar = "$(cat file)"
'

test_expect_success 'stash symlink to file (full stage)' '
	git reset --hard &&
	rm filelink &&
	cp file filelink &&
	git add filelink &&
	git stash save "symlink to file (full stage)"
'

test_expect_success SYMLINKS 'this must have re-created the symlink' '
	test -h filelink &&
	case "$(ls -l filelink)" in *" filelink -> file") :;; *) false;; esac
'

test_expect_success 'unstash must re-create the file' '
	git stash apply &&
	! test -h filelink &&
	test bar = "$(cat file)"
'

test_expect_failure 'stash directory to file' '
	git reset --hard &&
	mkdir dir &&
	echo foo >dir/file &&
	git add dir/file &&
	git commit -m "Add file in dir" &&
	rm -fr dir &&
	echo bar >dir &&
	git stash save "directory to file" &&
	test -d dir &&
	test foo = "$(cat dir/file)" &&
	test_must_fail git stash apply &&
	test bar = "$(cat dir)" &&
	git reset --soft HEAD^
'

test_expect_failure 'stash file to directory' '
	git reset --hard &&
	rm file &&
	mkdir file &&
	echo foo >file/file &&
	git stash save "file to directory" &&
	test -f file &&
	test bar = "$(cat file)" &&
	git stash apply &&
	test -f file/file &&
	test foo = "$(cat file/file)"
'

test_expect_success 'giving too many ref arguments does not modify files' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	echo foo >file2 &&
	git stash &&
	echo bar >file2 &&
	git stash &&
	test-tool chmtime =123456789 file2 &&
	for type in apply pop "branch stash-branch"
	do
		test_must_fail git stash $type stash@{0} stash@{1} 2>err &&
		test_i18ngrep "Too many revisions" err &&
		test 123456789 = $(test-tool chmtime -g file2) || return 1
	done
'

test_expect_success 'drop: too many arguments errors out (does nothing)' '
	git stash list >expect &&
	test_must_fail git stash drop stash@{0} stash@{1} 2>err &&
	test_i18ngrep "Too many revisions" err &&
	git stash list >actual &&
	test_cmp expect actual
'

test_expect_success 'show: too many arguments errors out (does nothing)' '
	test_must_fail git stash show stash@{0} stash@{1} 2>err 1>out &&
	test_i18ngrep "Too many revisions" err &&
	test_must_be_empty out
'

test_expect_success 'stash create - no changes' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	git stash create >actual &&
	test_must_be_empty actual
'

test_expect_success 'stash branch - no stashes on stack, stash-like argument' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	git stash branch stash-branch ${STASH_ID} &&
	test_when_finished "git reset --hard HEAD && git checkout master &&
	git branch -D stash-branch" &&
	test $(git ls-files --modified | wc -l) -eq 1
'

test_expect_success 'stash branch - stashes on stack, stash-like argument' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	git stash &&
	test_when_finished "git stash drop" &&
	echo bar >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	git stash branch stash-branch ${STASH_ID} &&
	test_when_finished "git reset --hard HEAD && git checkout master &&
	git branch -D stash-branch" &&
	test $(git ls-files --modified | wc -l) -eq 1
'

test_expect_success 'stash branch complains with no arguments' '
	test_must_fail git stash branch 2>err &&
	test_i18ngrep "No branch name specified" err
'

test_expect_success 'stash show format defaults to --stat' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	git stash &&
	test_when_finished "git stash drop" &&
	echo bar >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	cat >expected <<-EOF &&
	 file | 1 +
	 1 file changed, 1 insertion(+)
	EOF
	git stash show ${STASH_ID} >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'stash show - stashes on stack, stash-like argument' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	git stash &&
	test_when_finished "git stash drop" &&
	echo bar >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	echo "1	0	file" >expected &&
	git stash show --numstat ${STASH_ID} >actual &&
	test_cmp expected actual
'

test_expect_success 'stash show -p - stashes on stack, stash-like argument' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	git stash &&
	test_when_finished "git stash drop" &&
	echo bar >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	cat >expected <<-EOF &&
	diff --git a/file b/file
	index 7601807..935fbd3 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baz
	+bar
	EOF
	git stash show -p ${STASH_ID} >actual &&
	diff_cmp expected actual
'

test_expect_success 'stash show - no stashes on stack, stash-like argument' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	echo "1	0	file" >expected &&
	git stash show --numstat ${STASH_ID} >actual &&
	test_cmp expected actual
'

test_expect_success 'stash show -p - no stashes on stack, stash-like argument' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD" &&
	git reset --hard &&
	echo foo >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	cat >expected <<-EOF &&
	diff --git a/file b/file
	index 7601807..71b52c4 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baz
	+foo
	EOF
	git stash show -p ${STASH_ID} >actual &&
	diff_cmp expected actual
'

test_expect_success 'stash show --patience shows diff' '
	git reset --hard &&
	echo foo >>file &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	cat >expected <<-EOF &&
	diff --git a/file b/file
	index 7601807..71b52c4 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baz
	+foo
	EOF
	git stash show --patience ${STASH_ID} >actual &&
	diff_cmp expected actual
'

test_expect_success 'drop: fail early if specified stash is not a stash ref' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD && git stash clear" &&
	git reset --hard &&
	echo foo >file &&
	git stash &&
	echo bar >file &&
	git stash &&
	test_must_fail git stash drop $(git rev-parse stash@{0}) &&
	git stash pop &&
	test bar = "$(cat file)" &&
	git reset --hard HEAD
'

test_expect_success 'pop: fail early if specified stash is not a stash ref' '
	git stash clear &&
	test_when_finished "git reset --hard HEAD && git stash clear" &&
	git reset --hard &&
	echo foo >file &&
	git stash &&
	echo bar >file &&
	git stash &&
	test_must_fail git stash pop $(git rev-parse stash@{0}) &&
	git stash pop &&
	test bar = "$(cat file)" &&
	git reset --hard HEAD
'

test_expect_success 'ref with non-existent reflog' '
	git stash clear &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	git add file2 &&
	git stash &&
	test_must_fail git rev-parse --quiet --verify does-not-exist &&
	test_must_fail git stash drop does-not-exist &&
	test_must_fail git stash drop does-not-exist@{0} &&
	test_must_fail git stash pop does-not-exist &&
	test_must_fail git stash pop does-not-exist@{0} &&
	test_must_fail git stash apply does-not-exist &&
	test_must_fail git stash apply does-not-exist@{0} &&
	test_must_fail git stash show does-not-exist &&
	test_must_fail git stash show does-not-exist@{0} &&
	test_must_fail git stash branch tmp does-not-exist &&
	test_must_fail git stash branch tmp does-not-exist@{0} &&
	git stash drop
'

test_expect_success 'invalid ref of the form stash@{n}, n >= N' '
	git stash clear &&
	test_must_fail git stash drop stash@{0} &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	git add file2 &&
	git stash &&
	test_must_fail git stash drop stash@{1} &&
	test_must_fail git stash pop stash@{1} &&
	test_must_fail git stash apply stash@{1} &&
	test_must_fail git stash show stash@{1} &&
	test_must_fail git stash branch tmp stash@{1} &&
	git stash drop
'

test_expect_success 'invalid ref of the form "n", n >= N' '
	git stash clear &&
	test_must_fail git stash drop 0 &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	git add file2 &&
	git stash &&
	test_must_fail git stash drop 1 &&
	test_must_fail git stash pop 1 &&
	test_must_fail git stash apply 1 &&
	test_must_fail git stash show 1 &&
	test_must_fail git stash branch tmp 1 &&
	git stash drop
'

test_expect_success 'valid ref of the form "n", n < N' '
	git stash clear &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	git add file2 &&
	git stash &&
	git stash show 0 &&
	git stash branch tmp 0 &&
	git checkout master &&
	git stash &&
	git stash apply 0 &&
	git reset --hard &&
	git stash pop 0 &&
	git stash &&
	git stash drop 0 &&
	test_must_fail git stash drop
'

test_expect_success 'branch: do not drop the stash if the branch exists' '
	git stash clear &&
	echo foo >file &&
	git add file &&
	git commit -m initial &&
	echo bar >file &&
	git stash &&
	test_must_fail git stash branch master stash@{0} &&
	git rev-parse stash@{0} --
'

test_expect_success 'branch: should not drop the stash if the apply fails' '
	git stash clear &&
	git reset HEAD~1 --hard &&
	echo foo >file &&
	git add file &&
	git commit -m initial &&
	echo bar >file &&
	git stash &&
	echo baz >file &&
	test_when_finished "git checkout master" &&
	test_must_fail git stash branch new_branch stash@{0} &&
	git rev-parse stash@{0} --
'

test_expect_success 'apply: show same status as git status (relative to ./)' '
	git stash clear &&
	echo 1 >subdir/subfile1 &&
	echo 2 >subdir/subfile2 &&
	git add subdir/subfile1 &&
	git commit -m subdir &&
	(
		cd subdir &&
		echo x >subfile1 &&
		echo x >../file &&
		git status >../expect &&
		git stash &&
		sane_unset GIT_MERGE_VERBOSITY &&
		git stash apply
	) |
	sed -e 1d >actual && # drop "Saved..."
	test_i18ncmp expect actual
'

cat >expect <<EOF
diff --git a/HEAD b/HEAD
new file mode 100644
index 0000000..fe0cbee
--- /dev/null
+++ b/HEAD
@@ -0,0 +1 @@
+file-not-a-ref
EOF

test_expect_success 'stash where working directory contains "HEAD" file' '
	git stash clear &&
	git reset --hard &&
	echo file-not-a-ref >HEAD &&
	git add HEAD &&
	test_tick &&
	git stash &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD &&
	test "$(git rev-parse stash^)" = "$(git rev-parse HEAD)" &&
	git diff stash^..stash >output &&
	diff_cmp expect output
'

test_expect_success 'store called with invalid commit' '
	test_must_fail git stash store foo
'

test_expect_success 'store updates stash ref and reflog' '
	git stash clear &&
	git reset --hard &&
	echo quux >bazzy &&
	git add bazzy &&
	STASH_ID=$(git stash create) &&
	git reset --hard &&
	test_path_is_missing bazzy &&
	git stash store -m quuxery $STASH_ID &&
	test $(git rev-parse stash) = $STASH_ID &&
	git reflog --format=%H stash| grep $STASH_ID &&
	git stash pop &&
	grep quux bazzy
'

test_expect_success 'handle stash specification with spaces' '
	git stash clear &&
	echo pig >file &&
	git stash &&
	stamp=$(git log -g --format="%cd" -1 refs/stash) &&
	test_tick &&
	echo cow >file &&
	git stash &&
	git stash apply "stash@{$stamp}" &&
	grep pig file
'

test_expect_success 'setup stash with index and worktree changes' '
	git stash clear &&
	git reset --hard &&
	echo index >file &&
	git add file &&
	echo working >file &&
	git stash
'

test_expect_success 'stash list implies --first-parent -m' '
	cat >expect <<-EOF &&
	stash@{0}

	diff --git a/file b/file
	index 257cc56..d26b33d 100644
	--- a/file
	+++ b/file
	@@ -1 +1 @@
	-foo
	+working
	EOF
	git stash list --format=%gd -p >actual &&
	diff_cmp expect actual
'

test_expect_success 'stash list --cc shows combined diff' '
	cat >expect <<-\EOF &&
	stash@{0}

	diff --cc file
	index 257cc56,9015a7a..d26b33d
	--- a/file
	+++ b/file
	@@@ -1,1 -1,1 +1,1 @@@
	- foo
	 -index
	++working
	EOF
	git stash list --format=%gd -p --cc >actual &&
	diff_cmp expect actual
'

test_expect_success 'stash is not confused by partial renames' '
	mv file renamed &&
	git add renamed &&
	git stash &&
	git stash apply &&
	test_path_is_file renamed &&
	test_path_is_missing file
'

test_expect_success 'push -m shows right message' '
	>foo &&
	git add foo &&
	git stash push -m "test message" &&
	echo "stash@{0}: On master: test message" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push -m also works without space' '
	>foo &&
	git add foo &&
	git stash push -m"unspaced test message" &&
	echo "stash@{0}: On master: unspaced test message" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store -m foo shows right message' '
	git stash clear &&
	git reset --hard &&
	echo quux >bazzy &&
	git add bazzy &&
	STASH_ID=$(git stash create) &&
	git stash store -m "store m" $STASH_ID &&
	echo "stash@{0}: store m" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store -mfoo shows right message' '
	git stash clear &&
	git reset --hard &&
	echo quux >bazzy &&
	git add bazzy &&
	STASH_ID=$(git stash create) &&
	git stash store -m"store mfoo" $STASH_ID &&
	echo "stash@{0}: store mfoo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store --message=foo shows right message' '
	git stash clear &&
	git reset --hard &&
	echo quux >bazzy &&
	git add bazzy &&
	STASH_ID=$(git stash create) &&
	git stash store --message="store message=foo" $STASH_ID &&
	echo "stash@{0}: store message=foo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store --message foo shows right message' '
	git stash clear &&
	git reset --hard &&
	echo quux >bazzy &&
	git add bazzy &&
	STASH_ID=$(git stash create) &&
	git stash store --message "store message foo" $STASH_ID &&
	echo "stash@{0}: store message foo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push -mfoo uses right message' '
	>foo &&
	git add foo &&
	git stash push -m"test mfoo" &&
	echo "stash@{0}: On master: test mfoo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push --message foo is synonym for -mfoo' '
	>foo &&
	git add foo &&
	git stash push --message "test message foo" &&
	echo "stash@{0}: On master: test message foo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push --message=foo is synonym for -mfoo' '
	>foo &&
	git add foo &&
	git stash push --message="test message=foo" &&
	echo "stash@{0}: On master: test message=foo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push -m shows right message' '
	>foo &&
	git add foo &&
	git stash push -m "test m foo" &&
	echo "stash@{0}: On master: test m foo" >expect &&
	git stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create stores correct message' '
	>foo &&
	git add foo &&
	STASH_ID=$(git stash create "create test message") &&
	echo "On master: create test message" >expect &&
	git show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'create with multiple arguments for the message' '
	>foo &&
	git add foo &&
	STASH_ID=$(git stash create test untracked) &&
	echo "On master: test untracked" >expect &&
	git show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'create in a detached state' '
	test_when_finished "git checkout master" &&
	git checkout HEAD~1 &&
	>foo &&
	git add foo &&
	STASH_ID=$(git stash create) &&
	HEAD_ID=$(git rev-parse --short HEAD) &&
	echo "WIP on (no branch): ${HEAD_ID} initial" >expect &&
	git show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'stash -- <pathspec> stashes and restores the file' '
	>foo &&
	>bar &&
	git add foo bar &&
	git stash push -- foo &&
	test_path_is_file bar &&
	test_path_is_missing foo &&
	git stash pop &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash -- <pathspec> stashes in subdirectory' '
	mkdir sub &&
	>foo &&
	>bar &&
	git add foo bar &&
	(
		cd sub &&
		git stash push -- ../foo
	) &&
	test_path_is_file bar &&
	test_path_is_missing foo &&
	git stash pop &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash with multiple pathspec arguments' '
	>foo &&
	>bar &&
	>extra &&
	git add foo bar extra &&
	git stash push -- foo bar &&
	test_path_is_missing bar &&
	test_path_is_missing foo &&
	test_path_is_file extra &&
	git stash pop &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	test_path_is_file extra
'

test_expect_success 'stash with file including $IFS character' '
	>"foo bar" &&
	>foo &&
	>bar &&
	git add foo* &&
	git stash push -- "foo b*" &&
	test_path_is_missing "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	git stash pop &&
	test_path_is_file "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash with pathspec matching multiple paths' '
       echo original >file &&
       echo original >other-file &&
       git commit -m "two" file other-file &&
       echo modified >file &&
       echo modified >other-file &&
       git stash push -- "*file" &&
       echo original >expect &&
       test_cmp expect file &&
       test_cmp expect other-file &&
       git stash pop &&
       echo modified >expect &&
       test_cmp expect file &&
       test_cmp expect other-file
'

test_expect_success 'stash push -p with pathspec shows no changes only once' '
	>foo &&
	git add foo &&
	git commit -m "tmp" &&
	git stash push -p foo >actual &&
	echo "No local changes to save" >expect &&
	git reset --hard HEAD~ &&
	test_i18ncmp expect actual
'

test_expect_success 'push <pathspec>: show no changes when there are none' '
	>foo &&
	git add foo &&
	git commit -m "tmp" &&
	git stash push foo >actual &&
	echo "No local changes to save" >expect &&
	git reset --hard HEAD~ &&
	test_i18ncmp expect actual
'

test_expect_success 'push: <pathspec> not in the repository errors out' '
	>untracked &&
	test_must_fail git stash push untracked &&
	test_path_is_file untracked
'

test_expect_success 'push: -q is quiet with changes' '
	>foo &&
	git add foo &&
	git stash push -q >output 2>&1 &&
	test_must_be_empty output
'

test_expect_success 'push: -q is quiet with no changes' '
	git stash push -q >output 2>&1 &&
	test_must_be_empty output
'

test_expect_success 'push: -q is quiet even if there is no initial commit' '
	git init foo_dir &&
	test_when_finished rm -rf foo_dir &&
	(
		cd foo_dir &&
		>bar &&
		test_must_fail git stash push -q >output 2>&1 &&
		test_must_be_empty output
	)
'

test_expect_success 'untracked files are left in place when -u is not given' '
	>file &&
	git add file &&
	>untracked &&
	git stash push file &&
	test_path_is_file untracked
'

test_expect_success 'stash without verb with pathspec' '
	>"foo bar" &&
	>foo &&
	>bar &&
	git add foo* &&
	git stash -- "foo b*" &&
	test_path_is_missing "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	git stash pop &&
	test_path_is_file "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash -k -- <pathspec> leaves unstaged files intact' '
	git reset &&
	>foo &&
	>bar &&
	git add foo bar &&
	git commit -m "test" &&
	echo "foo" >foo &&
	echo "bar" >bar &&
	git stash -k -- foo &&
	test "",bar = $(cat foo),$(cat bar) &&
	git stash pop &&
	test foo,bar = $(cat foo),$(cat bar)
'

test_expect_success 'stash -- <subdir> leaves untracked files in subdir intact' '
	git reset &&
	>subdir/untracked &&
	>subdir/tracked1 &&
	>subdir/tracked2 &&
	git add subdir/tracked* &&
	git stash -- subdir/ &&
	test_path_is_missing subdir/tracked1 &&
	test_path_is_missing subdir/tracked2 &&
	test_path_is_file subdir/untracked &&
	git stash pop &&
	test_path_is_file subdir/tracked1 &&
	test_path_is_file subdir/tracked2 &&
	test_path_is_file subdir/untracked
'

test_expect_success 'stash -- <subdir> works with binary files' '
	git reset &&
	>subdir/untracked &&
	>subdir/tracked &&
	cp "$TEST_DIRECTORY"/test-binary-1.png subdir/tracked-binary &&
	git add subdir/tracked* &&
	git stash -- subdir/ &&
	test_path_is_missing subdir/tracked &&
	test_path_is_missing subdir/tracked-binary &&
	test_path_is_file subdir/untracked &&
	git stash pop &&
	test_path_is_file subdir/tracked &&
	test_path_is_file subdir/tracked-binary &&
	test_path_is_file subdir/untracked
'

test_expect_success 'stash with user.name and user.email set works' '
	test_config user.name "A U Thor" &&
	test_config user.email "a.u@thor" &&
	git stash
'

test_expect_success 'stash works when user.name and user.email are not set' '
	git reset &&
	>1 &&
	git add 1 &&
	echo "$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" >expect &&
	git stash &&
	git show -s --format="%an <%ae>" refs/stash >actual &&
	test_cmp expect actual &&
	>2 &&
	git add 2 &&
	test_config user.useconfigonly true &&
	test_config stash.usebuiltin true &&
	(
		sane_unset GIT_AUTHOR_NAME &&
		sane_unset GIT_AUTHOR_EMAIL &&
		sane_unset GIT_COMMITTER_NAME &&
		sane_unset GIT_COMMITTER_EMAIL &&
		test_unconfig user.email &&
		test_unconfig user.name &&
		test_must_fail git commit -m "should fail" &&
		echo "git stash <git@stash>" >expect &&
		>2 &&
		git stash &&
		git show -s --format="%an <%ae>" refs/stash >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'stash --keep-index with file deleted in index does not resurrect it on disk' '
	test_commit to-remove to-remove &&
	git rm to-remove &&
	git stash --keep-index &&
	test_path_is_missing to-remove
'

test_expect_success 'stash apply should succeed with unmodified file' '
	echo base >file &&
	git add file &&
	git commit -m base &&

	# now stash a modification
	echo modified >file &&
	git stash &&

	# make the file stat dirty
	cp file other &&
	mv other file &&

	git stash apply
'

test_expect_success 'stash handles skip-worktree entries nicely' '
	test_commit A &&
	echo changed >A.t &&
	git add A.t &&
	git update-index --skip-worktree A.t &&
	rm A.t &&
	git stash &&

	git rev-parse --verify refs/stash:A.t
'

test_expect_success 'stash -c stash.useBuiltin=false warning ' '
	expected="stash.useBuiltin support has been removed" &&

	git -c stash.useBuiltin=false stash 2>err &&
	test_i18ngrep "$expected" err &&
	env GIT_TEST_STASH_USE_BUILTIN=false git stash 2>err &&
	test_i18ngrep "$expected" err &&

	git -c stash.useBuiltin=true stash 2>err &&
	test_must_be_empty err &&
	env GIT_TEST_STASH_USE_BUILTIN=true git stash 2>err &&
	test_must_be_empty err
'

test_done
