#!/bin/sh

test_description='Merge-recursive merging renames'
. ./test-lib.sh

modify () {
	sed -e "$1" <"$2" >"$2.x" &&
	mv "$2.x" "$2"
}

test_expect_success setup \
'
cat >A <<\EOF &&
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

cat >M <<\EOF &&
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

git add A M &&
git commit -m "initial has A and M" &&
git branch white &&
git branch red &&
git branch blue &&
git branch yellow &&
git branch change &&
git branch change+rename &&

sed -e "/^g /s/.*/g : master changes a line/" <A >A+ &&
mv A+ A &&
git commit -a -m "master updates A" &&

git checkout yellow &&
rm -f M &&
git commit -a -m "yellow removes M" &&

git checkout white &&
sed -e "/^g /s/.*/g : white changes a line/" <A >B &&
sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
rm -f A M &&
git update-index --add --remove A B M N &&
git commit -m "white renames A->B, M->N" &&

git checkout red &&
sed -e "/^g /s/.*/g : red changes a line/" <A >B &&
sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
rm -f A M &&
git update-index --add --remove A B M N &&
git commit -m "red renames A->B, M->N" &&

git checkout blue &&
sed -e "/^g /s/.*/g : blue changes a line/" <A >C &&
sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
rm -f A M &&
git update-index --add --remove A C M N &&
git commit -m "blue renames A->C, M->N" &&

git checkout change &&
sed -e "/^g /s/.*/g : changed line/" <A >A+ &&
mv A+ A &&
git commit -q -a -m "changed" &&

git checkout change+rename &&
sed -e "/^g /s/.*/g : changed line/" <A >B &&
rm A &&
git update-index --add B &&
git commit -q -a -m "changed and renamed" &&

git checkout master'

test_expect_success 'pull renaming branch into unrenaming one' \
'
	git show-branch &&
	test_expect_code 1 git pull . white &&
	git ls-files -s &&
	git ls-files -u B >b.stages &&
	test_line_count = 3 b.stages &&
	git ls-files -s N >n.stages &&
	test_line_count = 1 n.stages &&
	sed -ne "/^g/{
	p
	q
	}" B | grep master &&
	git diff --exit-code white N
'

test_expect_success 'pull renaming branch into another renaming one' \
'
	rm -f B &&
	git reset --hard &&
	git checkout red &&
	test_expect_code 1 git pull . white &&
	git ls-files -u B >b.stages &&
	test_line_count = 3 b.stages &&
	git ls-files -s N >n.stages &&
	test_line_count = 1 n.stages &&
	sed -ne "/^g/{
	p
	q
	}" B | grep red &&
	git diff --exit-code white N
'

test_expect_success 'pull unrenaming branch into renaming one' \
'
	git reset --hard &&
	git show-branch &&
	test_expect_code 1 git pull . master &&
	git ls-files -u B >b.stages &&
	test_line_count = 3 b.stages &&
	git ls-files -s N >n.stages &&
	test_line_count = 1 n.stages &&
	sed -ne "/^g/{
	p
	q
	}" B | grep red &&
	git diff --exit-code white N
'

test_expect_success 'pull conflicting renames' \
'
	git reset --hard &&
	git show-branch &&
	test_expect_code 1 git pull . blue &&
	git ls-files -u A >a.stages &&
	test_line_count = 1 a.stages &&
	git ls-files -u B >b.stages &&
	test_line_count = 1 b.stages &&
	git ls-files -u C >c.stages &&
	test_line_count = 1 c.stages &&
	git ls-files -s N >n.stages &&
	test_line_count = 1 n.stages &&
	sed -ne "/^g/{
	p
	q
	}" B | grep red &&
	git diff --exit-code white N
'

test_expect_success 'interference with untracked working tree file' '
	git reset --hard &&
	git show-branch &&
	echo >A this file should not matter &&
	test_expect_code 1 git pull . white &&
	test_path_is_file A
'

test_expect_success 'interference with untracked working tree file' '
	git reset --hard &&
	git checkout white &&
	git show-branch &&
	rm -f A &&
	echo >A this file should not matter &&
	test_expect_code 1 git pull . red &&
	test_path_is_file A
'

test_expect_success 'interference with untracked working tree file' '
	git reset --hard &&
	rm -f A M &&
	git checkout -f master &&
	git tag -f anchor &&
	git show-branch &&
	git pull . yellow &&
	test_path_is_missing M &&
	git reset --hard anchor
'

test_expect_success 'updated working tree file should prevent the merge' '
	git reset --hard &&
	rm -f A M &&
	git checkout -f master &&
	git tag -f anchor &&
	git show-branch &&
	echo >>M one line addition &&
	cat M >M.saved &&
	test_expect_code 128 git pull . yellow &&
	test_cmp M M.saved &&
	rm -f M.saved
'

test_expect_success 'updated working tree file should prevent the merge' '
	git reset --hard &&
	rm -f A M &&
	git checkout -f master &&
	git tag -f anchor &&
	git show-branch &&
	echo >>M one line addition &&
	cat M >M.saved &&
	git update-index M &&
	test_expect_code 128 git pull . yellow &&
	test_cmp M M.saved &&
	rm -f M.saved
'

test_expect_success 'interference with untracked working tree file' '
	git reset --hard &&
	rm -f A M &&
	git checkout -f yellow &&
	git tag -f anchor &&
	git show-branch &&
	echo >M this file should not matter &&
	git pull . master &&
	test_path_is_file M &&
	! {
		git ls-files -s |
		grep M
	} &&
	git reset --hard anchor
'

test_expect_success 'merge of identical changes in a renamed file' '
	rm -f A M N &&
	git reset --hard &&
	git checkout change+rename &&
	GIT_MERGE_VERBOSITY=3 git merge change | grep "^Skipped B" &&
	git reset --hard HEAD^ &&
	git checkout change &&
	GIT_MERGE_VERBOSITY=3 git merge change+rename | grep "^Skipped B"
'

test_expect_success 'setup for rename + d/f conflicts' '
	git reset --hard &&
	git checkout --orphan dir-in-way &&
	git rm -rf . &&
	git clean -fdqx &&

	mkdir sub &&
	mkdir dir &&
	printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" >sub/file &&
	echo foo >dir/file-in-the-way &&
	git add -A &&
	git commit -m "Common commmit" &&

	echo 11 >>sub/file &&
	echo more >>dir/file-in-the-way &&
	git add -u &&
	git commit -m "Commit to merge, with dir in the way" &&

	git checkout -b dir-not-in-way &&
	git reset --soft HEAD^ &&
	git rm -rf dir &&
	git commit -m "Commit to merge, with dir removed" -- dir sub/file &&

	git checkout -b renamed-file-has-no-conflicts dir-in-way~1 &&
	git rm -rf dir &&
	git rm sub/file &&
	printf "1\n2\n3\n4\n5555\n6\n7\n8\n9\n10\n" >dir &&
	git add dir &&
	git commit -m "Independent change" &&

	git checkout -b renamed-file-has-conflicts dir-in-way~1 &&
	git rm -rf dir &&
	git mv sub/file dir &&
	echo 12 >>dir &&
	git add dir &&
	git commit -m "Conflicting change"
'

printf "1\n2\n3\n4\n5555\n6\n7\n8\n9\n10\n11\n" >expected

test_expect_success 'Rename+D/F conflict; renamed file merges + dir not in way' '
	git reset --hard &&
	git checkout -q renamed-file-has-no-conflicts^0 &&
	git merge --strategy=recursive dir-not-in-way &&
	git diff --quiet &&
	test -f dir &&
	test_cmp expected dir
'

test_expect_success 'Rename+D/F conflict; renamed file merges but dir in way' '
	git reset --hard &&
	rm -rf dir~* &&
	git checkout -q renamed-file-has-no-conflicts^0 &&
	test_must_fail git merge --strategy=recursive dir-in-way >output &&

	grep "CONFLICT (modify/delete): dir/file-in-the-way" output &&
	grep "Auto-merging dir" output &&
	grep "Adding as dir~HEAD instead" output &&

	test 3 -eq "$(git ls-files -u | wc -l)" &&
	test 2 -eq "$(git ls-files -u dir/file-in-the-way | wc -l)" &&

	test_must_fail git diff --quiet &&
	test_must_fail git diff --cached --quiet &&

	test -f dir/file-in-the-way &&
	test -f dir~HEAD &&
	test_cmp expected dir~HEAD
'

test_expect_success 'Same as previous, but merged other way' '
	git reset --hard &&
	rm -rf dir~* &&
	git checkout -q dir-in-way^0 &&
	test_must_fail git merge --strategy=recursive renamed-file-has-no-conflicts >output 2>errors &&

	! grep "error: refusing to lose untracked file at" errors &&
	grep "CONFLICT (modify/delete): dir/file-in-the-way" output &&
	grep "Auto-merging dir" output &&
	grep "Adding as dir~renamed-file-has-no-conflicts instead" output &&

	test 3 -eq "$(git ls-files -u | wc -l)" &&
	test 2 -eq "$(git ls-files -u dir/file-in-the-way | wc -l)" &&

	test_must_fail git diff --quiet &&
	test_must_fail git diff --cached --quiet &&

	test -f dir/file-in-the-way &&
	test -f dir~renamed-file-has-no-conflicts &&
	test_cmp expected dir~renamed-file-has-no-conflicts
'

cat >expected <<\EOF &&
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

test_expect_success 'Rename+D/F conflict; renamed file cannot merge, dir not in way' '
	git reset --hard &&
	rm -rf dir~* &&
	git checkout -q renamed-file-has-conflicts^0 &&
	test_must_fail git merge --strategy=recursive dir-not-in-way &&

	test 3 -eq "$(git ls-files -u | wc -l)" &&
	test 3 -eq "$(git ls-files -u dir | wc -l)" &&

	test_must_fail git diff --quiet &&
	test_must_fail git diff --cached --quiet &&

	test -f dir &&
	test_cmp expected dir
'

test_expect_success 'Rename+D/F conflict; renamed file cannot merge and dir in the way' '
	modify s/dir-not-in-way/dir-in-way/ expected &&

	git reset --hard &&
	rm -rf dir~* &&
	git checkout -q renamed-file-has-conflicts^0 &&
	test_must_fail git merge --strategy=recursive dir-in-way &&

	test 5 -eq "$(git ls-files -u | wc -l)" &&
	test 3 -eq "$(git ls-files -u dir | grep -v file-in-the-way | wc -l)" &&
	test 2 -eq "$(git ls-files -u dir/file-in-the-way | wc -l)" &&

	test_must_fail git diff --quiet &&
	test_must_fail git diff --cached --quiet &&

	test -f dir/file-in-the-way &&
	test -f dir~HEAD &&
	test_cmp expected dir~HEAD
'

cat >expected <<\EOF &&
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

test_expect_success 'Same as previous, but merged other way' '
	git reset --hard &&
	rm -rf dir~* &&
	git checkout -q dir-in-way^0 &&
	test_must_fail git merge --strategy=recursive renamed-file-has-conflicts &&

	test 5 -eq "$(git ls-files -u | wc -l)" &&
	test 3 -eq "$(git ls-files -u dir | grep -v file-in-the-way | wc -l)" &&
	test 2 -eq "$(git ls-files -u dir/file-in-the-way | wc -l)" &&

	test_must_fail git diff --quiet &&
	test_must_fail git diff --cached --quiet &&

	test -f dir/file-in-the-way &&
	test -f dir~renamed-file-has-conflicts &&
	test_cmp expected dir~renamed-file-has-conflicts
'

test_expect_success 'setup both rename source and destination involved in D/F conflict' '
	git reset --hard &&
	git checkout --orphan rename-dest &&
	git rm -rf . &&
	git clean -fdqx &&

	mkdir one &&
	echo stuff >one/file &&
	git add -A &&
	git commit -m "Common commmit" &&

	git mv one/file destdir &&
	git commit -m "Renamed to destdir" &&

	git checkout -b source-conflict HEAD~1 &&
	git rm -rf one &&
	mkdir destdir &&
	touch one destdir/foo &&
	git add -A &&
	git commit -m "Conflicts in the way"
'

test_expect_success 'both rename source and destination involved in D/F conflict' '
	git reset --hard &&
	rm -rf dir~* &&
	git checkout -q rename-dest^0 &&
	test_must_fail git merge --strategy=recursive source-conflict &&

	test 1 -eq "$(git ls-files -u | wc -l)" &&

	test_must_fail git diff --quiet &&

	test -f destdir/foo &&
	test -f one &&
	test -f destdir~HEAD &&
	test "stuff" = "$(cat destdir~HEAD)"
'

test_expect_success 'setup pair rename to parent of other (D/F conflicts)' '
	git reset --hard &&
	git checkout --orphan rename-two &&
	git rm -rf . &&
	git clean -fdqx &&

	mkdir one &&
	mkdir two &&
	echo stuff >one/file &&
	echo other >two/file &&
	git add -A &&
	git commit -m "Common commmit" &&

	git rm -rf one &&
	git mv two/file one &&
	git commit -m "Rename two/file -> one" &&

	git checkout -b rename-one HEAD~1 &&
	git rm -rf two &&
	git mv one/file two &&
	rm -r one &&
	git commit -m "Rename one/file -> two"
'

test_expect_success 'pair rename to parent of other (D/F conflicts) w/ untracked dir' '
	git checkout -q rename-one^0 &&
	mkdir one &&
	test_must_fail git merge --strategy=recursive rename-two &&

	test 2 -eq "$(git ls-files -u | wc -l)" &&
	test 1 -eq "$(git ls-files -u one | wc -l)" &&
	test 1 -eq "$(git ls-files -u two | wc -l)" &&

	test_must_fail git diff --quiet &&

	test 4 -eq $(find . | grep -v .git | wc -l) &&

	test -d one &&
	test -f one~rename-two &&
	test -f two &&
	test "other" = $(cat one~rename-two) &&
	test "stuff" = $(cat two)
'

test_expect_success 'pair rename to parent of other (D/F conflicts) w/ clean start' '
	git reset --hard &&
	git clean -fdqx &&
	test_must_fail git merge --strategy=recursive rename-two &&

	test 2 -eq "$(git ls-files -u | wc -l)" &&
	test 1 -eq "$(git ls-files -u one | wc -l)" &&
	test 1 -eq "$(git ls-files -u two | wc -l)" &&

	test_must_fail git diff --quiet &&

	test 3 -eq $(find . | grep -v .git | wc -l) &&

	test -f one &&
	test -f two &&
	test "other" = $(cat one) &&
	test "stuff" = $(cat two)
'

test_expect_success 'setup rename of one file to two, with directories in the way' '
	git reset --hard &&
	git checkout --orphan first-rename &&
	git rm -rf . &&
	git clean -fdqx &&

	echo stuff >original &&
	git add -A &&
	git commit -m "Common commmit" &&

	mkdir two &&
	>two/file &&
	git add two/file &&
	git mv original one &&
	git commit -m "Put two/file in the way, rename to one" &&

	git checkout -b second-rename HEAD~1 &&
	mkdir one &&
	>one/file &&
	git add one/file &&
	git mv original two &&
	git commit -m "Put one/file in the way, rename to two"
'

test_expect_success 'check handling of differently renamed file with D/F conflicts' '
	git checkout -q first-rename^0 &&
	test_must_fail git merge --strategy=recursive second-rename &&

	test 5 -eq "$(git ls-files -s | wc -l)" &&
	test 3 -eq "$(git ls-files -u | wc -l)" &&
	test 1 -eq "$(git ls-files -u one | wc -l)" &&
	test 1 -eq "$(git ls-files -u two | wc -l)" &&
	test 1 -eq "$(git ls-files -u original | wc -l)" &&
	test 2 -eq "$(git ls-files -o | wc -l)" &&

	test -f one/file &&
	test -f two/file &&
	test -f one~HEAD &&
	test -f two~second-rename &&
	! test -f original
'

test_expect_success 'setup rename one file to two; directories moving out of the way' '
	git reset --hard &&
	git checkout --orphan first-rename-redo &&
	git rm -rf . &&
	git clean -fdqx &&

	echo stuff >original &&
	mkdir one two &&
	touch one/file two/file &&
	git add -A &&
	git commit -m "Common commmit" &&

	git rm -rf one &&
	git mv original one &&
	git commit -m "Rename to one" &&

	git checkout -b second-rename-redo HEAD~1 &&
	git rm -rf two &&
	git mv original two &&
	git commit -m "Rename to two"
'

test_expect_success 'check handling of differently renamed file with D/F conflicts' '
	git checkout -q first-rename-redo^0 &&
	test_must_fail git merge --strategy=recursive second-rename-redo &&

	test 3 -eq "$(git ls-files -u | wc -l)" &&
	test 1 -eq "$(git ls-files -u one | wc -l)" &&
	test 1 -eq "$(git ls-files -u two | wc -l)" &&
	test 1 -eq "$(git ls-files -u original | wc -l)" &&
	test 0 -eq "$(git ls-files -o | wc -l)" &&

	test -f one &&
	test -f two &&
	! test -f original
'

test_expect_success 'setup avoid unnecessary update, normal rename' '
	git reset --hard &&
	git checkout --orphan avoid-unnecessary-update-1 &&
	git rm -rf . &&
	git clean -fdqx &&

	printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" >original &&
	git add -A &&
	git commit -m "Common commmit" &&

	git mv original rename &&
	echo 11 >>rename &&
	git add -u &&
	git commit -m "Renamed and modified" &&

	git checkout -b merge-branch-1 HEAD~1 &&
	echo "random content" >random-file &&
	git add -A &&
	git commit -m "Random, unrelated changes"
'

test_expect_success 'avoid unnecessary update, normal rename' '
	git checkout -q avoid-unnecessary-update-1^0 &&
	test-chmtime =1000000000 rename &&
	test-chmtime -v +0 rename >expect &&
	git merge merge-branch-1 &&
	test-chmtime -v +0 rename >actual &&
	test_cmp expect actual # "rename" should have stayed intact
'

test_expect_success 'setup to test avoiding unnecessary update, with D/F conflict' '
	git reset --hard &&
	git checkout --orphan avoid-unnecessary-update-2 &&
	git rm -rf . &&
	git clean -fdqx &&

	mkdir df &&
	printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" >df/file &&
	git add -A &&
	git commit -m "Common commmit" &&

	git mv df/file temp &&
	rm -rf df &&
	git mv temp df &&
	echo 11 >>df &&
	git add -u &&
	git commit -m "Renamed and modified" &&

	git checkout -b merge-branch-2 HEAD~1 &&
	>unrelated-change &&
	git add unrelated-change &&
	git commit -m "Only unrelated changes"
'

test_expect_success 'avoid unnecessary update, with D/F conflict' '
	git checkout -q avoid-unnecessary-update-2^0 &&
	test-chmtime =1000000000 df &&
	test-chmtime -v +0 df >expect &&
	git merge merge-branch-2 &&
	test-chmtime -v +0 df >actual &&
	test_cmp expect actual # "df" should have stayed intact
'

test_expect_success 'setup avoid unnecessary update, dir->(file,nothing)' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	>irrelevant &&
	mkdir df &&
	>df/file &&
	git add -A &&
	git commit -mA &&

	git checkout -b side
	git rm -rf df &&
	git commit -mB &&

	git checkout master &&
	git rm -rf df &&
	echo bla >df &&
	git add -A &&
	git commit -m "Add a newfile"
'

test_expect_success 'avoid unnecessary update, dir->(file,nothing)' '
	git checkout -q master^0 &&
	test-chmtime =1000000000 df &&
	test-chmtime -v +0 df >expect &&
	git merge side &&
	test-chmtime -v +0 df >actual &&
	test_cmp expect actual # "df" should have stayed intact
'

test_expect_success 'setup avoid unnecessary update, modify/delete' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	>irrelevant &&
	>file &&
	git add -A &&
	git commit -mA &&

	git checkout -b side
	git rm -f file &&
	git commit -m "Delete file" &&

	git checkout master &&
	echo bla >file &&
	git add -A &&
	git commit -m "Modify file"
'

test_expect_success 'avoid unnecessary update, modify/delete' '
	git checkout -q master^0 &&
	test-chmtime =1000000000 file &&
	test-chmtime -v +0 file >expect &&
	test_must_fail git merge side &&
	test-chmtime -v +0 file >actual &&
	test_cmp expect actual # "file" should have stayed intact
'

test_expect_success 'setup avoid unnecessary update, rename/add-dest' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	printf "1\n2\n3\n4\n5\n6\n7\n8\n" >file &&
	git add -A &&
	git commit -mA &&

	git checkout -b side
	cp file newfile &&
	git add -A &&
	git commit -m "Add file copy" &&

	git checkout master &&
	git mv file newfile &&
	git commit -m "Rename file"
'

test_expect_success 'avoid unnecessary update, rename/add-dest' '
	git checkout -q master^0 &&
	test-chmtime =1000000000 newfile &&
	test-chmtime -v +0 newfile >expect &&
	git merge side &&
	test-chmtime -v +0 newfile >actual &&
	test_cmp expect actual # "file" should have stayed intact
'

test_expect_success 'setup merge of rename + small change' '
	git reset --hard &&
	git checkout --orphan rename-plus-small-change &&
	git rm -rf . &&
	git clean -fdqx &&

	echo ORIGINAL >file &&
	git add file &&

	test_tick &&
	git commit -m Initial &&
	git checkout -b rename_branch &&
	git mv file renamed_file &&
	git commit -m Rename &&
	git checkout rename-plus-small-change &&
	echo NEW-VERSION >file &&
	git commit -a -m Reformat
'

test_expect_success 'merge rename + small change' '
	git merge rename_branch &&

	test 1 -eq $(git ls-files -s | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&
	test $(git rev-parse HEAD:renamed_file) = $(git rev-parse HEAD~1:file)
'

test_expect_success 'setup for use of extended merge markers' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	printf "1\n2\n3\n4\n5\n6\n7\n8\n" >original_file &&
	git add original_file &&
	git commit -mA &&

	git checkout -b rename &&
	echo 9 >>original_file &&
	git add original_file &&
	git mv original_file renamed_file &&
	git commit -mB &&

	git checkout master &&
	echo 8.5 >>original_file &&
	git add original_file &&
	git commit -mC
'

cat >expected <<\EOF &&
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
>>>>>>> master^0:original_file
EOF

test_expect_success 'merge master into rename has correct extended markers' '
	git checkout rename^0 &&
	test_must_fail git merge -s recursive master^0 &&
	test_cmp expected renamed_file
'

cat >expected <<\EOF &&
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

test_expect_success 'merge rename into master has correct extended markers' '
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git merge -s recursive rename^0 &&
	test_cmp expected renamed_file
'

test_expect_success 'setup spurious "refusing to lose untracked" message' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	> irrelevant_file &&
	printf "1\n2\n3\n4\n5\n6\n7\n8\n" >original_file &&
	git add irrelevant_file original_file &&
	git commit -mA &&

	git checkout -b rename &&
	git mv original_file renamed_file &&
	git commit -mB &&

	git checkout master &&
	git rm original_file &&
	git commit -mC
'

test_expect_success 'no spurious "refusing to lose untracked" message' '
	git checkout master^0 &&
	test_must_fail git merge rename^0 2>errors.txt &&
	! grep "refusing to lose untracked file" errors.txt
'

test_done
