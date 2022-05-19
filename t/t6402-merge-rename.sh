#!/bin/sh

test_description='Merge-recursive merging renames'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

modify () {
	sed -e "$1" <"$2" >"$2.x" &&
	mv "$2.x" "$2"
}

test_expect_success 'setup' '
	cat >A <<-\EOF &&
	a aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
	b bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
	c cccccccccccccccccccccccccccccccccccccccccccccccc
	d dddddddddddddddddddddddddddddddddddddddddddddddd
	e eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
	f ffffffffffffffffffffffffffffffffffffffffffffffff
	g gggggggggggggggggggggggggggggggggggggggggggggggg
	h hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh
	i iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii
	j jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj
	k kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk
	l llllllllllllllllllllllllllllllllllllllllllllllll
	m mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm
	n nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
	o oooooooooooooooooooooooooooooooooooooooooooooooo
	EOF

	cat >M <<-\EOF &&
	A AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
	B BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
	C CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
	D DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD
	E EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE
	F FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
	G GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG
	H HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
	I IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII
	J JJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJ
	K KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
	L LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
	M MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
	N NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN
	O OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
	EOF

	but add A M &&
	but cummit -m "initial has A and M" &&
	but branch white &&
	but branch red &&
	but branch blue &&
	but branch yellow &&
	but branch change &&
	but branch change+rename &&

	sed -e "/^g /s/.*/g : main changes a line/" <A >A+ &&
	mv A+ A &&
	but cummit -a -m "main updates A" &&

	but checkout yellow &&
	rm -f M &&
	but cummit -a -m "yellow removes M" &&

	but checkout white &&
	sed -e "/^g /s/.*/g : white changes a line/" <A >B &&
	sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
	rm -f A M &&
	but update-index --add --remove A B M N &&
	but cummit -m "white renames A->B, M->N" &&

	but checkout red &&
	sed -e "/^g /s/.*/g : red changes a line/" <A >B &&
	sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
	rm -f A M &&
	but update-index --add --remove A B M N &&
	but cummit -m "red renames A->B, M->N" &&

	but checkout blue &&
	sed -e "/^g /s/.*/g : blue changes a line/" <A >C &&
	sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
	rm -f A M &&
	but update-index --add --remove A C M N &&
	but cummit -m "blue renames A->C, M->N" &&

	but checkout change &&
	sed -e "/^g /s/.*/g : changed line/" <A >A+ &&
	mv A+ A &&
	but cummit -q -a -m "changed" &&

	but checkout change+rename &&
	sed -e "/^g /s/.*/g : changed line/" <A >B &&
	rm A &&
	but update-index --add B &&
	but cummit -q -a -m "changed and renamed" &&

	but checkout main
'

test_expect_success 'pull renaming branch into unrenaming one' \
'
	but show-branch &&
	test_expect_code 1 but pull --no-rebase . white &&
	but ls-files -s &&
	test_stdout_line_count = 3 but ls-files -u B &&
	test_stdout_line_count = 1 but ls-files -s N &&
	sed -ne "/^g/{
	p
	q
	}" B | grep main &&
	but diff --exit-code white N
'

test_expect_success 'pull renaming branch into another renaming one' \
'
	rm -f B &&
	but reset --hard &&
	but checkout red &&
	test_expect_code 1 but pull --no-rebase . white &&
	test_stdout_line_count = 3 but ls-files -u B &&
	test_stdout_line_count = 1 but ls-files -s N &&
	sed -ne "/^g/{
	p
	q
	}" B | grep red &&
	but diff --exit-code white N
'

test_expect_success 'pull unrenaming branch into renaming one' \
'
	but reset --hard &&
	but show-branch &&
	test_expect_code 1 but pull --no-rebase . main &&
	test_stdout_line_count = 3 but ls-files -u B &&
	test_stdout_line_count = 1 but ls-files -s N &&
	sed -ne "/^g/{
	p
	q
	}" B | grep red &&
	but diff --exit-code white N
'

test_expect_success 'pull conflicting renames' \
'
	but reset --hard &&
	but show-branch &&
	test_expect_code 1 but pull --no-rebase . blue &&
	test_stdout_line_count = 1 but ls-files -u A &&
	test_stdout_line_count = 1 but ls-files -u B &&
	test_stdout_line_count = 1 but ls-files -u C &&
	test_stdout_line_count = 1 but ls-files -s N &&
	sed -ne "/^g/{
	p
	q
	}" B | grep red &&
	but diff --exit-code white N
'

test_expect_success 'interference with untracked working tree file' '
	but reset --hard &&
	but show-branch &&
	echo >A this file should not matter &&
	test_expect_code 1 but pull --no-rebase . white &&
	test_path_is_file A
'

test_expect_success 'interference with untracked working tree file' '
	but reset --hard &&
	but checkout white &&
	but show-branch &&
	rm -f A &&
	echo >A this file should not matter &&
	test_expect_code 1 but pull --no-rebase . red &&
	test_path_is_file A
'

test_expect_success 'interference with untracked working tree file' '
	but reset --hard &&
	rm -f A M &&
	but checkout -f main &&
	but tag -f anchor &&
	but show-branch &&
	but pull --no-rebase . yellow &&
	test_path_is_missing M &&
	but reset --hard anchor
'

test_expect_success 'updated working tree file should prevent the merge' '
	but reset --hard &&
	rm -f A M &&
	but checkout -f main &&
	but tag -f anchor &&
	but show-branch &&
	echo >>M one line addition &&
	cat M >M.saved &&
	test_expect_code 128 but pull . yellow &&
	test_cmp M M.saved &&
	rm -f M.saved
'

test_expect_success 'updated working tree file should prevent the merge' '
	but reset --hard &&
	rm -f A M &&
	but checkout -f main &&
	but tag -f anchor &&
	but show-branch &&
	echo >>M one line addition &&
	cat M >M.saved &&
	but update-index M &&
	test_expect_code 128 but pull --no-rebase . yellow &&
	test_cmp M M.saved &&
	rm -f M.saved
'

test_expect_success 'interference with untracked working tree file' '
	but reset --hard &&
	rm -f A M &&
	but checkout -f yellow &&
	but tag -f anchor &&
	but show-branch &&
	echo >M this file should not matter &&
	but pull --no-rebase . main &&
	test_path_is_file M &&
	! {
		but ls-files -s |
		grep M
	} &&
	but reset --hard anchor
'

test_expect_success 'merge of identical changes in a renamed file' '
	rm -f A M N &&
	but reset --hard &&
	but checkout change+rename &&

	test-tool chmtime --get -3600 B >old-mtime &&
	BUT_MERGE_VERBOSITY=3 but merge change >out &&

	test-tool chmtime --get B >new-mtime &&
	test_cmp old-mtime new-mtime &&

	but reset --hard HEAD^ &&
	but checkout change &&

	# A will be renamed to B; we check mtimes and file presence
	test_path_is_missing B &&
	test-tool chmtime --get -3600 A >old-mtime &&
	BUT_MERGE_VERBOSITY=3 but merge change+rename >out &&

	test_path_is_missing A &&
	test-tool chmtime --get B >new-mtime &&
	test $(cat old-mtime) -lt $(cat new-mtime)
'

test_expect_success 'setup for rename + d/f conflicts' '
	but reset --hard &&
	but checkout --orphan dir-in-way &&
	but rm -rf . &&
	but clean -fdqx &&

	mkdir sub &&
	mkdir dir &&
	printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" >sub/file &&
	echo foo >dir/file-in-the-way &&
	but add -A &&
	but cummit -m "Common cummit" &&

	echo 11 >>sub/file &&
	echo more >>dir/file-in-the-way &&
	but add -u &&
	but cummit -m "cummit to merge, with dir in the way" &&

	but checkout -b dir-not-in-way &&
	but reset --soft HEAD^ &&
	but rm -rf dir &&
	but cummit -m "cummit to merge, with dir removed" -- dir sub/file &&

	but checkout -b renamed-file-has-no-conflicts dir-in-way~1 &&
	but rm -rf dir &&
	but rm sub/file &&
	printf "1\n2\n3\n4\n5555\n6\n7\n8\n9\n10\n" >dir &&
	but add dir &&
	but cummit -m "Independent change" &&

	but checkout -b renamed-file-has-conflicts dir-in-way~1 &&
	but rm -rf dir &&
	but mv sub/file dir &&
	echo 12 >>dir &&
	but add dir &&
	but cummit -m "Conflicting change"
'

test_expect_success 'Rename+D/F conflict; renamed file merges + dir not in way' '
	but reset --hard &&
	but checkout -q renamed-file-has-no-conflicts^0 &&

	but merge --strategy=recursive dir-not-in-way &&

	but diff --quiet &&
	test_path_is_file dir &&
	test_write_lines 1 2 3 4 5555 6 7 8 9 10 11 >expected &&
	test_cmp expected dir
'

test_expect_success 'Rename+D/F conflict; renamed file merges but dir in way' '
	but reset --hard &&
	rm -rf dir~* &&
	but checkout -q renamed-file-has-no-conflicts^0 &&
	test_must_fail but merge --strategy=recursive dir-in-way >output &&

	test_i18ngrep "CONFLICT (modify/delete): dir/file-in-the-way" output &&
	test_i18ngrep "Auto-merging dir" output &&
	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_i18ngrep "moving it to dir~HEAD instead" output
	else
		test_i18ngrep "Adding as dir~HEAD instead" output
	fi &&

	test_stdout_line_count = 3 but ls-files -u &&
	test_stdout_line_count = 2 but ls-files -u dir/file-in-the-way &&

	test_must_fail but diff --quiet &&
	test_must_fail but diff --cached --quiet &&

	test_path_is_file dir/file-in-the-way &&
	test_path_is_file dir~HEAD &&
	test_cmp expected dir~HEAD
'

test_expect_success 'Same as previous, but merged other way' '
	but reset --hard &&
	rm -rf dir~* &&
	but checkout -q dir-in-way^0 &&
	test_must_fail but merge --strategy=recursive renamed-file-has-no-conflicts >output 2>errors &&

	! grep "error: refusing to lose untracked file at" errors &&
	test_i18ngrep "CONFLICT (modify/delete): dir/file-in-the-way" output &&
	test_i18ngrep "Auto-merging dir" output &&
	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_i18ngrep "moving it to dir~renamed-file-has-no-conflicts instead" output
	else
		test_i18ngrep "Adding as dir~renamed-file-has-no-conflicts instead" output
	fi &&

	test_stdout_line_count = 3 but ls-files -u &&
	test_stdout_line_count = 2 but ls-files -u dir/file-in-the-way &&

	test_must_fail but diff --quiet &&
	test_must_fail but diff --cached --quiet &&

	test_path_is_file dir/file-in-the-way &&
	test_path_is_file dir~renamed-file-has-no-conflicts &&
	test_cmp expected dir~renamed-file-has-no-conflicts
'

test_expect_success 'Rename+D/F conflict; renamed file cannot merge, dir not in way' '
	but reset --hard &&
	rm -rf dir~* &&
	but checkout -q renamed-file-has-conflicts^0 &&
	test_must_fail but merge --strategy=recursive dir-not-in-way &&

	test_stdout_line_count = 3 but ls-files -u &&
	test_stdout_line_count = 3 but ls-files -u dir &&

	test_must_fail but diff --quiet &&
	test_must_fail but diff --cached --quiet &&

	test_path_is_file dir &&
	cat >expected <<-\EOF &&
	1
	2
	3
	4
	5
	6
	7
	8
	9
	10
	<<<<<<< HEAD:dir
	12
	=======
	11
	>>>>>>> dir-not-in-way:sub/file
	EOF
	test_cmp expected dir
'

test_expect_success 'Rename+D/F conflict; renamed file cannot merge and dir in the way' '
	modify s/dir-not-in-way/dir-in-way/ expected &&

	but reset --hard &&
	rm -rf dir~* &&
	but checkout -q renamed-file-has-conflicts^0 &&
	test_must_fail but merge --strategy=recursive dir-in-way &&

	test_stdout_line_count = 5 but ls-files -u &&
	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 3 but ls-files -u dir~HEAD
	else
		but ls-files -u dir >out &&
		test 3 -eq $(grep -v file-in-the-way out | wc -l) &&
		rm -f out
	fi &&
	test_stdout_line_count = 2 but ls-files -u dir/file-in-the-way &&

	test_must_fail but diff --quiet &&
	test_must_fail but diff --cached --quiet &&

	test_path_is_file dir/file-in-the-way &&
	test_path_is_file dir~HEAD &&
	test_cmp expected dir~HEAD
'

test_expect_success 'Same as previous, but merged other way' '
	but reset --hard &&
	rm -rf dir~* &&
	but checkout -q dir-in-way^0 &&
	test_must_fail but merge --strategy=recursive renamed-file-has-conflicts &&

	test_stdout_line_count = 5 but ls-files -u &&
	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 3 but ls-files -u dir~renamed-file-has-conflicts
	else
		but ls-files -u dir >out &&
		test 3 -eq $(grep -v file-in-the-way out | wc -l) &&
		rm -f out
	fi &&
	test_stdout_line_count = 2 but ls-files -u dir/file-in-the-way &&

	test_must_fail but diff --quiet &&
	test_must_fail but diff --cached --quiet &&

	test_path_is_file dir/file-in-the-way &&
	test_path_is_file dir~renamed-file-has-conflicts &&
	cat >expected <<-\EOF &&
	1
	2
	3
	4
	5
	6
	7
	8
	9
	10
	<<<<<<< HEAD:sub/file
	11
	=======
	12
	>>>>>>> renamed-file-has-conflicts:dir
	EOF
	test_cmp expected dir~renamed-file-has-conflicts
'

test_expect_success 'setup both rename source and destination involved in D/F conflict' '
	but reset --hard &&
	but checkout --orphan rename-dest &&
	but rm -rf . &&
	but clean -fdqx &&

	mkdir one &&
	echo stuff >one/file &&
	but add -A &&
	but cummit -m "Common cummit" &&

	but mv one/file destdir &&
	but cummit -m "Renamed to destdir" &&

	but checkout -b source-conflict HEAD~1 &&
	but rm -rf one &&
	mkdir destdir &&
	touch one destdir/foo &&
	but add -A &&
	but cummit -m "Conflicts in the way"
'

test_expect_success 'both rename source and destination involved in D/F conflict' '
	but reset --hard &&
	rm -rf dir~* &&
	but checkout -q rename-dest^0 &&
	test_must_fail but merge --strategy=recursive source-conflict &&

	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 2 but ls-files -u
	else
		test_stdout_line_count = 1 but ls-files -u
	fi &&

	test_must_fail but diff --quiet &&

	test_path_is_file destdir/foo &&
	test_path_is_file one &&
	test_path_is_file destdir~HEAD &&
	test "stuff" = "$(cat destdir~HEAD)"
'

test_expect_success 'setup pair rename to parent of other (D/F conflicts)' '
	but reset --hard &&
	but checkout --orphan rename-two &&
	but rm -rf . &&
	but clean -fdqx &&

	mkdir one &&
	mkdir two &&
	echo stuff >one/file &&
	echo other >two/file &&
	but add -A &&
	but cummit -m "Common cummit" &&

	but rm -rf one &&
	but mv two/file one &&
	but cummit -m "Rename two/file -> one" &&

	but checkout -b rename-one HEAD~1 &&
	but rm -rf two &&
	but mv one/file two &&
	rm -r one &&
	but cummit -m "Rename one/file -> two"
'

if test "$BUT_TEST_MERGE_ALGORITHM" = ort
then
	test_expect_success 'pair rename to parent of other (D/F conflicts) w/ untracked dir' '
		but checkout -q rename-one^0 &&
		mkdir one &&
		test_must_fail but merge --strategy=recursive rename-two &&

		test_stdout_line_count = 4 but ls-files -u &&
		test_stdout_line_count = 2 but ls-files -u one &&
		test_stdout_line_count = 2 but ls-files -u two &&

		test_must_fail but diff --quiet &&

		test 3 -eq $(find . | grep -v .but | wc -l) &&

		test_path_is_file one &&
		test_path_is_file two &&
		test "other" = $(cat one) &&
		test "stuff" = $(cat two)
	'
else
	test_expect_success 'pair rename to parent of other (D/F conflicts) w/ untracked dir' '
		but checkout -q rename-one^0 &&
		mkdir one &&
		test_must_fail but merge --strategy=recursive rename-two &&

		test_stdout_line_count = 2 but ls-files -u &&
		test_stdout_line_count = 1 but ls-files -u one &&
		test_stdout_line_count = 1 but ls-files -u two &&

		test_must_fail but diff --quiet &&

		test 4 -eq $(find . | grep -v .but | wc -l) &&

		test_path_is_dir one &&
		test_path_is_file one~rename-two &&
		test_path_is_file two &&
		test "other" = $(cat one~rename-two) &&
		test "stuff" = $(cat two)
	'
fi

test_expect_success 'pair rename to parent of other (D/F conflicts) w/ clean start' '
	but reset --hard &&
	but clean -fdqx &&
	test_must_fail but merge --strategy=recursive rename-two &&

	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 4 but ls-files -u &&
		test_stdout_line_count = 2 but ls-files -u one &&
		test_stdout_line_count = 2 but ls-files -u two
	else
		test_stdout_line_count = 2 but ls-files -u &&
		test_stdout_line_count = 1 but ls-files -u one &&
		test_stdout_line_count = 1 but ls-files -u two
	fi &&

	test_must_fail but diff --quiet &&

	test 3 -eq $(find . | grep -v .but | wc -l) &&

	test_path_is_file one &&
	test_path_is_file two &&
	test "other" = $(cat one) &&
	test "stuff" = $(cat two)
'

test_expect_success 'setup rename of one file to two, with directories in the way' '
	but reset --hard &&
	but checkout --orphan first-rename &&
	but rm -rf . &&
	but clean -fdqx &&

	echo stuff >original &&
	but add -A &&
	but cummit -m "Common cummit" &&

	mkdir two &&
	>two/file &&
	but add two/file &&
	but mv original one &&
	but cummit -m "Put two/file in the way, rename to one" &&

	but checkout -b second-rename HEAD~1 &&
	mkdir one &&
	>one/file &&
	but add one/file &&
	but mv original two &&
	but cummit -m "Put one/file in the way, rename to two"
'

test_expect_success 'check handling of differently renamed file with D/F conflicts' '
	but checkout -q first-rename^0 &&
	test_must_fail but merge --strategy=recursive second-rename &&

	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 5 but ls-files -s &&
		test_stdout_line_count = 3 but ls-files -u &&
		test_stdout_line_count = 1 but ls-files -u one~HEAD &&
		test_stdout_line_count = 1 but ls-files -u two~second-rename &&
		test_stdout_line_count = 1 but ls-files -u original &&
		test_stdout_line_count = 0 but ls-files -o
	else
		test_stdout_line_count = 5 but ls-files -s &&
		test_stdout_line_count = 3 but ls-files -u &&
		test_stdout_line_count = 1 but ls-files -u one &&
		test_stdout_line_count = 1 but ls-files -u two &&
		test_stdout_line_count = 1 but ls-files -u original &&
		test_stdout_line_count = 2 but ls-files -o
	fi &&

	test_path_is_file one/file &&
	test_path_is_file two/file &&
	test_path_is_file one~HEAD &&
	test_path_is_file two~second-rename &&
	test_path_is_missing original
'

test_expect_success 'setup rename one file to two; directories moving out of the way' '
	but reset --hard &&
	but checkout --orphan first-rename-redo &&
	but rm -rf . &&
	but clean -fdqx &&

	echo stuff >original &&
	mkdir one two &&
	touch one/file two/file &&
	but add -A &&
	but cummit -m "Common cummit" &&

	but rm -rf one &&
	but mv original one &&
	but cummit -m "Rename to one" &&

	but checkout -b second-rename-redo HEAD~1 &&
	but rm -rf two &&
	but mv original two &&
	but cummit -m "Rename to two"
'

test_expect_success 'check handling of differently renamed file with D/F conflicts' '
	but checkout -q first-rename-redo^0 &&
	test_must_fail but merge --strategy=recursive second-rename-redo &&

	test_stdout_line_count = 3 but ls-files -u &&
	test_stdout_line_count = 1 but ls-files -u one &&
	test_stdout_line_count = 1 but ls-files -u two &&
	test_stdout_line_count = 1 but ls-files -u original &&
	test_stdout_line_count = 0 but ls-files -o &&

	test_path_is_file one &&
	test_path_is_file two &&
	test_path_is_missing original
'

test_expect_success 'setup avoid unnecessary update, normal rename' '
	but reset --hard &&
	but checkout --orphan avoid-unnecessary-update-1 &&
	but rm -rf . &&
	but clean -fdqx &&

	printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" >original &&
	but add -A &&
	but cummit -m "Common cummit" &&

	but mv original rename &&
	echo 11 >>rename &&
	but add -u &&
	but cummit -m "Renamed and modified" &&

	but checkout -b merge-branch-1 HEAD~1 &&
	echo "random content" >random-file &&
	but add -A &&
	but cummit -m "Random, unrelated changes"
'

test_expect_success 'avoid unnecessary update, normal rename' '
	but checkout -q avoid-unnecessary-update-1^0 &&
	test-tool chmtime --get -3600 rename >expect &&
	but merge merge-branch-1 &&
	test-tool chmtime --get rename >actual &&
	test_cmp expect actual # "rename" should have stayed intact
'

test_expect_success 'setup to test avoiding unnecessary update, with D/F conflict' '
	but reset --hard &&
	but checkout --orphan avoid-unnecessary-update-2 &&
	but rm -rf . &&
	but clean -fdqx &&

	mkdir df &&
	printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" >df/file &&
	but add -A &&
	but cummit -m "Common cummit" &&

	but mv df/file temp &&
	rm -rf df &&
	but mv temp df &&
	echo 11 >>df &&
	but add -u &&
	but cummit -m "Renamed and modified" &&

	but checkout -b merge-branch-2 HEAD~1 &&
	>unrelated-change &&
	but add unrelated-change &&
	but cummit -m "Only unrelated changes"
'

test_expect_success 'avoid unnecessary update, with D/F conflict' '
	but checkout -q avoid-unnecessary-update-2^0 &&
	test-tool chmtime --get -3600 df >expect &&
	but merge merge-branch-2 &&
	test-tool chmtime --get df >actual &&
	test_cmp expect actual # "df" should have stayed intact
'

test_expect_success 'setup avoid unnecessary update, dir->(file,nothing)' '
	but rm -rf . &&
	but clean -fdqx &&
	rm -rf .but &&
	but init &&

	>irrelevant &&
	mkdir df &&
	>df/file &&
	but add -A &&
	but cummit -mA &&

	but checkout -b side &&
	but rm -rf df &&
	but cummit -mB &&

	but checkout main &&
	but rm -rf df &&
	echo bla >df &&
	but add -A &&
	but cummit -m "Add a newfile"
'

test_expect_success 'avoid unnecessary update, dir->(file,nothing)' '
	but checkout -q main^0 &&
	test-tool chmtime --get -3600 df >expect &&
	but merge side &&
	test-tool chmtime --get df >actual &&
	test_cmp expect actual # "df" should have stayed intact
'

test_expect_success 'setup avoid unnecessary update, modify/delete' '
	but rm -rf . &&
	but clean -fdqx &&
	rm -rf .but &&
	but init &&

	>irrelevant &&
	>file &&
	but add -A &&
	but cummit -mA &&

	but checkout -b side &&
	but rm -f file &&
	but cummit -m "Delete file" &&

	but checkout main &&
	echo bla >file &&
	but add -A &&
	but cummit -m "Modify file"
'

test_expect_success 'avoid unnecessary update, modify/delete' '
	but checkout -q main^0 &&
	test-tool chmtime --get -3600 file >expect &&
	test_must_fail but merge side &&
	test-tool chmtime --get file >actual &&
	test_cmp expect actual # "file" should have stayed intact
'

test_expect_success 'setup avoid unnecessary update, rename/add-dest' '
	but rm -rf . &&
	but clean -fdqx &&
	rm -rf .but &&
	but init &&

	printf "1\n2\n3\n4\n5\n6\n7\n8\n" >file &&
	but add -A &&
	but cummit -mA &&

	but checkout -b side &&
	cp file newfile &&
	but add -A &&
	but cummit -m "Add file copy" &&

	but checkout main &&
	but mv file newfile &&
	but cummit -m "Rename file"
'

test_expect_success 'avoid unnecessary update, rename/add-dest' '
	but checkout -q main^0 &&
	test-tool chmtime --get -3600 newfile >expect &&
	but merge side &&
	test-tool chmtime --get newfile >actual &&
	test_cmp expect actual # "file" should have stayed intact
'

test_expect_success 'setup merge of rename + small change' '
	but reset --hard &&
	but checkout --orphan rename-plus-small-change &&
	but rm -rf . &&
	but clean -fdqx &&

	echo ORIGINAL >file &&
	but add file &&

	test_tick &&
	but cummit -m Initial &&
	but checkout -b rename_branch &&
	but mv file renamed_file &&
	but cummit -m Rename &&
	but checkout rename-plus-small-change &&
	echo NEW-VERSION >file &&
	but cummit -a -m Reformat
'

test_expect_success 'merge rename + small change' '
	but merge rename_branch &&

	test_stdout_line_count = 1 but ls-files -s &&
	test_stdout_line_count = 0 but ls-files -o &&
	newhash=$(but rev-parse HEAD:renamed_file) &&
	oldhash=$(but rev-parse HEAD~1:file) &&
	test $newhash = $oldhash
'

test_expect_success 'setup for use of extended merge markers' '
	but rm -rf . &&
	but clean -fdqx &&
	rm -rf .but &&
	but init &&

	printf "1\n2\n3\n4\n5\n6\n7\n8\n" >original_file &&
	but add original_file &&
	but cummit -mA &&

	but checkout -b rename &&
	echo 9 >>original_file &&
	but add original_file &&
	but mv original_file renamed_file &&
	but cummit -mB &&

	but checkout main &&
	echo 8.5 >>original_file &&
	but add original_file &&
	but cummit -mC
'

test_expect_success 'merge main into rename has correct extended markers' '
	but checkout rename^0 &&
	test_must_fail but merge -s recursive main^0 &&

	cat >expected <<-\EOF &&
	1
	2
	3
	4
	5
	6
	7
	8
	<<<<<<< HEAD:renamed_file
	9
	=======
	8.5
	>>>>>>> main^0:original_file
	EOF
	test_cmp expected renamed_file
'

test_expect_success 'merge rename into main has correct extended markers' '
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but merge -s recursive rename^0 &&

	cat >expected <<-\EOF &&
	1
	2
	3
	4
	5
	6
	7
	8
	<<<<<<< HEAD:original_file
	8.5
	=======
	9
	>>>>>>> rename^0:renamed_file
	EOF
	test_cmp expected renamed_file
'

test_expect_success 'setup spurious "refusing to lose untracked" message' '
	but rm -rf . &&
	but clean -fdqx &&
	rm -rf .but &&
	but init &&

	> irrelevant_file &&
	printf "1\n2\n3\n4\n5\n6\n7\n8\n" >original_file &&
	but add irrelevant_file original_file &&
	but cummit -mA &&

	but checkout -b rename &&
	but mv original_file renamed_file &&
	but cummit -mB &&

	but checkout main &&
	but rm original_file &&
	but cummit -mC
'

test_expect_success 'no spurious "refusing to lose untracked" message' '
	but checkout main^0 &&
	test_must_fail but merge rename^0 2>errors.txt &&
	! grep "refusing to lose untracked file" errors.txt
'

test_expect_success 'do not follow renames for empty files' '
	but checkout -f -b empty-base &&
	>empty1 &&
	but add empty1 &&
	but cummit -m base &&
	echo content >empty1 &&
	but add empty1 &&
	but cummit -m fill &&
	but checkout -b empty-topic HEAD^ &&
	but mv empty1 empty2 &&
	but cummit -m rename &&
	test_must_fail but merge empty-base &&
	test_must_be_empty empty2
'

test_done
