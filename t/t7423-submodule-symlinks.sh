#!/bin/sh

test_description='check that submodule operations do not follow symlinks'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'prepare' '
	git config --global protocol.file.allow always &&
	test_commit initial &&
	git init upstream &&
	test_commit -C upstream upstream submodule_file &&
	git submodule add ./upstream a/sm &&
	test_tick &&
	git commit -m submodule
'

test_expect_success SYMLINKS 'git submodule update must not create submodule behind symlink' '
	rm -rf a b &&
	mkdir b &&
	ln -s b a &&
	test_path_is_missing b/sm &&
	test_must_fail git submodule update &&
	test_path_is_missing b/sm
'

test_expect_success SYMLINKS,CASE_INSENSITIVE_FS 'git submodule update must not create submodule behind symlink on case insensitive fs' '
	rm -rf a b &&
	mkdir b &&
	ln -s b A &&
	test_must_fail git submodule update &&
	test_path_is_missing b/sm
'

prepare_symlink_to_repo() {
	rm -rf a &&
	mkdir a &&
	git init a/target &&
	git -C a/target fetch ../../upstream &&
	ln -s target a/sm
}

test_expect_success SYMLINKS 'git restore --recurse-submodules must not be confused by a symlink' '
	prepare_symlink_to_repo &&
	test_must_fail git restore --recurse-submodules a/sm &&
	test_path_is_missing a/sm/submodule_file &&
	test_path_is_dir a/target/.git &&
	test_path_is_missing a/target/submodule_file
'

test_expect_success SYMLINKS 'git restore --recurse-submodules must not migrate git dir of symlinked repo' '
	prepare_symlink_to_repo &&
	rm -rf .git/modules &&
	test_must_fail git restore --recurse-submodules a/sm &&
	test_path_is_dir a/target/.git &&
	test_path_is_missing .git/modules/a/sm &&
	test_path_is_missing a/target/submodule_file
'

test_expect_success SYMLINKS 'git checkout -f --recurse-submodules must not migrate git dir of symlinked repo when removing submodule' '
	prepare_symlink_to_repo &&
	rm -rf .git/modules &&
	test_must_fail git checkout -f --recurse-submodules initial &&
	test_path_is_dir a/target/.git &&
	test_path_is_missing .git/modules/a/sm
'

test_done
