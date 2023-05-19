#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply symlinks and partial files

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	test_ln_s_add path1/path2/path3/path4/path5 link1 &&
	git commit -m initial &&

	git branch side &&

	rm -f link? &&

	test_ln_s_add htap6 link1 &&
	git commit -m second &&

	git diff-tree -p HEAD^ HEAD >patch  &&
	git apply --stat --summary patch

'

test_expect_success SYMLINKS 'apply symlink patch' '

	git checkout side &&
	git apply patch &&
	git diff-files -p >patched &&
	test_cmp patch patched

'

test_expect_success 'apply --index symlink patch' '

	git checkout -f side &&
	git apply --index patch &&
	git diff-index --cached -p HEAD >patched &&
	test_cmp patch patched

'

test_expect_success 'symlink setup' '
	ln -s .git symlink &&
	git add symlink &&
	git commit -m "add symlink"
'

test_expect_success SYMLINKS 'symlink escape when creating new files' '
	test_when_finished "git reset --hard && git clean -dfx" &&

	cat >patch <<-EOF &&
	diff --git a/symlink b/renamed-symlink
	similarity index 100%
	rename from symlink
	rename to renamed-symlink
	--
	diff --git /dev/null b/renamed-symlink/create-me
	new file mode 100644
	index 0000000..039727e
	--- /dev/null
	+++ b/renamed-symlink/create-me
	@@ -0,0 +1,1 @@
	+busted
	EOF

	test_must_fail git apply patch 2>stderr &&
	cat >expected_stderr <<-EOF &&
	error: affected file ${SQ}renamed-symlink/create-me${SQ} is beyond a symbolic link
	EOF
	test_cmp expected_stderr stderr &&
	test_path_is_missing .git/create-me
'

test_expect_success SYMLINKS 'symlink escape when modifying file' '
	test_when_finished "git reset --hard && git clean -dfx" &&
	touch .git/modify-me &&

	cat >patch <<-EOF &&
	diff --git a/symlink b/renamed-symlink
	similarity index 100%
	rename from symlink
	rename to renamed-symlink
	--
	diff --git a/renamed-symlink/modify-me b/renamed-symlink/modify-me
	index 1111111..2222222 100644
	--- a/renamed-symlink/modify-me
	+++ b/renamed-symlink/modify-me
	@@ -0,0 +1,1 @@
	+busted
	EOF

	test_must_fail git apply patch 2>stderr &&
	cat >expected_stderr <<-EOF &&
	error: renamed-symlink/modify-me: No such file or directory
	EOF
	test_cmp expected_stderr stderr &&
	test_must_be_empty .git/modify-me
'

test_expect_success SYMLINKS 'symlink escape when deleting file' '
	test_when_finished "git reset --hard && git clean -dfx && rm .git/delete-me" &&
	touch .git/delete-me &&

	cat >patch <<-EOF &&
	diff --git a/symlink b/renamed-symlink
	similarity index 100%
	rename from symlink
	rename to renamed-symlink
	--
	diff --git a/renamed-symlink/delete-me b/renamed-symlink/delete-me
	deleted file mode 100644
	index 1111111..0000000 100644
	EOF

	test_must_fail git apply patch 2>stderr &&
	cat >expected_stderr <<-EOF &&
	error: renamed-symlink/delete-me: No such file or directory
	EOF
	test_cmp expected_stderr stderr &&
	test_path_is_file .git/delete-me
'

test_expect_success SYMLINKS '--reject removes .rej symlink if it exists' '
	test_when_finished "git reset --hard && git clean -dfx" &&

	test_commit file &&
	echo modified >file.t &&
	git diff -- file.t >patch &&
	echo modified-again >file.t &&

	ln -s foo file.t.rej &&
	test_must_fail git apply patch --reject 2>err &&
	test_i18ngrep "Rejected hunk" err &&
	test_path_is_missing foo &&
	test_path_is_file file.t.rej
'

test_done
