#!/bin/sh

test_description='Merge-recursive merging renames'
. ./test-lib.sh

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

test_done
