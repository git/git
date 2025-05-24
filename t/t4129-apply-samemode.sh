#!/bin/sh

test_description='applying patch with mode bits'


. ./test-lib.sh

test_expect_success setup '
	echo original >file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag initial &&
	echo modified >file &&
	git diff --stat -p >patch-0.txt &&
	chmod +x file &&
	git diff --stat -p >patch-1.txt &&
	sed "s/^\(new mode \).*/\1/" <patch-1.txt >patch-empty-mode.txt &&
	sed "s/^\(new mode \).*/\1garbage/" <patch-1.txt >patch-bogus-mode.txt
'

test_expect_success FILEMODE 'same mode (no index)' '
	git reset --hard &&
	chmod +x file &&
	git apply patch-0.txt &&
	test -x file
'

test_expect_success FILEMODE 'same mode (with index)' '
	git reset --hard &&
	chmod +x file &&
	git add file &&
	git apply --index patch-0.txt &&
	test -x file &&
	git diff --exit-code
'

test_expect_success FILEMODE 'same mode (index only)' '
	git reset --hard &&
	chmod +x file &&
	git add file &&
	git apply --cached patch-0.txt &&
	git ls-files -s file >ls-files-output &&
	test_grep "^100755" ls-files-output
'

test_expect_success FILEMODE 'mode update (no index)' '
	git reset --hard &&
	git apply patch-1.txt &&
	test -x file
'

test_expect_success FILEMODE 'mode update (with index)' '
	git reset --hard &&
	git apply --index patch-1.txt &&
	test -x file &&
	git diff --exit-code
'

test_expect_success FILEMODE 'mode update (index only)' '
	git reset --hard &&
	git apply --cached patch-1.txt &&
	git ls-files -s file >ls-files-output &&
	test_grep "^100755" ls-files-output
'

test_expect_success FILEMODE 'empty mode is rejected' '
	git reset --hard &&
	test_must_fail git apply patch-empty-mode.txt 2>err &&
	test_grep "invalid mode" err
'

test_expect_success FILEMODE 'bogus mode is rejected' '
	git reset --hard &&
	test_must_fail git apply patch-bogus-mode.txt 2>err &&
	test_grep "invalid mode" err
'

test_expect_success POSIXPERM 'do not use core.sharedRepository for working tree files' '
	git reset --hard &&
	test_config core.sharedRepository 0666 &&
	(
		# Remove a default ACL if possible.
		(setfacl -k . 2>/dev/null || true) &&
		umask 0077 &&

		# Test both files (f1) and leading dirs (d)
		mkdir d &&
		touch f1 d/f2 &&
		git add f1 d/f2 &&
		git diff --staged >patch-f1-and-f2.txt &&

		rm -rf d f1 &&
		git apply patch-f1-and-f2.txt &&

		echo "-rw-------" >f1_mode.expected &&
		echo "drwx------" >d_mode.expected &&
		test_modebits f1 >f1_mode.actual &&
		test_modebits d >d_mode.actual &&
		test_cmp f1_mode.expected f1_mode.actual &&
		test_cmp d_mode.expected d_mode.actual
	)
'

test_file_mode_common () {
	if test "$1" = "000000"
	then
		test_must_be_empty "$2"
	else
		test_grep "^$1 " "$2"
	fi
}

test_file_mode_staged () {
	git ls-files --stage -- "$2" >ls-files-output &&
	test_file_mode_common "$1" ls-files-output
}

test_file_mode_HEAD () {
	git ls-tree HEAD -- "$2" >ls-tree-output &&
	test_file_mode_common "$1" ls-tree-output
}

test_expect_success 'git apply respects core.fileMode' '
	test_config core.fileMode false &&
	echo true >script.sh &&
	git add --chmod=+x script.sh &&
	test_file_mode_staged 100755 script.sh &&
	test_tick && git commit -m "Add script" &&
	test_file_mode_HEAD 100755 script.sh &&

	echo true >>script.sh &&
	test_tick && git commit -m "Modify script" script.sh &&
	git format-patch -1 --stdout >patch &&
	test_grep "^index.*100755$" patch &&

	git switch -c branch HEAD^ &&
	git apply --index patch 2>err &&
	test_grep ! "has type 100644, expected 100755" err &&
	git reset --hard &&

	git apply patch 2>err &&
	test_grep ! "has type 100644, expected 100755" err &&

	git apply --cached patch 2>err &&
	test_grep ! "has type 100644, expected 100755" err &&
	git reset --hard
'

test_expect_success 'setup: git apply [--reverse] warns about incorrect file modes' '
	test_config core.fileMode false &&

	>mode_test &&
	git add --chmod=-x mode_test &&
	test_file_mode_staged 100644 mode_test &&
	test_tick && git commit -m "add mode_test" &&
	test_file_mode_HEAD 100644 mode_test &&
	git tag mode_test_forward_initial &&

	echo content >>mode_test &&
	test_tick && git commit -m "append to mode_test" mode_test &&
	test_file_mode_HEAD 100644 mode_test &&
	git tag mode_test_reverse_initial &&

	git format-patch -1 --stdout >patch &&
	test_grep "^index .* 100644$" patch
'

test_expect_success 'git apply warns about incorrect file modes' '
	test_config core.fileMode false &&
	git reset --hard mode_test_forward_initial &&

	git add --chmod=+x mode_test &&
	test_file_mode_staged 100755 mode_test &&
	test_tick && git commit -m "make mode_test executable" &&
	test_file_mode_HEAD 100755 mode_test &&

	git apply --index patch 2>err &&
	test_grep "has type 100755, expected 100644" err &&
	test_file_mode_staged 100755 mode_test &&
	test_tick && git commit -m "redo: append to mode_test" &&
	test_file_mode_HEAD 100755 mode_test
'

test_expect_success 'git apply --reverse warns about incorrect file modes' '
	test_config core.fileMode false &&
	git reset --hard mode_test_reverse_initial &&

	git add --chmod=+x mode_test &&
	test_file_mode_staged 100755 mode_test &&
	test_tick && git commit -m "make mode_test executable" &&
	test_file_mode_HEAD 100755 mode_test &&

	git apply --index --reverse patch 2>err &&
	test_grep "has type 100755, expected 100644" err &&
	test_file_mode_staged 100755 mode_test &&
	test_tick && git commit -m "undo: append to mode_test" &&
	test_file_mode_HEAD 100755 mode_test
'

test_expect_success 'setup: git apply [--reverse] restores file modes (change_x_to_notx)' '
	test_config core.fileMode false &&

	touch change_x_to_notx &&
	git add --chmod=+x change_x_to_notx &&
	test_file_mode_staged 100755 change_x_to_notx &&
	test_tick && git commit -m "add change_x_to_notx as executable" &&
	test_file_mode_HEAD 100755 change_x_to_notx &&

	git add --chmod=-x change_x_to_notx &&
	test_file_mode_staged 100644 change_x_to_notx &&
	test_tick && git commit -m "make change_x_to_notx not executable" &&
	test_file_mode_HEAD 100644 change_x_to_notx &&

	git rm change_x_to_notx &&
	test_file_mode_staged 000000 change_x_to_notx &&
	test_tick && git commit -m "remove change_x_to_notx" &&
	test_file_mode_HEAD 000000 change_x_to_notx &&

	git format-patch -o patches -3 &&
	mv patches/0001-* change_x_to_notx-0001-create-0755.patch &&
	mv patches/0002-* change_x_to_notx-0002-chmod-0644.patch &&
	mv patches/0003-* change_x_to_notx-0003-delete.patch &&

	test_grep "^new file mode 100755$" change_x_to_notx-0001-create-0755.patch &&
	test_grep "^old mode 100755$" change_x_to_notx-0002-chmod-0644.patch &&
	test_grep "^new mode 100644$" change_x_to_notx-0002-chmod-0644.patch &&
	test_grep "^deleted file mode 100644$" change_x_to_notx-0003-delete.patch &&

	git tag change_x_to_notx_initial
'

test_expect_success 'git apply restores file modes (change_x_to_notx)' '
	test_config core.fileMode false &&
	git reset --hard change_x_to_notx_initial &&

	git apply --index change_x_to_notx-0001-create-0755.patch &&
	test_file_mode_staged 100755 change_x_to_notx &&
	test_tick && git commit -m "redo: add change_x_to_notx as executable" &&
	test_file_mode_HEAD 100755 change_x_to_notx &&

	git apply --index change_x_to_notx-0002-chmod-0644.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 100644 change_x_to_notx &&
	test_tick && git commit -m "redo: make change_x_to_notx not executable" &&
	test_file_mode_HEAD 100644 change_x_to_notx &&

	git apply --index change_x_to_notx-0003-delete.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 000000 change_x_to_notx &&
	test_tick && git commit -m "redo: remove change_notx_to_x" &&
	test_file_mode_HEAD 000000 change_x_to_notx
'

test_expect_success 'git apply --reverse restores file modes (change_x_to_notx)' '
	test_config core.fileMode false &&
	git reset --hard change_x_to_notx_initial &&

	git apply --index --reverse change_x_to_notx-0003-delete.patch &&
	test_file_mode_staged 100644 change_x_to_notx &&
	test_tick && git commit -m "undo: remove change_x_to_notx" &&
	test_file_mode_HEAD 100644 change_x_to_notx &&

	git apply --index --reverse change_x_to_notx-0002-chmod-0644.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 100755 change_x_to_notx &&
	test_tick && git commit -m "undo: make change_x_to_notx not executable" &&
	test_file_mode_HEAD 100755 change_x_to_notx &&

	git apply --index --reverse change_x_to_notx-0001-create-0755.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 000000 change_x_to_notx &&
	test_tick && git commit -m "undo: add change_x_to_notx as executable" &&
	test_file_mode_HEAD 000000 change_x_to_notx
'

test_expect_success 'setup: git apply [--reverse] restores file modes (change_notx_to_x)' '
	test_config core.fileMode false &&

	touch change_notx_to_x &&
	git add --chmod=-x change_notx_to_x &&
	test_file_mode_staged 100644 change_notx_to_x &&
	test_tick && git commit -m "add change_notx_to_x as not executable" &&
	test_file_mode_HEAD 100644 change_notx_to_x &&

	git add --chmod=+x change_notx_to_x &&
	test_file_mode_staged 100755 change_notx_to_x &&
	test_tick && git commit -m "make change_notx_to_x executable" &&
	test_file_mode_HEAD 100755 change_notx_to_x &&

	git rm change_notx_to_x &&
	test_file_mode_staged 000000 change_notx_to_x &&
	test_tick && git commit -m "remove change_notx_to_x" &&
	test_file_mode_HEAD 000000 change_notx_to_x &&

	git format-patch -o patches -3 &&
	mv patches/0001-* change_notx_to_x-0001-create-0644.patch &&
	mv patches/0002-* change_notx_to_x-0002-chmod-0755.patch &&
	mv patches/0003-* change_notx_to_x-0003-delete.patch &&

	test_grep "^new file mode 100644$" change_notx_to_x-0001-create-0644.patch &&
	test_grep "^old mode 100644$" change_notx_to_x-0002-chmod-0755.patch &&
	test_grep "^new mode 100755$" change_notx_to_x-0002-chmod-0755.patch &&
	test_grep "^deleted file mode 100755$" change_notx_to_x-0003-delete.patch &&

	git tag change_notx_to_x_initial
'

test_expect_success 'git apply restores file modes (change_notx_to_x)' '
	test_config core.fileMode false &&
	git reset --hard change_notx_to_x_initial &&

	git apply --index change_notx_to_x-0001-create-0644.patch &&
	test_file_mode_staged 100644 change_notx_to_x &&
	test_tick && git commit -m "redo: add change_notx_to_x as not executable" &&
	test_file_mode_HEAD 100644 change_notx_to_x &&

	git apply --index change_notx_to_x-0002-chmod-0755.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 100755 change_notx_to_x &&
	test_tick && git commit -m "redo: make change_notx_to_x executable" &&
	test_file_mode_HEAD 100755 change_notx_to_x &&

	git apply --index change_notx_to_x-0003-delete.patch &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 000000 change_notx_to_x &&
	test_tick && git commit -m "undo: remove change_notx_to_x" &&
	test_file_mode_HEAD 000000 change_notx_to_x
'

test_expect_success 'git apply --reverse restores file modes (change_notx_to_x)' '
	test_config core.fileMode false &&
	git reset --hard change_notx_to_x_initial &&

	git apply --index --reverse change_notx_to_x-0003-delete.patch &&
	test_file_mode_staged 100755 change_notx_to_x &&
	test_tick && git commit -m "undo: remove change_notx_to_x" &&
	test_file_mode_HEAD 100755 change_notx_to_x &&

	git apply --index --reverse change_notx_to_x-0002-chmod-0755.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 100644 change_notx_to_x &&
	test_tick && git commit -m "undo: make change_notx_to_x executable" &&
	test_file_mode_HEAD 100644 change_notx_to_x &&

	git apply --index --reverse change_notx_to_x-0001-create-0644.patch 2>err &&
	test_grep ! "has type 100.*, expected 100.*" err &&
	test_file_mode_staged 000000 change_notx_to_x &&
	test_tick && git commit -m "undo: add change_notx_to_x as not executable" &&
	test_file_mode_HEAD 000000 change_notx_to_x
'

test_expect_success POSIXPERM 'patch mode for new file is canonicalized' '
	cat >patch <<-\EOF &&
	diff --git a/non-canon b/non-canon
	new file mode 100660
	--- /dev/null
	+++ b/non-canon
	+content
	EOF
	test_when_finished "git reset --hard" &&
	(
		umask 0 &&
		git apply --index patch 2>err
	) &&
	test_must_be_empty err &&
	git ls-files -s -- non-canon >staged &&
	test_grep "^100644" staged &&
	ls -l non-canon >worktree &&
	test_grep "^-rw-rw-rw" worktree
'

test_expect_success POSIXPERM 'patch mode for deleted file is canonicalized' '
	test_when_finished "git reset --hard" &&
	echo content >non-canon &&
	chmod 666 non-canon &&
	git add non-canon &&

	cat >patch <<-\EOF &&
	diff --git a/non-canon b/non-canon
	deleted file mode 100660
	--- a/non-canon
	+++ /dev/null
	@@ -1 +0,0 @@
	-content
	EOF
	git apply --index patch 2>err &&
	test_must_be_empty err &&
	git ls-files -- non-canon >staged &&
	test_must_be_empty staged &&
	test_path_is_missing non-canon
'

test_expect_success POSIXPERM 'patch mode for mode change is canonicalized' '
	test_when_finished "git reset --hard" &&
	echo content >non-canon &&
	git add non-canon &&

	cat >patch <<-\EOF &&
	diff --git a/non-canon b/non-canon
	old mode 100660
	new mode 100770
	EOF
	(
		umask 0 &&
		git apply --index patch 2>err
	) &&
	test_must_be_empty err &&
	git ls-files -s -- non-canon >staged &&
	test_grep "^100755" staged &&
	ls -l non-canon >worktree &&
	test_grep "^-rwxrwxrwx" worktree
'

test_done
