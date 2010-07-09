#!/bin/sh

test_description='git init'

. ./test-lib.sh

check_config () {
	if test -d "$1" && test -f "$1/config" && test -d "$1/refs"
	then
		: happy
	else
		echo "expected a directory $1, a file $1/config and $1/refs"
		return 1
	fi
	bare=$(GIT_CONFIG="$1/config" git config --bool core.bare)
	worktree=$(GIT_CONFIG="$1/config" git config core.worktree) ||
	worktree=unset

	test "$bare" = "$2" && test "$worktree" = "$3" || {
		echo "expected bare=$2 worktree=$3"
		echo "     got bare=$bare worktree=$worktree"
		return 1
	}
}

test_expect_success 'plain' '
	(
		unset GIT_DIR GIT_WORK_TREE
		mkdir plain &&
		cd plain &&
		git init
	) &&
	check_config plain/.git false unset
'

test_expect_success 'plain with GIT_WORK_TREE' '
	if (
		unset GIT_DIR
		mkdir plain-wt &&
		cd plain-wt &&
		GIT_WORK_TREE=$(pwd) git init
	)
	then
		echo Should have failed -- GIT_WORK_TREE should not be used
		false
	fi
'

test_expect_success 'plain bare' '
	(
		unset GIT_DIR GIT_WORK_TREE GIT_CONFIG
		mkdir plain-bare-1 &&
		cd plain-bare-1 &&
		git --bare init
	) &&
	check_config plain-bare-1 true unset
'

test_expect_success 'plain bare with GIT_WORK_TREE' '
	if (
		unset GIT_DIR GIT_CONFIG
		mkdir plain-bare-2 &&
		cd plain-bare-2 &&
		GIT_WORK_TREE=$(pwd) git --bare init
	)
	then
		echo Should have failed -- GIT_WORK_TREE should not be used
		false
	fi
'

test_expect_success 'GIT_DIR bare' '

	(
		unset GIT_CONFIG
		mkdir git-dir-bare.git &&
		GIT_DIR=git-dir-bare.git git init
	) &&
	check_config git-dir-bare.git true unset
'

test_expect_success 'init --bare' '

	(
		unset GIT_DIR GIT_WORK_TREE GIT_CONFIG
		mkdir init-bare.git &&
		cd init-bare.git &&
		git init --bare
	) &&
	check_config init-bare.git true unset
'

test_expect_success 'GIT_DIR non-bare' '

	(
		unset GIT_CONFIG
		mkdir non-bare &&
		cd non-bare &&
		GIT_DIR=.git git init
	) &&
	check_config non-bare/.git false unset
'

test_expect_success 'GIT_DIR & GIT_WORK_TREE (1)' '

	(
		unset GIT_CONFIG
		mkdir git-dir-wt-1.git &&
		GIT_WORK_TREE=$(pwd) GIT_DIR=git-dir-wt-1.git git init
	) &&
	check_config git-dir-wt-1.git false "$(pwd)"
'

test_expect_success 'GIT_DIR & GIT_WORK_TREE (2)' '

	if (
		unset GIT_CONFIG
		mkdir git-dir-wt-2.git &&
		GIT_WORK_TREE=$(pwd) GIT_DIR=git-dir-wt-2.git git --bare init
	)
	then
		echo Should have failed -- --bare should not be used
		false
	fi
'

test_expect_success 'reinit' '

	(
		unset GIT_CONFIG GIT_WORK_TREE GIT_CONFIG

		mkdir again &&
		cd again &&
		git init >out1 2>err1 &&
		git init >out2 2>err2
	) &&
	grep "Initialized empty" again/out1 &&
	grep "Reinitialized existing" again/out2 &&
	>again/empty &&
	test_cmp again/empty again/err1 &&
	test_cmp again/empty again/err2
'

test_expect_success 'init with --template' '
	mkdir template-source &&
	echo content >template-source/file &&
	(
		mkdir template-custom &&
		cd template-custom &&
		git init --template=../template-source
	) &&
	test_cmp template-source/file template-custom/.git/file
'

test_expect_success 'init with --template (blank)' '
	(
		mkdir template-plain &&
		cd template-plain &&
		git init
	) &&
	test -f template-plain/.git/info/exclude &&
	(
		mkdir template-blank &&
		cd template-blank &&
		git init --template=
	) &&
	! test -f template-blank/.git/info/exclude
'

test_expect_success 'init with init.templatedir set' '
	mkdir templatedir-source &&
	echo Content >templatedir-source/file &&
	(
		HOME="`pwd`" &&
		export HOME &&
		test_config="${HOME}/.gitconfig" &&
		git config -f "$test_config"  init.templatedir "${HOME}/templatedir-source" &&
		mkdir templatedir-set &&
		cd templatedir-set &&
		unset GIT_CONFIG_NOGLOBAL &&
		unset GIT_TEMPLATE_DIR &&
		NO_SET_GIT_TEMPLATE_DIR=t &&
		export NO_SET_GIT_TEMPLATE_DIR &&
		git init
	) &&
	test_cmp templatedir-source/file templatedir-set/.git/file
'

test_expect_success 'init --bare/--shared overrides system/global config' '
	(
		HOME="`pwd`" &&
		export HOME &&
		test_config="$HOME"/.gitconfig &&
		unset GIT_CONFIG_NOGLOBAL &&
		git config -f "$test_config" core.bare false &&
		git config -f "$test_config" core.sharedRepository 0640 &&
		mkdir init-bare-shared-override &&
		cd init-bare-shared-override &&
		git init --bare --shared=0666
	) &&
	check_config init-bare-shared-override true unset &&
	test x0666 = \
	x`git config -f init-bare-shared-override/config core.sharedRepository`
'

test_expect_success 'init honors global core.sharedRepository' '
	(
		HOME="`pwd`" &&
		export HOME &&
		test_config="$HOME"/.gitconfig &&
		unset GIT_CONFIG_NOGLOBAL &&
		git config -f "$test_config" core.sharedRepository 0666 &&
		mkdir shared-honor-global &&
		cd shared-honor-global &&
		git init
	) &&
	test x0666 = \
	x`git config -f shared-honor-global/.git/config core.sharedRepository`
'

test_expect_success 'init rejects insanely long --template' '
	(
		insane=$(printf "x%09999dx" 1) &&
		mkdir test &&
		cd test &&
		test_must_fail git init --template=$insane
	)
'

test_expect_success 'init creates a new directory' '
	rm -fr newdir &&
	(
		git init newdir &&
		test -d newdir/.git/refs
	)
'

test_expect_success 'init creates a new bare directory' '
	rm -fr newdir &&
	(
		git init --bare newdir &&
		test -d newdir/refs
	)
'

test_expect_success 'init recreates a directory' '
	rm -fr newdir &&
	(
		mkdir newdir &&
		git init newdir &&
		test -d newdir/.git/refs
	)
'

test_expect_success 'init recreates a new bare directory' '
	rm -fr newdir &&
	(
		mkdir newdir &&
		git init --bare newdir &&
		test -d newdir/refs
	)
'

test_expect_success 'init creates a new deep directory' '
	rm -fr newdir &&
	git init newdir/a/b/c &&
	test -d newdir/a/b/c/.git/refs
'

test_expect_success POSIXPERM 'init creates a new deep directory (umask vs. shared)' '
	rm -fr newdir &&
	(
		# Leading directories should honor umask while
		# the repository itself should follow "shared"
		umask 002 &&
		git init --bare --shared=0660 newdir/a/b/c &&
		test -d newdir/a/b/c/refs &&
		ls -ld newdir/a newdir/a/b > lsab.out &&
		! grep -v "^drwxrw[sx]r-x" lsab.out &&
		ls -ld newdir/a/b/c > lsc.out &&
		! grep -v "^drwxrw[sx]---" lsc.out
	)
'

test_expect_success 'init notices EEXIST (1)' '
	rm -fr newdir &&
	(
		>newdir &&
		test_must_fail git init newdir &&
		test -f newdir
	)
'

test_expect_success 'init notices EEXIST (2)' '
	rm -fr newdir &&
	(
		mkdir newdir &&
		>newdir/a
		test_must_fail git init newdir/a/b &&
		test -f newdir/a
	)
'

test_expect_success POSIXPERM 'init notices EPERM' '
	rm -fr newdir &&
	(
		mkdir newdir &&
		chmod -w newdir &&
		test_must_fail git init newdir/a/b
	)
'

test_expect_success 'init creates a new bare directory with global --bare' '
	rm -rf newdir &&
	git --bare init newdir &&
	test -d newdir/refs
'

test_expect_success 'init prefers command line to GIT_DIR' '
	rm -rf newdir &&
	mkdir otherdir &&
	GIT_DIR=otherdir git --bare init newdir &&
	test -d newdir/refs &&
	! test -d otherdir/refs
'

test_done
