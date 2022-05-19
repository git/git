#!/bin/sh
#
# Copyright (c) 2007 Johannes E Schindelin
#

test_description='Test but stash'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'usage on cmd and subcommand invalid option' '
	test_expect_code 129 but stash --invalid-option 2>usage &&
	grep "or: but stash" usage &&

	test_expect_code 129 but stash push --invalid-option 2>usage &&
	! grep "or: but stash" usage
'

test_expect_success 'usage on main command -h emits a summary of subcommands' '
	test_expect_code 129 but stash -h >usage &&
	grep -F "usage: but stash list" usage &&
	grep -F "or: but stash show" usage
'

test_expect_failure 'usage for subcommands should emit subcommand usage' '
	test_expect_code 129 but stash push -h >usage &&
	grep -F "usage: but stash [push" usage
'

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

setup_stash() {
	echo 1 >file &&
	but add file &&
	echo unrelated >other-file &&
	but add other-file &&
	test_tick &&
	but cummit -m initial &&
	echo 2 >file &&
	but add file &&
	echo 3 >file &&
	test_tick &&
	but stash &&
	but diff-files --quiet &&
	but diff-index --cached --quiet HEAD
}

test_expect_success 'stash some dirty working directory' '
	setup_stash
'

cat >expect <<EOF
diff --but a/file b/file
index 0cfbf08..00750ed 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-2
+3
EOF

test_expect_success 'parents of stash' '
	test $(but rev-parse stash^) = $(but rev-parse HEAD) &&
	but diff stash^2..stash >output &&
	diff_cmp expect output
'

test_expect_success 'applying bogus stash does nothing' '
	test_must_fail but stash apply stash@{1} &&
	echo 1 >expect &&
	test_cmp expect file
'

test_expect_success 'apply does not need clean working directory' '
	echo 4 >other-file &&
	but stash apply &&
	echo 3 >expect &&
	test_cmp expect file
'

test_expect_success 'apply does not clobber working directory changes' '
	but reset --hard &&
	echo 4 >file &&
	test_must_fail but stash apply &&
	echo 4 >expect &&
	test_cmp expect file
'

test_expect_success 'apply stashed changes' '
	but reset --hard &&
	echo 5 >other-file &&
	but add other-file &&
	test_tick &&
	but cummit -m other-file &&
	but stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file)
'

test_expect_success 'apply stashed changes (including index)' '
	but reset --hard HEAD^ &&
	echo 6 >other-file &&
	but add other-file &&
	test_tick &&
	but cummit -m other-file &&
	but stash apply --index &&
	test 3 = $(cat file) &&
	test 2 = $(but show :file) &&
	test 1 = $(but show HEAD:file)
'

test_expect_success 'unstashing in a subdirectory' '
	but reset --hard HEAD &&
	mkdir subdir &&
	(
		cd subdir &&
		but stash apply
	)
'

test_expect_success 'stash drop complains of extra options' '
	test_must_fail but stash drop --foo
'

test_expect_success 'drop top stash' '
	but reset --hard &&
	but stash list >expected &&
	echo 7 >file &&
	but stash &&
	but stash drop &&
	but stash list >actual &&
	test_cmp expected actual &&
	but stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file)
'

test_expect_success 'drop middle stash' '
	but reset --hard &&
	echo 8 >file &&
	but stash &&
	echo 9 >file &&
	but stash &&
	but stash drop stash@{1} &&
	test 2 = $(but stash list | wc -l) &&
	but stash apply &&
	test 9 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file) &&
	but reset --hard &&
	but stash drop &&
	but stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file)
'

test_expect_success 'drop middle stash by index' '
	but reset --hard &&
	echo 8 >file &&
	but stash &&
	echo 9 >file &&
	but stash &&
	but stash drop 1 &&
	test 2 = $(but stash list | wc -l) &&
	but stash apply &&
	test 9 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file) &&
	but reset --hard &&
	but stash drop &&
	but stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file)
'

test_expect_success 'drop stash reflog updates refs/stash' '
	but reset --hard &&
	but rev-parse refs/stash >expect &&
	echo 9 >file &&
	but stash &&
	but stash drop stash@{0} &&
	but rev-parse refs/stash >actual &&
	test_cmp expect actual
'

test_expect_success REFFILES 'drop stash reflog updates refs/stash with rewrite' '
	but init repo &&
	(
		cd repo &&
		setup_stash
	) &&
	echo 9 >repo/file &&

	old_oid="$(but -C repo rev-parse stash@{0})" &&
	but -C repo stash &&
	new_oid="$(but -C repo rev-parse stash@{0})" &&

	cat >expect <<-EOF &&
	$(test_oid zero) $old_oid
	$old_oid $new_oid
	EOF
	cut -d" " -f1-2 repo/.but/logs/refs/stash >actual &&
	test_cmp expect actual &&

	but -C repo stash drop stash@{1} &&
	cut -d" " -f1-2 repo/.but/logs/refs/stash >actual &&
	cat >expect <<-EOF &&
	$(test_oid zero) $new_oid
	EOF
	test_cmp expect actual
'

test_expect_success 'stash pop' '
	but reset --hard &&
	but stash pop &&
	test 3 = $(cat file) &&
	test 1 = $(but show :file) &&
	test 1 = $(but show HEAD:file) &&
	test 0 = $(but stash list | wc -l)
'

cat >expect <<EOF
diff --but a/file2 b/file2
new file mode 100644
index 0000000..1fe912c
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+bar2
EOF

cat >expect1 <<EOF
diff --but a/file b/file
index 257cc56..5716ca5 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-foo
+bar
EOF

cat >expect2 <<EOF
diff --but a/file b/file
index 7601807..5716ca5 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-baz
+bar
diff --but a/file2 b/file2
new file mode 100644
index 0000000..1fe912c
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+bar2
EOF

test_expect_success 'stash branch' '
	echo foo >file &&
	but cummit file -m first &&
	echo bar >file &&
	echo bar2 >file2 &&
	but add file2 &&
	but stash &&
	echo baz >file &&
	but cummit file -m second &&
	but stash branch stashbranch &&
	test refs/heads/stashbranch = $(but symbolic-ref HEAD) &&
	test $(but rev-parse HEAD) = $(but rev-parse main^) &&
	but diff --cached >output &&
	diff_cmp expect output &&
	but diff >output &&
	diff_cmp expect1 output &&
	but add file &&
	but cummit -m alternate\ second &&
	but diff main..stashbranch >output &&
	diff_cmp output expect2 &&
	test 0 = $(but stash list | wc -l)
'

test_expect_success 'apply -q is quiet' '
	echo foo >file &&
	but stash &&
	but stash apply -q >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'apply --index -q is quiet' '
	# Added file, deleted file, modified file all staged for cummit
	echo foo >new-file &&
	echo test >file &&
	but add new-file file &&
	but rm other-file &&

	but stash &&
	but stash apply --index -q >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'save -q is quiet' '
	but stash save --quiet >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'pop -q works and is quiet' '
	but stash pop -q >output.out 2>&1 &&
	echo bar >expect &&
	but show :file >actual &&
	test_cmp expect actual &&
	test_must_be_empty output.out
'

test_expect_success 'pop -q --index works and is quiet' '
	echo foo >file &&
	but add file &&
	but stash save --quiet &&
	but stash pop -q --index >output.out 2>&1 &&
	but diff-files file2 >file2.diff &&
	test_must_be_empty file2.diff &&
	test foo = "$(but show :file)" &&
	test_must_be_empty output.out
'

test_expect_success 'drop -q is quiet' '
	but stash &&
	but stash drop -q >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'stash push -q --staged refreshes the index' '
	but reset --hard &&
	echo test >file &&
	but add file &&
	but stash push -q --staged &&
	but diff-files >output.out &&
	test_must_be_empty output.out
'

test_expect_success 'stash apply -q --index refreshes the index' '
	echo test >other-file &&
	but add other-file &&
	echo another-change >other-file &&
	but diff-files >expect &&
	but stash &&

	but stash apply -q --index &&
	but diff-files >actual &&
	test_cmp expect actual
'

test_expect_success 'stash -k' '
	echo bar3 >file &&
	echo bar4 >file2 &&
	but add file2 &&
	but stash -k &&
	test bar,bar4 = $(cat file),$(cat file2)
'

test_expect_success 'stash --no-keep-index' '
	echo bar33 >file &&
	echo bar44 >file2 &&
	but add file2 &&
	but stash --no-keep-index &&
	test bar,bar2 = $(cat file),$(cat file2)
'

test_expect_success 'stash --staged' '
	echo bar3 >file &&
	echo bar4 >file2 &&
	but add file2 &&
	but stash --staged &&
	test bar3,bar2 = $(cat file),$(cat file2) &&
	but reset --hard &&
	but stash pop &&
	test bar,bar4 = $(cat file),$(cat file2)
'

test_expect_success 'dont assume push with non-option args' '
	test_must_fail but stash -q drop 2>err &&
	test_i18ngrep -e "subcommand wasn'\''t specified; '\''push'\'' can'\''t be assumed due to unexpected token '\''drop'\''" err
'

test_expect_success 'stash --invalid-option' '
	echo bar5 >file &&
	echo bar6 >file2 &&
	but add file2 &&
	test_must_fail but stash --invalid-option &&
	test_must_fail but stash save --invalid-option &&
	test bar5,bar6 = $(cat file),$(cat file2)
'

test_expect_success 'stash an added file' '
	but reset --hard &&
	echo new >file3 &&
	but add file3 &&
	but stash save "added file" &&
	! test -r file3 &&
	but stash apply &&
	test new = "$(cat file3)"
'

test_expect_success 'stash --intent-to-add file' '
	but reset --hard &&
	echo new >file4 &&
	but add --intent-to-add file4 &&
	test_when_finished "but rm -f file4" &&
	test_must_fail but stash
'

test_expect_success 'stash rm then recreate' '
	but reset --hard &&
	but rm file &&
	echo bar7 >file &&
	but stash save "rm then recreate" &&
	test bar = "$(cat file)" &&
	but stash apply &&
	test bar7 = "$(cat file)"
'

test_expect_success 'stash rm and ignore' '
	but reset --hard &&
	but rm file &&
	echo file >.butignore &&
	but stash save "rm and ignore" &&
	test bar = "$(cat file)" &&
	test file = "$(cat .butignore)" &&
	but stash apply &&
	! test -r file &&
	test file = "$(cat .butignore)"
'

test_expect_success 'stash rm and ignore (stage .butignore)' '
	but reset --hard &&
	but rm file &&
	echo file >.butignore &&
	but add .butignore &&
	but stash save "rm and ignore (stage .butignore)" &&
	test bar = "$(cat file)" &&
	! test -r .butignore &&
	but stash apply &&
	! test -r file &&
	test file = "$(cat .butignore)"
'

test_expect_success SYMLINKS 'stash file to symlink' '
	but reset --hard &&
	rm file &&
	ln -s file2 file &&
	but stash save "file to symlink" &&
	test_path_is_file_not_symlink file &&
	test bar = "$(cat file)" &&
	but stash apply &&
	test_path_is_symlink file &&
	test "$(test_readlink file)" = file2
'

test_expect_success SYMLINKS 'stash file to symlink (stage rm)' '
	but reset --hard &&
	but rm file &&
	ln -s file2 file &&
	but stash save "file to symlink (stage rm)" &&
	test_path_is_file_not_symlink file &&
	test bar = "$(cat file)" &&
	but stash apply &&
	test_path_is_symlink file &&
	test "$(test_readlink file)" = file2
'

test_expect_success SYMLINKS 'stash file to symlink (full stage)' '
	but reset --hard &&
	rm file &&
	ln -s file2 file &&
	but add file &&
	but stash save "file to symlink (full stage)" &&
	test_path_is_file_not_symlink file &&
	test bar = "$(cat file)" &&
	but stash apply &&
	test_path_is_symlink file &&
	test "$(test_readlink file)" = file2
'

# This test creates a cummit with a symlink used for the following tests

test_expect_success 'stash symlink to file' '
	but reset --hard &&
	test_ln_s_add file filelink &&
	but cummit -m "Add symlink" &&
	rm filelink &&
	cp file filelink &&
	but stash save "symlink to file"
'

test_expect_success SYMLINKS 'this must have re-created the symlink' '
	test -h filelink &&
	case "$(ls -l filelink)" in *" filelink -> file") :;; *) false;; esac
'

test_expect_success 'unstash must re-create the file' '
	but stash apply &&
	! test -h filelink &&
	test bar = "$(cat file)"
'

test_expect_success 'stash symlink to file (stage rm)' '
	but reset --hard &&
	but rm filelink &&
	cp file filelink &&
	but stash save "symlink to file (stage rm)"
'

test_expect_success SYMLINKS 'this must have re-created the symlink' '
	test -h filelink &&
	case "$(ls -l filelink)" in *" filelink -> file") :;; *) false;; esac
'

test_expect_success 'unstash must re-create the file' '
	but stash apply &&
	! test -h filelink &&
	test bar = "$(cat file)"
'

test_expect_success 'stash symlink to file (full stage)' '
	but reset --hard &&
	rm filelink &&
	cp file filelink &&
	but add filelink &&
	but stash save "symlink to file (full stage)"
'

test_expect_success SYMLINKS 'this must have re-created the symlink' '
	test -h filelink &&
	case "$(ls -l filelink)" in *" filelink -> file") :;; *) false;; esac
'

test_expect_success 'unstash must re-create the file' '
	but stash apply &&
	! test -h filelink &&
	test bar = "$(cat file)"
'

test_expect_failure 'stash directory to file' '
	but reset --hard &&
	mkdir dir &&
	echo foo >dir/file &&
	but add dir/file &&
	but cummit -m "Add file in dir" &&
	rm -fr dir &&
	echo bar >dir &&
	but stash save "directory to file" &&
	test_path_is_dir dir &&
	test foo = "$(cat dir/file)" &&
	test_must_fail but stash apply &&
	test bar = "$(cat dir)" &&
	but reset --soft HEAD^
'

test_expect_failure 'stash file to directory' '
	but reset --hard &&
	rm file &&
	mkdir file &&
	echo foo >file/file &&
	but stash save "file to directory" &&
	test_path_is_file file &&
	test bar = "$(cat file)" &&
	but stash apply &&
	test_path_is_file file/file &&
	test foo = "$(cat file/file)"
'

test_expect_success 'giving too many ref arguments does not modify files' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	echo foo >file2 &&
	but stash &&
	echo bar >file2 &&
	but stash &&
	test-tool chmtime =123456789 file2 &&
	for type in apply pop "branch stash-branch"
	do
		test_must_fail but stash $type stash@{0} stash@{1} 2>err &&
		test_i18ngrep "Too many revisions" err &&
		test 123456789 = $(test-tool chmtime -g file2) || return 1
	done
'

test_expect_success 'drop: too many arguments errors out (does nothing)' '
	but stash list >expect &&
	test_must_fail but stash drop stash@{0} stash@{1} 2>err &&
	test_i18ngrep "Too many revisions" err &&
	but stash list >actual &&
	test_cmp expect actual
'

test_expect_success 'show: too many arguments errors out (does nothing)' '
	test_must_fail but stash show stash@{0} stash@{1} 2>err 1>out &&
	test_i18ngrep "Too many revisions" err &&
	test_must_be_empty out
'

test_expect_success 'stash create - no changes' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	but stash create >actual &&
	test_must_be_empty actual
'

test_expect_success 'stash branch - no stashes on stack, stash-like argument' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	but stash branch stash-branch ${STASH_ID} &&
	test_when_finished "but reset --hard HEAD && but checkout main &&
	but branch -D stash-branch" &&
	test $(but ls-files --modified | wc -l) -eq 1
'

test_expect_success 'stash branch - stashes on stack, stash-like argument' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	but stash &&
	test_when_finished "but stash drop" &&
	echo bar >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	but stash branch stash-branch ${STASH_ID} &&
	test_when_finished "but reset --hard HEAD && but checkout main &&
	but branch -D stash-branch" &&
	test $(but ls-files --modified | wc -l) -eq 1
'

test_expect_success 'stash branch complains with no arguments' '
	test_must_fail but stash branch 2>err &&
	test_i18ngrep "No branch name specified" err
'

test_expect_success 'stash show format defaults to --stat' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	but stash &&
	test_when_finished "but stash drop" &&
	echo bar >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	cat >expected <<-EOF &&
	 file | 1 +
	 1 file changed, 1 insertion(+)
	EOF
	but stash show ${STASH_ID} >actual &&
	test_cmp expected actual
'

test_expect_success 'stash show - stashes on stack, stash-like argument' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	but stash &&
	test_when_finished "but stash drop" &&
	echo bar >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	echo "1	0	file" >expected &&
	but stash show --numstat ${STASH_ID} >actual &&
	test_cmp expected actual
'

test_expect_success 'stash show -p - stashes on stack, stash-like argument' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	but stash &&
	test_when_finished "but stash drop" &&
	echo bar >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	cat >expected <<-EOF &&
	diff --but a/file b/file
	index 7601807..935fbd3 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baz
	+bar
	EOF
	but stash show -p ${STASH_ID} >actual &&
	diff_cmp expected actual
'

test_expect_success 'stash show - no stashes on stack, stash-like argument' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	echo "1	0	file" >expected &&
	but stash show --numstat ${STASH_ID} >actual &&
	test_cmp expected actual
'

test_expect_success 'stash show -p - no stashes on stack, stash-like argument' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD" &&
	but reset --hard &&
	echo foo >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	cat >expected <<-EOF &&
	diff --but a/file b/file
	index 7601807..71b52c4 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baz
	+foo
	EOF
	but stash show -p ${STASH_ID} >actual &&
	diff_cmp expected actual
'

test_expect_success 'stash show --patience shows diff' '
	but reset --hard &&
	echo foo >>file &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	cat >expected <<-EOF &&
	diff --but a/file b/file
	index 7601807..71b52c4 100644
	--- a/file
	+++ b/file
	@@ -1 +1,2 @@
	 baz
	+foo
	EOF
	but stash show --patience ${STASH_ID} >actual &&
	diff_cmp expected actual
'

test_expect_success 'drop: fail early if specified stash is not a stash ref' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD && but stash clear" &&
	but reset --hard &&
	echo foo >file &&
	but stash &&
	echo bar >file &&
	but stash &&
	test_must_fail but stash drop $(but rev-parse stash@{0}) &&
	but stash pop &&
	test bar = "$(cat file)" &&
	but reset --hard HEAD
'

test_expect_success 'pop: fail early if specified stash is not a stash ref' '
	but stash clear &&
	test_when_finished "but reset --hard HEAD && but stash clear" &&
	but reset --hard &&
	echo foo >file &&
	but stash &&
	echo bar >file &&
	but stash &&
	test_must_fail but stash pop $(but rev-parse stash@{0}) &&
	but stash pop &&
	test bar = "$(cat file)" &&
	but reset --hard HEAD
'

test_expect_success 'ref with non-existent reflog' '
	but stash clear &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	but add file2 &&
	but stash &&
	test_must_fail but rev-parse --quiet --verify does-not-exist &&
	test_must_fail but stash drop does-not-exist &&
	test_must_fail but stash drop does-not-exist@{0} &&
	test_must_fail but stash pop does-not-exist &&
	test_must_fail but stash pop does-not-exist@{0} &&
	test_must_fail but stash apply does-not-exist &&
	test_must_fail but stash apply does-not-exist@{0} &&
	test_must_fail but stash show does-not-exist &&
	test_must_fail but stash show does-not-exist@{0} &&
	test_must_fail but stash branch tmp does-not-exist &&
	test_must_fail but stash branch tmp does-not-exist@{0} &&
	but stash drop
'

test_expect_success 'invalid ref of the form stash@{n}, n >= N' '
	but stash clear &&
	test_must_fail but stash drop stash@{0} &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	but add file2 &&
	but stash &&
	test_must_fail but stash drop stash@{1} &&
	test_must_fail but stash pop stash@{1} &&
	test_must_fail but stash apply stash@{1} &&
	test_must_fail but stash show stash@{1} &&
	test_must_fail but stash branch tmp stash@{1} &&
	but stash drop
'

test_expect_success 'invalid ref of the form "n", n >= N' '
	but stash clear &&
	test_must_fail but stash drop 0 &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	but add file2 &&
	but stash &&
	test_must_fail but stash drop 1 &&
	test_must_fail but stash pop 1 &&
	test_must_fail but stash apply 1 &&
	test_must_fail but stash show 1 &&
	test_must_fail but stash branch tmp 1 &&
	but stash drop
'

test_expect_success 'valid ref of the form "n", n < N' '
	but stash clear &&
	echo bar5 >file &&
	echo bar6 >file2 &&
	but add file2 &&
	but stash &&
	but stash show 0 &&
	but stash branch tmp 0 &&
	but checkout main &&
	but stash &&
	but stash apply 0 &&
	but reset --hard &&
	but stash pop 0 &&
	but stash &&
	but stash drop 0 &&
	test_must_fail but stash drop
'

test_expect_success 'branch: do not drop the stash if the branch exists' '
	but stash clear &&
	echo foo >file &&
	but add file &&
	but cummit -m initial &&
	echo bar >file &&
	but stash &&
	test_must_fail but stash branch main stash@{0} &&
	but rev-parse stash@{0} --
'

test_expect_success 'branch: should not drop the stash if the apply fails' '
	but stash clear &&
	but reset HEAD~1 --hard &&
	echo foo >file &&
	but add file &&
	but cummit -m initial &&
	echo bar >file &&
	but stash &&
	echo baz >file &&
	test_when_finished "but checkout main" &&
	test_must_fail but stash branch new_branch stash@{0} &&
	but rev-parse stash@{0} --
'

test_expect_success 'apply: show same status as but status (relative to ./)' '
	but stash clear &&
	echo 1 >subdir/subfile1 &&
	echo 2 >subdir/subfile2 &&
	but add subdir/subfile1 &&
	but cummit -m subdir &&
	(
		cd subdir &&
		echo x >subfile1 &&
		echo x >../file &&
		but status >../expect &&
		but stash &&
		sane_unset GIT_MERGE_VERBOSITY &&
		but stash apply
	) |
	sed -e 1d >actual && # drop "Saved..."
	test_cmp expect actual
'

cat >expect <<EOF
diff --but a/HEAD b/HEAD
new file mode 100644
index 0000000..fe0cbee
--- /dev/null
+++ b/HEAD
@@ -0,0 +1 @@
+file-not-a-ref
EOF

test_expect_success 'stash where working directory contains "HEAD" file' '
	but stash clear &&
	but reset --hard &&
	echo file-not-a-ref >HEAD &&
	but add HEAD &&
	test_tick &&
	but stash &&
	but diff-files --quiet &&
	but diff-index --cached --quiet HEAD &&
	test "$(but rev-parse stash^)" = "$(but rev-parse HEAD)" &&
	but diff stash^..stash >output &&
	diff_cmp expect output
'

test_expect_success 'store called with invalid cummit' '
	test_must_fail but stash store foo
'

test_expect_success 'store updates stash ref and reflog' '
	but stash clear &&
	but reset --hard &&
	echo quux >bazzy &&
	but add bazzy &&
	STASH_ID=$(but stash create) &&
	but reset --hard &&
	test_path_is_missing bazzy &&
	but stash store -m quuxery $STASH_ID &&
	test $(but rev-parse stash) = $STASH_ID &&
	but reflog --format=%H stash| grep $STASH_ID &&
	but stash pop &&
	grep quux bazzy
'

test_expect_success 'handle stash specification with spaces' '
	but stash clear &&
	echo pig >file &&
	but stash &&
	stamp=$(but log -g --format="%cd" -1 refs/stash) &&
	test_tick &&
	echo cow >file &&
	but stash &&
	but stash apply "stash@{$stamp}" &&
	grep pig file
'

test_expect_success 'setup stash with index and worktree changes' '
	but stash clear &&
	but reset --hard &&
	echo index >file &&
	but add file &&
	echo working >file &&
	but stash
'

test_expect_success 'stash list -p shows simple diff' '
	cat >expect <<-EOF &&
	stash@{0}

	diff --but a/file b/file
	index 257cc56..d26b33d 100644
	--- a/file
	+++ b/file
	@@ -1 +1 @@
	-foo
	+working
	EOF
	but stash list --format=%gd -p >actual &&
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
	but stash list --format=%gd -p --cc >actual &&
	diff_cmp expect actual
'

test_expect_success 'stash is not confused by partial renames' '
	mv file renamed &&
	but add renamed &&
	but stash &&
	but stash apply &&
	test_path_is_file renamed &&
	test_path_is_missing file
'

test_expect_success 'push -m shows right message' '
	>foo &&
	but add foo &&
	but stash push -m "test message" &&
	echo "stash@{0}: On main: test message" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push -m also works without space' '
	>foo &&
	but add foo &&
	but stash push -m"unspaced test message" &&
	echo "stash@{0}: On main: unspaced test message" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store -m foo shows right message' '
	but stash clear &&
	but reset --hard &&
	echo quux >bazzy &&
	but add bazzy &&
	STASH_ID=$(but stash create) &&
	but stash store -m "store m" $STASH_ID &&
	echo "stash@{0}: store m" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store -mfoo shows right message' '
	but stash clear &&
	but reset --hard &&
	echo quux >bazzy &&
	but add bazzy &&
	STASH_ID=$(but stash create) &&
	but stash store -m"store mfoo" $STASH_ID &&
	echo "stash@{0}: store mfoo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store --message=foo shows right message' '
	but stash clear &&
	but reset --hard &&
	echo quux >bazzy &&
	but add bazzy &&
	STASH_ID=$(but stash create) &&
	but stash store --message="store message=foo" $STASH_ID &&
	echo "stash@{0}: store message=foo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'store --message foo shows right message' '
	but stash clear &&
	but reset --hard &&
	echo quux >bazzy &&
	but add bazzy &&
	STASH_ID=$(but stash create) &&
	but stash store --message "store message foo" $STASH_ID &&
	echo "stash@{0}: store message foo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push -mfoo uses right message' '
	>foo &&
	but add foo &&
	but stash push -m"test mfoo" &&
	echo "stash@{0}: On main: test mfoo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push --message foo is synonym for -mfoo' '
	>foo &&
	but add foo &&
	but stash push --message "test message foo" &&
	echo "stash@{0}: On main: test message foo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push --message=foo is synonym for -mfoo' '
	>foo &&
	but add foo &&
	but stash push --message="test message=foo" &&
	echo "stash@{0}: On main: test message=foo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'push -m shows right message' '
	>foo &&
	but add foo &&
	but stash push -m "test m foo" &&
	echo "stash@{0}: On main: test m foo" >expect &&
	but stash list -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create stores correct message' '
	>foo &&
	but add foo &&
	STASH_ID=$(but stash create "create test message") &&
	echo "On main: create test message" >expect &&
	but show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'create when branch name has /' '
	test_when_finished "but checkout main" &&
	but checkout -b some/topic &&
	>foo &&
	but add foo &&
	STASH_ID=$(but stash create "create test message") &&
	echo "On some/topic: create test message" >expect &&
	but show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'create with multiple arguments for the message' '
	>foo &&
	but add foo &&
	STASH_ID=$(but stash create test untracked) &&
	echo "On main: test untracked" >expect &&
	but show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'create in a detached state' '
	test_when_finished "but checkout main" &&
	but checkout HEAD~1 &&
	>foo &&
	but add foo &&
	STASH_ID=$(but stash create) &&
	HEAD_ID=$(but rev-parse --short HEAD) &&
	echo "WIP on (no branch): ${HEAD_ID} initial" >expect &&
	but show --pretty=%s -s ${STASH_ID} >actual &&
	test_cmp expect actual
'

test_expect_success 'stash -- <pathspec> stashes and restores the file' '
	>foo &&
	>bar &&
	but add foo bar &&
	but stash push -- foo &&
	test_path_is_file bar &&
	test_path_is_missing foo &&
	but stash pop &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash -- <pathspec> stashes in subdirectory' '
	mkdir sub &&
	>foo &&
	>bar &&
	but add foo bar &&
	(
		cd sub &&
		but stash push -- ../foo
	) &&
	test_path_is_file bar &&
	test_path_is_missing foo &&
	but stash pop &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash with multiple pathspec arguments' '
	>foo &&
	>bar &&
	>extra &&
	but add foo bar extra &&
	but stash push -- foo bar &&
	test_path_is_missing bar &&
	test_path_is_missing foo &&
	test_path_is_file extra &&
	but stash pop &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	test_path_is_file extra
'

test_expect_success 'stash with file including $IFS character' '
	>"foo bar" &&
	>foo &&
	>bar &&
	but add foo* &&
	but stash push -- "foo b*" &&
	test_path_is_missing "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	but stash pop &&
	test_path_is_file "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash with pathspec matching multiple paths' '
       echo original >file &&
       echo original >other-file &&
       but cummit -m "two" file other-file &&
       echo modified >file &&
       echo modified >other-file &&
       but stash push -- "*file" &&
       echo original >expect &&
       test_cmp expect file &&
       test_cmp expect other-file &&
       but stash pop &&
       echo modified >expect &&
       test_cmp expect file &&
       test_cmp expect other-file
'

test_expect_success 'stash push -p with pathspec shows no changes only once' '
	>foo &&
	but add foo &&
	but cummit -m "tmp" &&
	but stash push -p foo >actual &&
	echo "No local changes to save" >expect &&
	but reset --hard HEAD~ &&
	test_cmp expect actual
'

test_expect_success 'push <pathspec>: show no changes when there are none' '
	>foo &&
	but add foo &&
	but cummit -m "tmp" &&
	but stash push foo >actual &&
	echo "No local changes to save" >expect &&
	but reset --hard HEAD~ &&
	test_cmp expect actual
'

test_expect_success 'push: <pathspec> not in the repository errors out' '
	>untracked &&
	test_must_fail but stash push untracked &&
	test_path_is_file untracked
'

test_expect_success 'push: -q is quiet with changes' '
	>foo &&
	but add foo &&
	but stash push -q >output 2>&1 &&
	test_must_be_empty output
'

test_expect_success 'push: -q is quiet with no changes' '
	but stash push -q >output 2>&1 &&
	test_must_be_empty output
'

test_expect_success 'push: -q is quiet even if there is no initial cummit' '
	but init foo_dir &&
	test_when_finished rm -rf foo_dir &&
	(
		cd foo_dir &&
		>bar &&
		test_must_fail but stash push -q >output 2>&1 &&
		test_must_be_empty output
	)
'

test_expect_success 'untracked files are left in place when -u is not given' '
	>file &&
	but add file &&
	>untracked &&
	but stash push file &&
	test_path_is_file untracked
'

test_expect_success 'stash without verb with pathspec' '
	>"foo bar" &&
	>foo &&
	>bar &&
	but add foo* &&
	but stash -- "foo b*" &&
	test_path_is_missing "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar &&
	but stash pop &&
	test_path_is_file "foo bar" &&
	test_path_is_file foo &&
	test_path_is_file bar
'

test_expect_success 'stash -k -- <pathspec> leaves unstaged files intact' '
	but reset &&
	>foo &&
	>bar &&
	but add foo bar &&
	but cummit -m "test" &&
	echo "foo" >foo &&
	echo "bar" >bar &&
	but stash -k -- foo &&
	test "",bar = $(cat foo),$(cat bar) &&
	but stash pop &&
	test foo,bar = $(cat foo),$(cat bar)
'

test_expect_success 'stash -- <subdir> leaves untracked files in subdir intact' '
	but reset &&
	>subdir/untracked &&
	>subdir/tracked1 &&
	>subdir/tracked2 &&
	but add subdir/tracked* &&
	but stash -- subdir/ &&
	test_path_is_missing subdir/tracked1 &&
	test_path_is_missing subdir/tracked2 &&
	test_path_is_file subdir/untracked &&
	but stash pop &&
	test_path_is_file subdir/tracked1 &&
	test_path_is_file subdir/tracked2 &&
	test_path_is_file subdir/untracked
'

test_expect_success 'stash -- <subdir> works with binary files' '
	but reset &&
	>subdir/untracked &&
	>subdir/tracked &&
	cp "$TEST_DIRECTORY"/test-binary-1.png subdir/tracked-binary &&
	but add subdir/tracked* &&
	but stash -- subdir/ &&
	test_path_is_missing subdir/tracked &&
	test_path_is_missing subdir/tracked-binary &&
	test_path_is_file subdir/untracked &&
	but stash pop &&
	test_path_is_file subdir/tracked &&
	test_path_is_file subdir/tracked-binary &&
	test_path_is_file subdir/untracked
'

test_expect_success 'stash with user.name and user.email set works' '
	test_config user.name "A U Thor" &&
	test_config user.email "a.u@thor" &&
	but stash
'

test_expect_success 'stash works when user.name and user.email are not set' '
	but reset &&
	>1 &&
	but add 1 &&
	echo "$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" >expect &&
	but stash &&
	but show -s --format="%an <%ae>" refs/stash >actual &&
	test_cmp expect actual &&
	>2 &&
	but add 2 &&
	test_config user.useconfigonly true &&
	(
		sane_unset GIT_AUTHOR_NAME &&
		sane_unset GIT_AUTHOR_EMAIL &&
		sane_unset GIT_CUMMITTER_NAME &&
		sane_unset GIT_CUMMITTER_EMAIL &&
		test_unconfig user.email &&
		test_unconfig user.name &&
		test_must_fail but cummit -m "should fail" &&
		echo "but stash <but@stash>" >expect &&
		>2 &&
		but stash &&
		but show -s --format="%an <%ae>" refs/stash >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'stash --keep-index with file deleted in index does not resurrect it on disk' '
	test_cummit to-remove to-remove &&
	but rm to-remove &&
	but stash --keep-index &&
	test_path_is_missing to-remove
'

test_expect_success 'stash apply should succeed with unmodified file' '
	echo base >file &&
	but add file &&
	but cummit -m base &&

	# now stash a modification
	echo modified >file &&
	but stash &&

	# make the file stat dirty
	cp file other &&
	mv other file &&

	but stash apply
'

test_expect_success 'stash handles skip-worktree entries nicely' '
	test_cummit A &&
	echo changed >A.t &&
	but add A.t &&
	but update-index --skip-worktree A.t &&
	rm A.t &&
	but stash &&

	but rev-parse --verify refs/stash:A.t
'

test_expect_success 'but stash succeeds despite directory/file change' '
	test_create_repo directory_file_switch_v1 &&
	(
		cd directory_file_switch_v1 &&
		test_cummit init &&

		test_write_lines this file has some words >filler &&
		but add filler &&
		but cummit -m filler &&

		but rm filler &&
		mkdir filler &&
		echo contents >filler/file &&
		but stash push
	)
'

test_expect_success 'but stash can pop file -> directory saved changes' '
	test_create_repo directory_file_switch_v2 &&
	(
		cd directory_file_switch_v2 &&
		test_cummit init &&

		test_write_lines this file has some words >filler &&
		but add filler &&
		but cummit -m filler &&

		but rm filler &&
		mkdir filler &&
		echo contents >filler/file &&
		cp filler/file expect &&
		but stash push --include-untracked &&
		but stash apply --index &&
		test_cmp expect filler/file
	)
'

test_expect_success 'but stash can pop directory -> file saved changes' '
	test_create_repo directory_file_switch_v3 &&
	(
		cd directory_file_switch_v3 &&
		test_cummit init &&

		mkdir filler &&
		test_write_lines some words >filler/file1 &&
		test_write_lines and stuff >filler/file2 &&
		but add filler &&
		but cummit -m filler &&

		but rm -rf filler &&
		echo contents >filler &&
		cp filler expect &&
		but stash push --include-untracked &&
		but stash apply --index &&
		test_cmp expect filler
	)
'

test_expect_success 'restore untracked files even when we hit conflicts' '
	but init restore_untracked_after_conflict &&
	(
		cd restore_untracked_after_conflict &&

		echo hi >a &&
		echo there >b &&
		but add . &&
		but cummit -m first &&
		echo hello >a &&
		echo something >c &&

		but stash push --include-untracked &&

		echo conflict >a &&
		but add a &&
		but cummit -m second &&

		test_must_fail but stash pop &&

		test_path_is_file c
	)
'

test_done
