#!/bin/sh

test_description='Test merge without common ancestors'
. ./test-lib.sh

# This scenario is based on a real-world repository of Shawn Pearce.

# 1 - A - D - F
#   \   X   /
#     B   X
#       X   \
# 2 - C - E - G

GIT_COMMITTER_DATE="2006-12-12 23:28:00 +0100"
export GIT_COMMITTER_DATE

test_expect_success "setup tests" '
echo 1 > a1 &&
git add a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:00" git commit -m 1 a1 &&

git checkout -b A master &&
echo A > a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:01" git commit -m A a1 &&

git checkout -b B master &&
echo B > a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:02" git commit -m B a1 &&

git checkout -b D A &&
git rev-parse B > .git/MERGE_HEAD &&
echo D > a1 &&
git update-index a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:03" git commit -m D &&

git symbolic-ref HEAD refs/heads/other &&
echo 2 > a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:04" git commit -m 2 a1 &&

git checkout -b C &&
echo C > a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:05" git commit -m C a1 &&

git checkout -b E C &&
git rev-parse B > .git/MERGE_HEAD &&
echo E > a1 &&
git update-index a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:06" git commit -m E &&

git checkout -b G E &&
git rev-parse A > .git/MERGE_HEAD &&
echo G > a1 &&
git update-index a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:07" git commit -m G &&

git checkout -b F D &&
git rev-parse C > .git/MERGE_HEAD &&
echo F > a1 &&
git update-index a1 &&
GIT_AUTHOR_DATE="2006-12-12 23:00:08" git commit -m F
'

test_expect_success "combined merge conflicts" "! git merge -m final G"

cat > expect << EOF
<<<<<<< HEAD:a1
F
=======
G
>>>>>>> G:a1
EOF

test_expect_success "result contains a conflict" "git diff expect a1"

git ls-files --stage > out
cat > expect << EOF
100644 da056ce14a2241509897fa68bb2b3b6e6194ef9e 1	a1
100644 cf84443e49e1b366fac938711ddf4be2d4d1d9e9 2	a1
100644 fd7923529855d0b274795ae3349c5e0438333979 3	a1
EOF

test_expect_success "virtual trees were processed" "git diff expect out"

git reset --hard
test_expect_success 'refuse to merge binary files' '
	printf "\0" > binary-file &&
	git add binary-file &&
	git commit -m binary &&
	git checkout G &&
	printf "\0\0" > binary-file &&
	git add binary-file &&
	git commit -m binary2 &&
	! git merge F > merge.out 2> merge.err &&
	grep "Cannot merge binary files: HEAD:binary-file vs. F:binary-file" \
		merge.err
'

test_done
