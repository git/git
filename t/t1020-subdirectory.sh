#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Try various core-level commands in subdirectory.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

test_expect_success setup '
	long="a b c d e f g h i j k l m n o p q r s t u v w x y z" &&
	test_write_lines $long >one &&
	mkdir dir &&
	test_write_lines x y z $long a b c >dir/two &&
	cp one original.one &&
	cp dir/two original.two
'

test_expect_success 'update-index and ls-files' '
	but update-index --add one &&
	case "$(but ls-files)" in
	one) echo pass one ;;
	*) echo bad one; return 1 ;;
	esac &&
	(
		cd dir &&
		but update-index --add two &&
		case "$(but ls-files)" in
		two) echo pass two ;;
		*) echo bad two; exit 1 ;;
		esac
	) &&
	case "$(but ls-files)" in
	dir/two"$LF"one) echo pass both ;;
	*) echo bad; return 1 ;;
	esac
'

test_expect_success 'cat-file' '
	two=$(but ls-files -s dir/two) &&
	two=$(expr "$two" : "[0-7]* \\([0-9a-f]*\\)") &&
	echo "$two" &&
	but cat-file -p "$two" >actual &&
	cmp dir/two actual &&
	(
		cd dir &&
		but cat-file -p "$two" >actual &&
		cmp two actual
	)
'
rm -f actual dir/actual

test_expect_success 'diff-files' '
	echo a >>one &&
	echo d >>dir/two &&
	case "$(but diff-files --name-only)" in
	dir/two"$LF"one) echo pass top ;;
	*) echo bad top; return 1 ;;
	esac &&
	# diff should not omit leading paths
	(
		cd dir &&
		case "$(but diff-files --name-only)" in
		dir/two"$LF"one) echo pass subdir ;;
		*) echo bad subdir; exit 1 ;;
		esac &&
		case "$(but diff-files --name-only .)" in
		dir/two) echo pass subdir limited ;;
		*) echo bad subdir limited; exit 1 ;;
		esac
	)
'

test_expect_success 'write-tree' '
	top=$(but write-tree) &&
	echo $top &&
	(
		cd dir &&
		sub=$(but write-tree) &&
		echo $sub &&
		test "z$top" = "z$sub"
	)
'

test_expect_success 'checkout-index' '
	but checkout-index -f -u one &&
	cmp one original.one &&
	(
		cd dir &&
		but checkout-index -f -u two &&
		cmp two ../original.two
	)
'

test_expect_success 'read-tree' '
	rm -f one dir/two &&
	tree=$(but write-tree) &&
	read_tree_u_must_succeed --reset -u "$tree" &&
	cmp one original.one &&
	cmp dir/two original.two &&
	(
		cd dir &&
		rm -f two &&
		read_tree_u_must_succeed --reset -u "$tree" &&
		cmp two ../original.two &&
		cmp ../one ../original.one
	)
'

test_expect_success 'alias expansion' '
	(
		but config alias.test-status-alias status &&
		cd dir &&
		but status &&
		but test-status-alias
	)
'

test_expect_success !MINGW '!alias expansion' '
	pwd >expect &&
	(
		but config alias.test-alias-directory !pwd &&
		cd dir &&
		but test-alias-directory >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'GIT_PREFIX for !alias' '
	printf "dir/" >expect &&
	(
		but config alias.test-alias-directory "!sh -c \"printf \$GIT_PREFIX\"" &&
		cd dir &&
		but test-alias-directory >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'GIT_PREFIX for built-ins' '
	# Use GIT_EXTERNAL_DIFF to test that the "diff" built-in
	# receives the GIT_PREFIX variable.
	echo "dir/" >expect &&
	write_script diff <<-\EOF &&
	printf "%s\n" "$GIT_PREFIX"
	EOF
	(
		cd dir &&
		echo "change" >two &&
		GIT_EXTERNAL_DIFF=./diff but diff >../actual &&
		but checkout -- two
	) &&
	test_cmp expect actual
'

test_expect_success 'no file/rev ambiguity check inside .but' '
	but cummit -a -m 1 &&
	(
		cd .but &&
		but show -s HEAD
	)
'

test_expect_success 'no file/rev ambiguity check inside a bare repo (explicit GIT_DIR)' '
	test_when_finished "rm -fr foo.but" &&
	but clone -s --bare .but foo.but &&
	(
		cd foo.but &&
		# older Git needed help by exporting GIT_DIR=.
		# to realize that it is inside a bare repository.
		# We keep this test around for regression testing.
		GIT_DIR=. but show -s HEAD
	)
'

test_expect_success 'no file/rev ambiguity check inside a bare repo' '
	test_when_finished "rm -fr foo.but" &&
	but clone -s --bare .but foo.but &&
	(
		cd foo.but &&
		but show -s HEAD
	)
'

test_expect_success SYMLINKS 'detection should not be fooled by a symlink' '
	but clone -s .but another &&
	ln -s another yetanother &&
	(
		cd yetanother/.but &&
		but show -s HEAD
	)
'

test_done
