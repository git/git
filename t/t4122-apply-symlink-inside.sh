#!/bin/sh

test_description='apply to deeper directory without getting fooled with symlink'
. ./test-lib.sh

test_expect_success setup '

	mkdir -p arch/i386/boot arch/x86_64 &&
	test_write_lines 1 2 3 4 5 >arch/i386/boot/Makefile &&
	test_ln_s_add ../i386/boot arch/x86_64/boot &&
	git add . &&
	test_tick &&
	git commit -m initial &&
	git branch test &&

	rm arch/x86_64/boot &&
	mkdir arch/x86_64/boot &&
	test_write_lines 2 3 4 5 6 >arch/x86_64/boot/Makefile &&
	git add . &&
	test_tick &&
	git commit -a -m second &&

	git format-patch --binary -1 --stdout >test.patch

'

test_expect_success apply '

	git checkout test &&
	git diff --exit-code test &&
	git diff --exit-code --cached test &&
	git apply --index test.patch

'

test_expect_success 'check result' '

	git diff --exit-code master &&
	git diff --exit-code --cached master &&
	test_tick &&
	git commit -m replay &&
	T1=$(git rev-parse "master^{tree}") &&
	T2=$(git rev-parse "HEAD^{tree}") &&
	test "z$T1" = "z$T2"

'

test_expect_success SYMLINKS 'do not read from beyond symbolic link' '
	git reset --hard &&
	mkdir -p arch/x86_64/dir &&
	>arch/x86_64/dir/file &&
	git add arch/x86_64/dir/file &&
	echo line >arch/x86_64/dir/file &&
	git diff >patch &&
	git reset --hard &&

	mkdir arch/i386/dir &&
	>arch/i386/dir/file &&
	ln -s ../i386/dir arch/x86_64/dir &&

	test_must_fail git apply patch &&
	test_must_fail git apply --cached patch &&
	test_must_fail git apply --index patch

'

test_expect_success SYMLINKS 'do not follow symbolic link (setup)' '

	rm -rf arch/i386/dir arch/x86_64/dir &&
	git reset --hard &&
	ln -s ../i386/dir arch/x86_64/dir &&
	git add arch/x86_64/dir &&
	git diff HEAD >add_symlink.patch &&
	git reset --hard &&

	mkdir arch/x86_64/dir &&
	>arch/x86_64/dir/file &&
	git add arch/x86_64/dir/file &&
	git diff HEAD >add_file.patch &&
	git diff -R HEAD >del_file.patch &&
	git reset --hard &&
	rm -fr arch/x86_64/dir &&

	cat add_symlink.patch add_file.patch >patch &&
	cat add_symlink.patch del_file.patch >tricky_del &&

	mkdir arch/i386/dir
'

test_expect_success SYMLINKS 'do not follow symbolic link (same input)' '

	# same input creates a confusing symbolic link
	test_must_fail git apply patch 2>error-wt &&
	test_i18ngrep "beyond a symbolic link" error-wt &&
	test_path_is_missing arch/x86_64/dir &&
	test_path_is_missing arch/i386/dir/file &&

	test_must_fail git apply --index patch 2>error-ix &&
	test_i18ngrep "beyond a symbolic link" error-ix &&
	test_path_is_missing arch/x86_64/dir &&
	test_path_is_missing arch/i386/dir/file &&
	test_must_fail git ls-files --error-unmatch arch/x86_64/dir &&
	test_must_fail git ls-files --error-unmatch arch/i386/dir &&

	test_must_fail git apply --cached patch 2>error-ct &&
	test_i18ngrep "beyond a symbolic link" error-ct &&
	test_must_fail git ls-files --error-unmatch arch/x86_64/dir &&
	test_must_fail git ls-files --error-unmatch arch/i386/dir &&

	>arch/i386/dir/file &&
	git add arch/i386/dir/file &&

	test_must_fail git apply tricky_del &&
	test_path_is_file arch/i386/dir/file &&

	test_must_fail git apply --index tricky_del &&
	test_path_is_file arch/i386/dir/file &&
	test_must_fail git ls-files --error-unmatch arch/x86_64/dir &&
	git ls-files --error-unmatch arch/i386/dir &&

	test_must_fail git apply --cached tricky_del &&
	test_must_fail git ls-files --error-unmatch arch/x86_64/dir &&
	git ls-files --error-unmatch arch/i386/dir
'

test_expect_success SYMLINKS 'do not follow symbolic link (existing)' '

	# existing symbolic link
	git reset --hard &&
	ln -s ../i386/dir arch/x86_64/dir &&
	git add arch/x86_64/dir &&

	test_must_fail git apply add_file.patch 2>error-wt-add &&
	test_i18ngrep "beyond a symbolic link" error-wt-add &&
	test_path_is_missing arch/i386/dir/file &&

	mkdir arch/i386/dir &&
	>arch/i386/dir/file &&
	test_must_fail git apply del_file.patch 2>error-wt-del &&
	test_i18ngrep "beyond a symbolic link" error-wt-del &&
	test_path_is_file arch/i386/dir/file &&
	rm arch/i386/dir/file &&

	test_must_fail git apply --index add_file.patch 2>error-ix-add &&
	test_i18ngrep "beyond a symbolic link" error-ix-add &&
	test_path_is_missing arch/i386/dir/file &&
	test_must_fail git ls-files --error-unmatch arch/i386/dir &&

	test_must_fail git apply --cached add_file.patch 2>error-ct-file &&
	test_i18ngrep "beyond a symbolic link" error-ct-file &&
	test_must_fail git ls-files --error-unmatch arch/i386/dir
'

test_done
