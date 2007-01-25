#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Try various core-level commands in subdirectory.
'

. ./test-lib.sh

test_expect_success setup '
	long="a b c d e f g h i j k l m n o p q r s t u v w x y z" &&
	for c in $long; do echo $c; done >one &&
	mkdir dir &&
	for c in x y z $long a b c; do echo $c; done >dir/two &&
	cp one original.one &&
	cp dir/two original.two
'
HERE=`pwd`
LF='
'

test_expect_success 'update-index and ls-files' '
	cd $HERE &&
	git-update-index --add one &&
	case "`git-ls-files`" in
	one) echo ok one ;;
	*) echo bad one; exit 1 ;;
	esac &&
	cd dir &&
	git-update-index --add two &&
	case "`git-ls-files`" in
	two) echo ok two ;;
	*) echo bad two; exit 1 ;;
	esac &&
	cd .. &&
	case "`git-ls-files`" in
	dir/two"$LF"one) echo ok both ;;
	*) echo bad; exit 1 ;;
	esac
'

test_expect_success 'cat-file' '
	cd $HERE &&
	two=`git-ls-files -s dir/two` &&
	two=`expr "$two" : "[0-7]* \\([0-9a-f]*\\)"` &&
	echo "$two" &&
	git-cat-file -p "$two" >actual &&
	cmp dir/two actual &&
	cd dir &&
	git-cat-file -p "$two" >actual &&
	cmp two actual
'
rm -f actual dir/actual

test_expect_success 'diff-files' '
	cd $HERE &&
	echo a >>one &&
	echo d >>dir/two &&
	case "`git-diff-files --name-only`" in
	dir/two"$LF"one) echo ok top ;;
	*) echo bad top; exit 1 ;;
	esac &&
	# diff should not omit leading paths
	cd dir &&
	case "`git-diff-files --name-only`" in
	dir/two"$LF"one) echo ok subdir ;;
	*) echo bad subdir; exit 1 ;;
	esac &&
	case "`git-diff-files --name-only .`" in
	dir/two) echo ok subdir limited ;;
	*) echo bad subdir limited; exit 1 ;;
	esac
'

test_expect_success 'write-tree' '
	cd $HERE &&
	top=`git-write-tree` &&
	echo $top &&
	cd dir &&
	sub=`git-write-tree` &&
	echo $sub &&
	test "z$top" = "z$sub"
'

test_expect_success 'checkout-index' '
	cd $HERE &&
	git-checkout-index -f -u one &&
	cmp one original.one &&
	cd dir &&
	git-checkout-index -f -u two &&
	cmp two ../original.two
'

test_expect_success 'read-tree' '
	cd $HERE &&
	rm -f one dir/two &&
	tree=`git-write-tree` &&
	git-read-tree --reset -u "$tree" &&
	cmp one original.one &&
	cmp dir/two original.two &&
	cd dir &&
	rm -f two &&
	git-read-tree --reset -u "$tree" &&
	cmp two ../original.two &&
	cmp ../one ../original.one
'

test_expect_success 'no file/rev ambuguity check inside .git' '
	cd $HERE &&
	git commit -a -m 1 &&
	cd $HERE/.git &&
	git show -s HEAD
'

test_expect_success 'no file/rev ambuguity check inside a bare repo' '
	cd $HERE &&
	git clone -s --bare .git foo.git &&
	cd foo.git && GIT_DIR=. git show -s HEAD
'

# This still does not work as it should...
: test_expect_success 'no file/rev ambuguity check inside a bare repo' '
	cd $HERE &&
	git clone -s --bare .git foo.git &&
	cd foo.git && git show -s HEAD
'

test_expect_success 'detection should not be fooled by a symlink' '
	cd $HERE &&
	rm -fr foo.git &&
	git clone -s .git another &&
	ln -s another yetanother &&
	cd yetanother/.git &&
	git show -s HEAD
'

test_done
