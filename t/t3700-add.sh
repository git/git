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
	'git config core.filemode 0 &&
	 echo foo >xfoo1 &&
	 chmod 755 xfoo1 &&
	 git-add xfoo1 &&
	 case "`git-ls-files --stage xfoo1`" in
	 100644" "*xfoo1) echo ok;;
	 *) echo fail; git-ls-files --stage xfoo1; (exit 1);;
	 esac'

test_expect_success 'git-add: filemode=0 should not get confused by symlink' '
	rm -f xfoo1 &&
	ln -s foo xfoo1 &&
	git-add xfoo1 &&
	case "`git-ls-files --stage xfoo1`" in
	120000" "*xfoo1) echo ok;;
	*) echo fail; git-ls-files --stage xfoo1; (exit 1);;
	esac
'

test_expect_success \
	'git-update-index --add: Test that executable bit is not used...' \
	'git config core.filemode 0 &&
	 echo foo >xfoo2 &&
	 chmod 755 xfoo2 &&
	 git-update-index --add xfoo2 &&
	 case "`git-ls-files --stage xfoo2`" in
	 100644" "*xfoo2) echo ok;;
	 *) echo fail; git-ls-files --stage xfoo2; (exit 1);;
	 esac'

test_expect_success 'git-add: filemode=0 should not get confused by symlink' '
	rm -f xfoo2 &&
	ln -s foo xfoo2 &&
	git update-index --add xfoo2 &&
	case "`git-ls-files --stage xfoo2`" in
	120000" "*xfoo2) echo ok;;
	*) echo fail; git-ls-files --stage xfoo2; (exit 1);;
	esac
'

test_expect_success \
	'git-update-index --add: Test that executable bit is not used...' \
	'git config core.filemode 0 &&
	 ln -s xfoo2 xfoo3 &&
	 git-update-index --add xfoo3 &&
	 case "`git-ls-files --stage xfoo3`" in
	 120000" "*xfoo3) echo ok;;
	 *) echo fail; git-ls-files --stage xfoo3; (exit 1);;
	 esac'

test_expect_success '.gitignore test setup' '
	echo "*.ig" >.gitignore &&
	mkdir c.if d.ig &&
	>a.ig && >b.if &&
	>c.if/c.if && >c.if/c.ig &&
	>d.ig/d.if && >d.ig/d.ig
'

test_expect_success '.gitignore is honored' '
	git-add . &&
	! git-ls-files | grep "\\.ig"
'

test_expect_success 'error out when attempting to add ignored ones without -f' '
	! git-add a.?? &&
	! git-ls-files | grep "\\.ig"
'

test_expect_success 'error out when attempting to add ignored ones without -f' '
	! git-add d.?? &&
	! git-ls-files | grep "\\.ig"
'

test_expect_success 'add ignored ones with -f' '
	git-add -f a.?? &&
	git-ls-files --error-unmatch a.ig
'

test_expect_success 'add ignored ones with -f' '
	git-add -f d.??/* &&
	git-ls-files --error-unmatch d.ig/d.if d.ig/d.ig
'

mkdir 1 1/2 1/3
touch 1/2/a 1/3/b 1/2/c
test_expect_success 'check correct prefix detection' '
	git add 1/2/a 1/3/b 1/2/c
'

test_done
