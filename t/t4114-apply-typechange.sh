#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git apply should not get confused with type changes.

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup repository and commits' '
	echo "hello world" > foo &&
	echo "hi planet" > bar &&
	git update-index --add foo bar &&
	git commit -m initial &&
	git branch initial &&
	rm -f foo &&
	test_ln_s_add bar foo &&
	git commit -m "foo symlinked to bar" &&
	git branch foo-symlinked-to-bar &&
	git rm -f foo &&
	echo "how far is the sun?" > foo &&
	git update-index --add foo &&
	git commit -m "foo back to file" &&
	git branch foo-back-to-file &&
	printf "\0" > foo &&
	git update-index foo &&
	git commit -m "foo becomes binary" &&
	git branch foo-becomes-binary &&
	rm -f foo &&
	git update-index --remove foo &&
	mkdir foo &&
	echo "if only I knew" > foo/baz &&
	git update-index --add foo/baz &&
	git commit -m "foo becomes a directory" &&
	git branch "foo-becomes-a-directory" &&
	echo "hello world" > foo/baz &&
	git update-index foo/baz &&
	git commit -m "foo/baz is the original foo" &&
	git branch foo-baz-renamed-from-foo
	'

test_expect_success 'file renamed from foo to foo/baz' '
	git checkout -f initial &&
	git diff-tree -M -p HEAD foo-baz-renamed-from-foo > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'file renamed from foo/baz to foo' '
	git checkout -f foo-baz-renamed-from-foo &&
	git diff-tree -M -p HEAD initial > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'directory becomes file' '
	git checkout -f foo-becomes-a-directory &&
	git diff-tree -p HEAD initial > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'file becomes directory' '
	git checkout -f initial &&
	git diff-tree -p HEAD foo-becomes-a-directory > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'file becomes symlink' '
	git checkout -f initial &&
	git diff-tree -p HEAD foo-symlinked-to-bar > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'symlink becomes file' '
	git checkout -f foo-symlinked-to-bar &&
	git diff-tree -p HEAD foo-back-to-file > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'

test_expect_success 'symlink becomes file, in reverse' '
	git checkout -f foo-symlinked-to-bar &&
	git diff-tree -p HEAD foo-back-to-file > patch &&
	git checkout foo-back-to-file &&
	git apply -R --index < patch
	'

test_expect_success 'binary file becomes symlink' '
	git checkout -f foo-becomes-binary &&
	git diff-tree -p --binary HEAD foo-symlinked-to-bar > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'

test_expect_success 'symlink becomes binary file' '
	git checkout -f foo-symlinked-to-bar &&
	git diff-tree -p --binary HEAD foo-becomes-binary > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'symlink becomes directory' '
	git checkout -f foo-symlinked-to-bar &&
	git diff-tree -p HEAD foo-becomes-a-directory > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'directory becomes symlink' '
	git checkout -f foo-becomes-a-directory &&
	git diff-tree -p HEAD foo-symlinked-to-bar > patch &&
	git apply --index < patch
	'
test_debug 'cat patch'


test_done
