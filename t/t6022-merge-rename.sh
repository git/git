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

git checkout master'

test_expect_success 'pull renaming branch into unrenaming one' \
'
	git show-branch
	git pull . white && {
		echo "BAD: should have conflicted"
		return 1
	}
	git ls-files -s
	test "$(git ls-files -u B | wc -l)" -eq 3 || {
		echo "BAD: should have left stages for B"
		return 1
	}
	test "$(git ls-files -s N | wc -l)" -eq 1 || {
		echo "BAD: should have merged N"
		return 1
	}
	sed -ne "/^g/{
	p
	q
	}" B | grep master || {
		echo "BAD: should have listed our change first"
		return 1
	}
	test "$(git diff white N | wc -l)" -eq 0 || {
		echo "BAD: should have taken colored branch"
		return 1
	}
'

test_expect_success 'pull renaming branch into another renaming one' \
'
	rm -f B
	git reset --hard
	git checkout red
	git pull . white && {
		echo "BAD: should have conflicted"
		return 1
	}
	test "$(git ls-files -u B | wc -l)" -eq 3 || {
		echo "BAD: should have left stages"
		return 1
	}
	test "$(git ls-files -s N | wc -l)" -eq 1 || {
		echo "BAD: should have merged N"
		return 1
	}
	sed -ne "/^g/{
	p
	q
	}" B | grep red || {
		echo "BAD: should have listed our change first"
		return 1
	}
	test "$(git diff white N | wc -l)" -eq 0 || {
		echo "BAD: should have taken colored branch"
		return 1
	}
'

test_expect_success 'pull unrenaming branch into renaming one' \
'
	git reset --hard
	git show-branch
	git pull . master && {
		echo "BAD: should have conflicted"
		return 1
	}
	test "$(git ls-files -u B | wc -l)" -eq 3 || {
		echo "BAD: should have left stages"
		return 1
	}
	test "$(git ls-files -s N | wc -l)" -eq 1 || {
		echo "BAD: should have merged N"
		return 1
	}
	sed -ne "/^g/{
	p
	q
	}" B | grep red || {
		echo "BAD: should have listed our change first"
		return 1
	}
	test "$(git diff white N | wc -l)" -eq 0 || {
		echo "BAD: should have taken colored branch"
		return 1
	}
'

test_expect_success 'pull conflicting renames' \
'
	git reset --hard
	git show-branch
	git pull . blue && {
		echo "BAD: should have conflicted"
		return 1
	}
	test "$(git ls-files -u A | wc -l)" -eq 1 || {
		echo "BAD: should have left a stage"
		return 1
	}
	test "$(git ls-files -u B | wc -l)" -eq 1 || {
		echo "BAD: should have left a stage"
		return 1
	}
	test "$(git ls-files -u C | wc -l)" -eq 1 || {
		echo "BAD: should have left a stage"
		return 1
	}
	test "$(git ls-files -s N | wc -l)" -eq 1 || {
		echo "BAD: should have merged N"
		return 1
	}
	sed -ne "/^g/{
	p
	q
	}" B | grep red || {
		echo "BAD: should have listed our change first"
		return 1
	}
	test "$(git diff white N | wc -l)" -eq 0 || {
		echo "BAD: should have taken colored branch"
		return 1
	}
'

test_expect_success 'interference with untracked working tree file' '

	git reset --hard
	git show-branch
	echo >A this file should not matter
	git pull . white && {
		echo "BAD: should have conflicted"
		return 1
	}
	test -f A || {
		echo "BAD: should have left A intact"
		return 1
	}
'

test_expect_success 'interference with untracked working tree file' '

	git reset --hard
	git checkout white
	git show-branch
	rm -f A
	echo >A this file should not matter
	git pull . red && {
		echo "BAD: should have conflicted"
		return 1
	}
	test -f A || {
		echo "BAD: should have left A intact"
		return 1
	}
'

test_expect_success 'interference with untracked working tree file' '

	git reset --hard
	rm -f A M
	git checkout -f master
	git tag -f anchor
	git show-branch
	git pull . yellow || {
		echo "BAD: should have cleanly merged"
		return 1
	}
	test -f M && {
		echo "BAD: should have removed M"
		return 1
	}
	git reset --hard anchor
'

test_expect_success 'updated working tree file should prevent the merge' '

	git reset --hard
	rm -f A M
	git checkout -f master
	git tag -f anchor
	git show-branch
	echo >>M one line addition
	cat M >M.saved
	git pull . yellow && {
		echo "BAD: should have complained"
		return 1
	}
	diff M M.saved || {
		echo "BAD: should have left M intact"
		return 1
	}
	rm -f M.saved
'

test_expect_success 'updated working tree file should prevent the merge' '

	git reset --hard
	rm -f A M
	git checkout -f master
	git tag -f anchor
	git show-branch
	echo >>M one line addition
	cat M >M.saved
	git update-index M
	git pull . yellow && {
		echo "BAD: should have complained"
		return 1
	}
	diff M M.saved || {
		echo "BAD: should have left M intact"
		return 1
	}
	rm -f M.saved
'

test_expect_success 'interference with untracked working tree file' '

	git reset --hard
	rm -f A M
	git checkout -f yellow
	git tag -f anchor
	git show-branch
	echo >M this file should not matter
	git pull . master || {
		echo "BAD: should have cleanly merged"
		return 1
	}
	test -f M || {
		echo "BAD: should have left M intact"
		return 1
	}
	git ls-files -s | grep M && {
		echo "BAD: M must be untracked in the result"
		return 1
	}
	git reset --hard anchor
'

test_done
