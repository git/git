#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of git-add, including the -- option.'

. ./test-lib.sh

test_expect_success \
    'Test of git-add' \
    'touch foo && git-add foo'

test_expect_success \
    'Post-check that foo is in the index' \
    'git-ls-files foo | grep foo'

test_expect_success \
    'Test that "git-add -- -q" works' \
    'touch -- -q && git-add -- -q'

test_expect_success \
	'git-add: Test that executable bit is not used if core.filemode=0' \
	'git repo-config core.filemode 0 &&
	 echo foo >xfoo1 &&
	 chmod 755 xfoo1 &&
	 git-add xfoo1 &&
	 case "`git-ls-files --stage xfoo1`" in
	 100644" "*xfoo1) echo ok;;
	 *) echo fail; git-ls-files --stage xfoo1; exit 1;;
	 esac'

test_expect_success \
	'git-update-index --add: Test that executable bit is not used...' \
	'git repo-config core.filemode 0 &&
	 echo foo >xfoo2 &&
	 chmod 755 xfoo2 &&
	 git-add xfoo2 &&
	 case "`git-ls-files --stage xfoo2`" in
	 100644" "*xfoo2) echo ok;;
	 *) echo fail; git-ls-files --stage xfoo2; exit 1;;
	 esac'

test_done
