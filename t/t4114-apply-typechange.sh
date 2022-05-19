#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='but apply should not get confused with type changes.

'

. ./test-lib.sh

test_expect_success 'setup repository and cummits' '
	echo "hello world" > foo &&
	echo "hi planet" > bar &&
	but update-index --add foo bar &&
	but cummit -m initial &&
	but branch initial &&
	rm -f foo &&
	test_ln_s_add bar foo &&
	but cummit -m "foo symlinked to bar" &&
	but branch foo-symlinked-to-bar &&
	but rm -f foo &&
	echo "how far is the sun?" > foo &&
	but update-index --add foo &&
	but cummit -m "foo back to file" &&
	but branch foo-back-to-file &&
	printf "\0" > foo &&
	but update-index foo &&
	but cummit -m "foo becomes binary" &&
	but branch foo-becomes-binary &&
	rm -f foo &&
	but update-index --remove foo &&
	mkdir foo &&
	echo "if only I knew" > foo/baz &&
	but update-index --add foo/baz &&
	but cummit -m "foo becomes a directory" &&
	but branch "foo-becomes-a-directory" &&
	echo "hello world" > foo/baz &&
	but update-index foo/baz &&
	but cummit -m "foo/baz is the original foo" &&
	but branch foo-baz-renamed-from-foo
	'

test_expect_success 'file renamed from foo to foo/baz' '
	but checkout -f initial &&
	but diff-tree -M -p HEAD foo-baz-renamed-from-foo > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'file renamed from foo/baz to foo' '
	but checkout -f foo-baz-renamed-from-foo &&
	but diff-tree -M -p HEAD initial > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'directory becomes file' '
	but checkout -f foo-becomes-a-directory &&
	but diff-tree -p HEAD initial > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'file becomes directory' '
	but checkout -f initial &&
	but diff-tree -p HEAD foo-becomes-a-directory > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'file becomes symlink' '
	but checkout -f initial &&
	but diff-tree -p HEAD foo-symlinked-to-bar > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'symlink becomes file' '
	but checkout -f foo-symlinked-to-bar &&
	but diff-tree -p HEAD foo-back-to-file > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'

test_expect_success 'symlink becomes file, in reverse' '
	but checkout -f foo-symlinked-to-bar &&
	but diff-tree -p HEAD foo-back-to-file > patch &&
	but checkout foo-back-to-file &&
	but apply -R --index < patch
	'

test_expect_success 'binary file becomes symlink' '
	but checkout -f foo-becomes-binary &&
	but diff-tree -p --binary HEAD foo-symlinked-to-bar > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'

test_expect_success 'symlink becomes binary file' '
	but checkout -f foo-symlinked-to-bar &&
	but diff-tree -p --binary HEAD foo-becomes-binary > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'symlink becomes directory' '
	but checkout -f foo-symlinked-to-bar &&
	but diff-tree -p HEAD foo-becomes-a-directory > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_expect_success 'directory becomes symlink' '
	but checkout -f foo-becomes-a-directory &&
	but diff-tree -p HEAD foo-symlinked-to-bar > patch &&
	but apply --index < patch
	'
test_debug 'cat patch'


test_done
