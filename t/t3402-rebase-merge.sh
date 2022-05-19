#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='but rebase --merge test'

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
	but add original &&
	but cummit -m"initial" &&
	but branch side &&
	echo "11 $T" >>original &&
	but cummit -a -m"main updates a bit." &&

	echo "12 $T" >>original &&
	but cummit -a -m"main updates a bit more." &&

	but checkout side &&
	(echo "0 $T" && cat original) >renamed &&
	but add renamed &&
	but update-index --force-remove original &&
	but cummit -a -m"side renames and edits." &&

	tr "[a-z]" "[A-Z]" <original >newfile &&
	but add newfile &&
	but cummit -a -m"side edits further." &&
	but branch second-side &&

	tr "[a-m]" "[A-M]" <original >newfile &&
	rm -f original &&
	but cummit -a -m"side edits once again." &&

	but branch test-rebase side &&
	but branch test-rebase-pick side &&
	but branch test-reference-pick side &&
	but branch test-conflicts side &&
	but checkout -b test-merge side
'

test_expect_success 'reference merge' '
	but merge -s recursive -m "reference merge" main
'

PRE_REBASE=$(but rev-parse test-rebase)
test_expect_success rebase '
	but checkout test-rebase &&
	GIT_TRACE=1 but rebase --merge main
'

test_expect_success 'test-rebase@{1} is pre rebase' '
	test $PRE_REBASE = $(but rev-parse test-rebase@{1})
'

test_expect_success 'merge and rebase should match' '
	but diff-tree -r test-rebase test-merge >difference &&
	if test -s difference
	then
		cat difference
		false
	else
		echo happy
	fi
'

test_expect_success 'rebase the other way' '
	but reset --hard main &&
	but rebase --merge side
'

test_expect_success 'rebase -Xtheirs' '
	but checkout -b conflicting main~2 &&
	echo "AB $T" >> original &&
	but cummit -mconflicting original &&
	but rebase -Xtheirs main &&
	grep AB original &&
	! grep 11 original
'

test_expect_success 'rebase -Xtheirs from orphan' '
	but checkout --orphan orphan-conflicting main~2 &&
	echo "AB $T" >> original &&
	but cummit -morphan-conflicting original &&
	but rebase -Xtheirs main &&
	grep AB original &&
	! grep 11 original
'

test_expect_success 'merge and rebase should match' '
	but diff-tree -r test-rebase test-merge >difference &&
	if test -s difference
	then
		cat difference
		false
	else
		echo happy
	fi
'

test_expect_success 'picking rebase' '
	but reset --hard side &&
	but rebase --merge --onto main side^^ &&
	mb=$(but merge-base main HEAD) &&
	if test "$mb" = "$(but rev-parse main)"
	then
		echo happy
	else
		but show-branch
		false
	fi &&
	f=$(but diff-tree --name-only HEAD^ HEAD) &&
	g=$(but diff-tree --name-only HEAD^^ HEAD^) &&
	case "$f,$g" in
	newfile,newfile)
		echo happy ;;
	*)
		echo "$f"
		echo "$g"
		false
	esac
'

test_expect_success 'rebase -s funny -Xopt' '
	test_when_finished "rm -fr test-bin funny.was.run" &&
	mkdir test-bin &&
	cat >test-bin/but-merge-funny <<-EOF &&
	#!$SHELL_PATH
	case "\$1" in --opt) ;; *) exit 2 ;; esac
	shift &&
	>funny.was.run &&
	exec but merge-recursive "\$@"
	EOF
	chmod +x test-bin/but-merge-funny &&
	but reset --hard &&
	but checkout -b test-funny main^ &&
	test_cummit funny &&
	(
		PATH=./test-bin:$PATH &&
		but rebase -s funny -Xopt main
	) &&
	test -f funny.was.run
'

test_expect_success 'rebase --skip works with two conflicts in a row' '
	but checkout second-side  &&
	tr "[A-Z]" "[a-z]" <newfile >tmp &&
	mv tmp newfile &&
	but cummit -a -m"edit conflicting with side" &&
	tr "[d-f]" "[D-F]" <newfile >tmp &&
	mv tmp newfile &&
	but cummit -a -m"another edit conflicting with side" &&
	test_must_fail but rebase --merge test-conflicts &&
	test_must_fail but rebase --skip &&
	but rebase --skip
'

test_expect_success '--reapply-cherry-picks' '
	but init repo &&

	# O(1-10) -- O(1-11) -- O(0-10) main
	#        \
	#         -- O(1-11) -- O(1-12) otherbranch

	printf "Line %d\n" $(test_seq 1 10) >repo/file.txt &&
	but -C repo add file.txt &&
	but -C repo cummit -m "base cummit" &&

	printf "Line %d\n" $(test_seq 1 11) >repo/file.txt &&
	but -C repo cummit -a -m "add 11" &&

	printf "Line %d\n" $(test_seq 0 10) >repo/file.txt &&
	but -C repo cummit -a -m "add 0 delete 11" &&

	but -C repo checkout -b otherbranch HEAD^^ &&
	printf "Line %d\n" $(test_seq 1 11) >repo/file.txt &&
	but -C repo cummit -a -m "add 11 in another branch" &&

	printf "Line %d\n" $(test_seq 1 12) >repo/file.txt &&
	but -C repo cummit -a -m "add 12 in another branch" &&

	# Regular rebase fails, because the 1-11 cummit is deduplicated
	test_must_fail but -C repo rebase --merge main 2> err &&
	test_i18ngrep "error: could not apply.*add 12 in another branch" err &&
	but -C repo rebase --abort &&

	# With --reapply-cherry-picks, it works
	but -C repo rebase --merge --reapply-cherry-picks main
'

test_expect_success '--reapply-cherry-picks refrains from reading unneeded blobs' '
	but init server &&

	# O(1-10) -- O(1-11) -- O(1-12) main
	#        \
	#         -- O(0-10) otherbranch

	printf "Line %d\n" $(test_seq 1 10) >server/file.txt &&
	but -C server add file.txt &&
	but -C server cummit -m "merge base" &&

	printf "Line %d\n" $(test_seq 1 11) >server/file.txt &&
	but -C server cummit -a -m "add 11" &&

	printf "Line %d\n" $(test_seq 1 12) >server/file.txt &&
	but -C server cummit -a -m "add 12" &&

	but -C server checkout -b otherbranch HEAD^^ &&
	printf "Line %d\n" $(test_seq 0 10) >server/file.txt &&
	but -C server cummit -a -m "add 0" &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&

	but clone --filter=blob:none "file://$(pwd)/server" client &&
	but -C client checkout origin/main &&
	but -C client checkout origin/otherbranch &&

	# Sanity check to ensure that the blobs from the merge base and "add
	# 11" are missing
	but -C client rev-list --objects --all --missing=print >missing_list &&
	MERGE_BASE_BLOB=$(but -C server rev-parse main^^:file.txt) &&
	ADD_11_BLOB=$(but -C server rev-parse main^:file.txt) &&
	grep "[?]$MERGE_BASE_BLOB" missing_list &&
	grep "[?]$ADD_11_BLOB" missing_list &&

	but -C client rebase --merge --reapply-cherry-picks origin/main &&

	# The blob from the merge base had to be fetched, but not "add 11"
	but -C client rev-list --objects --all --missing=print >missing_list &&
	! grep "[?]$MERGE_BASE_BLOB" missing_list &&
	grep "[?]$ADD_11_BLOB" missing_list
'

test_done
