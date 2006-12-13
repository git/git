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

git checkout white &&
sed -e "/^g /s/.*/g : white changes a line/" <A >B &&
sed -e "/^G /s/.*/G : colored branch changes a line/" <M >N &&
rm -f A M &&
git update-index --add --remove A B M N &&
git commit -m "white renames A->B, M->N" &&

git checkout red &&
echo created by red >R &&
git update-index --add R &&
git commit -m "red creates R" &&

git checkout master'

# This test broke in 65ac6e9c3f47807cb603af07a6a9e1a43bc119ae
test_expect_success 'merge white into red (A->B,M->N)' \
'
	git checkout -b red-white red &&
	git merge white &&
	git write-tree >/dev/null || {
		echo "BAD: merge did not complete"
		return 1
	}

	test -f B || {
		echo "BAD: B does not exist in working directory"
		return 1
	}
	test -f N || {
		echo "BAD: N does not exist in working directory"
		return 1
	}
	test -f R || {
		echo "BAD: R does not exist in working directory"
		return 1
	}

	test -f A && {
		echo "BAD: A still exists in working directory"
		return 1
	}
	test -f M && {
		echo "BAD: M still exists in working directory"
		return 1
	}
	return 0
'

test_done
