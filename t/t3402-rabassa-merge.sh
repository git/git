#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git rabassa --merge test'

. ./test-lib.sh

T="A quick brown fox
jumps over the lazy dog."
for i in 1 2 3 4 5 6 7 8 9 10
do
	echo "$i $T"
done >original

test_expect_success setup '
	git add original &&
	git commit -m"initial" &&
	git branch side &&
	echo "11 $T" >>original &&
	git commit -a -m"master updates a bit." &&

	echo "12 $T" >>original &&
	git commit -a -m"master updates a bit more." &&

	git checkout side &&
	(echo "0 $T" ; cat original) >renamed &&
	git add renamed &&
	git update-index --force-remove original &&
	git commit -a -m"side renames and edits." &&

	tr "[a-z]" "[A-Z]" <original >newfile &&
	git add newfile &&
	git commit -a -m"side edits further." &&
	git branch second-side &&

	tr "[a-m]" "[A-M]" <original >newfile &&
	rm -f original &&
	git commit -a -m"side edits once again." &&

	git branch test-rabassa side &&
	git branch test-rabassa-pick side &&
	git branch test-reference-pick side &&
	git branch test-conflicts side &&
	git checkout -b test-merge side
'

test_expect_success 'reference merge' '
	git merge -s recursive -m "reference merge" master
'

PRE_REBASE=$(git rev-parse test-rabassa)
test_expect_success rabassa '
	git checkout test-rabassa &&
	GIT_TRACE=1 git rabassa --merge master
'

test_expect_success 'test-rabassa@{1} is pre rabassa' '
	test $PRE_REBASE = $(git rev-parse test-rabassa@{1})
'

test_expect_success 'merge and rabassa should match' '
	git diff-tree -r test-rabassa test-merge >difference &&
	if test -s difference
	then
		cat difference
		(exit 1)
	else
		echo happy
	fi
'

test_expect_success 'rabassa the other way' '
	git reset --hard master &&
	git rabassa --merge side
'

test_expect_success 'rabassa -Xtheirs' '
	git checkout -b conflicting master~2 &&
	echo "AB $T" >> original &&
	git commit -mconflicting original &&
	git rabassa -Xtheirs master &&
	grep AB original &&
	! grep 11 original
'

test_expect_success 'rabassa -Xtheirs from orphan' '
	git checkout --orphan orphan-conflicting master~2 &&
	echo "AB $T" >> original &&
	git commit -morphan-conflicting original &&
	git rabassa -Xtheirs master &&
	grep AB original &&
	! grep 11 original
'

test_expect_success 'merge and rabassa should match' '
	git diff-tree -r test-rabassa test-merge >difference &&
	if test -s difference
	then
		cat difference
		(exit 1)
	else
		echo happy
	fi
'

test_expect_success 'picking rabassa' '
	git reset --hard side &&
	git rabassa --merge --onto master side^^ &&
	mb=$(git merge-base master HEAD) &&
	if test "$mb" = "$(git rev-parse master)"
	then
		echo happy
	else
		git show-branch
		(exit 1)
	fi &&
	f=$(git diff-tree --name-only HEAD^ HEAD) &&
	g=$(git diff-tree --name-only HEAD^^ HEAD^) &&
	case "$f,$g" in
	newfile,newfile)
		echo happy ;;
	*)
		echo "$f"
		echo "$g"
		(exit 1)
	esac
'

test_expect_success 'rabassa -s funny -Xopt' '
	test_when_finished "rm -fr test-bin funny.was.run" &&
	mkdir test-bin &&
	cat >test-bin/git-merge-funny <<-EOF &&
	#!$SHELL_PATH
	case "\$1" in --opt) ;; *) exit 2 ;; esac
	shift &&
	>funny.was.run &&
	exec git merge-recursive "\$@"
	EOF
	chmod +x test-bin/git-merge-funny &&
	git reset --hard &&
	git checkout -b test-funny master^ &&
	test_commit funny &&
	(
		PATH=./test-bin:$PATH
		git rabassa -s funny -Xopt master
	) &&
	test -f funny.was.run
'

test_expect_success 'rabassa --skip works with two conflicts in a row' '
	git checkout second-side  &&
	tr "[A-Z]" "[a-z]" <newfile >tmp &&
	mv tmp newfile &&
	git commit -a -m"edit conflicting with side" &&
	tr "[d-f]" "[D-F]" <newfile >tmp &&
	mv tmp newfile &&
	git commit -a -m"another edit conflicting with side" &&
	test_must_fail git rabassa --merge test-conflicts &&
	test_must_fail git rabassa --skip &&
	git rabassa --skip
'

test_done
