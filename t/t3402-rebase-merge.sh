#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git rebase --merge test'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
	git commit -a -m"main updates a bit." &&

	echo "12 $T" >>original &&
	git commit -a -m"main updates a bit more." &&

	git checkout side &&
	(echo "0 $T" && cat original) >renamed &&
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

	git branch test-rebase side &&
	git branch test-rebase-pick side &&
	git branch test-reference-pick side &&
	git branch test-conflicts side &&
	git checkout -b test-merge side
'

test_expect_success 'reference merge' '
	git merge -s recursive -m "reference merge" main
'

PRE_REBASE=$(git rev-parse test-rebase)
test_expect_success rebase '
	git checkout test-rebase &&
	GIT_TRACE=1 git rebase --merge main
'

test_expect_success 'test-rebase@{1} is pre rebase' '
	test $PRE_REBASE = $(git rev-parse test-rebase@{1})
'

test_expect_success 'merge and rebase should match' '
	git diff-tree -r test-rebase test-merge >difference &&
	if test -s difference
	then
		cat difference
		(exit 1)
	else
		echo happy
	fi
'

test_expect_success 'rebase the other way' '
	git reset --hard main &&
	git rebase --merge side
'

test_expect_success 'rebase -Xtheirs' '
	git checkout -b conflicting main~2 &&
	echo "AB $T" >> original &&
	git commit -mconflicting original &&
	git rebase -Xtheirs main &&
	grep AB original &&
	! grep 11 original
'

test_expect_success 'rebase -Xtheirs from orphan' '
	git checkout --orphan orphan-conflicting main~2 &&
	echo "AB $T" >> original &&
	git commit -morphan-conflicting original &&
	git rebase -Xtheirs main &&
	grep AB original &&
	! grep 11 original
'

test_expect_success 'merge and rebase should match' '
	git diff-tree -r test-rebase test-merge >difference &&
	if test -s difference
	then
		cat difference
		(exit 1)
	else
		echo happy
	fi
'

test_expect_success 'picking rebase' '
	git reset --hard side &&
	git rebase --merge --onto main side^^ &&
	mb=$(git merge-base main HEAD) &&
	if test "$mb" = "$(git rev-parse main)"
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

test_expect_success 'rebase -s funny -Xopt' '
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
	git checkout -b test-funny main^ &&
	test_commit funny &&
	(
		PATH=./test-bin:$PATH &&
		git rebase -s funny -Xopt main
	) &&
	test -f funny.was.run
'

test_expect_success 'rebase --skip works with two conflicts in a row' '
	git checkout second-side  &&
	tr "[A-Z]" "[a-z]" <newfile >tmp &&
	mv tmp newfile &&
	git commit -a -m"edit conflicting with side" &&
	tr "[d-f]" "[D-F]" <newfile >tmp &&
	mv tmp newfile &&
	git commit -a -m"another edit conflicting with side" &&
	test_must_fail git rebase --merge test-conflicts &&
	test_must_fail git rebase --skip &&
	git rebase --skip
'

test_expect_success '--reapply-cherry-picks' '
	git init repo &&

	# O(1-10) -- O(1-11) -- O(0-10) main
	#        \
	#         -- O(1-11) -- O(1-12) otherbranch

	printf "Line %d\n" $(test_seq 1 10) >repo/file.txt &&
	git -C repo add file.txt &&
	git -C repo commit -m "base commit" &&

	printf "Line %d\n" $(test_seq 1 11) >repo/file.txt &&
	git -C repo commit -a -m "add 11" &&

	printf "Line %d\n" $(test_seq 0 10) >repo/file.txt &&
	git -C repo commit -a -m "add 0 delete 11" &&

	git -C repo checkout -b otherbranch HEAD^^ &&
	printf "Line %d\n" $(test_seq 1 11) >repo/file.txt &&
	git -C repo commit -a -m "add 11 in another branch" &&

	printf "Line %d\n" $(test_seq 1 12) >repo/file.txt &&
	git -C repo commit -a -m "add 12 in another branch" &&

	# Regular rebase fails, because the 1-11 commit is deduplicated
	test_must_fail git -C repo rebase --merge main 2> err &&
	test_i18ngrep "error: could not apply.*add 12 in another branch" err &&
	git -C repo rebase --abort &&

	# With --reapply-cherry-picks, it works
	git -C repo rebase --merge --reapply-cherry-picks main
'

test_expect_success '--reapply-cherry-picks refrains from reading unneeded blobs' '
	git init server &&

	# O(1-10) -- O(1-11) -- O(1-12) main
	#        \
	#         -- O(0-10) otherbranch

	printf "Line %d\n" $(test_seq 1 10) >server/file.txt &&
	git -C server add file.txt &&
	git -C server commit -m "merge base" &&

	printf "Line %d\n" $(test_seq 1 11) >server/file.txt &&
	git -C server commit -a -m "add 11" &&

	printf "Line %d\n" $(test_seq 1 12) >server/file.txt &&
	git -C server commit -a -m "add 12" &&

	git -C server checkout -b otherbranch HEAD^^ &&
	printf "Line %d\n" $(test_seq 0 10) >server/file.txt &&
	git -C server commit -a -m "add 0" &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&

	git clone --filter=blob:none "file://$(pwd)/server" client &&
	git -C client checkout origin/main &&
	git -C client checkout origin/otherbranch &&

	# Sanity check to ensure that the blobs from the merge base and "add
	# 11" are missing
	git -C client rev-list --objects --all --missing=print >missing_list &&
	MERGE_BASE_BLOB=$(git -C server rev-parse main^^:file.txt) &&
	ADD_11_BLOB=$(git -C server rev-parse main^:file.txt) &&
	grep "[?]$MERGE_BASE_BLOB" missing_list &&
	grep "[?]$ADD_11_BLOB" missing_list &&

	git -C client rebase --merge --reapply-cherry-picks origin/main &&

	# The blob from the merge base had to be fetched, but not "add 11"
	git -C client rev-list --objects --all --missing=print >missing_list &&
	! grep "[?]$MERGE_BASE_BLOB" missing_list &&
	grep "[?]$ADD_11_BLOB" missing_list
'

test_done
