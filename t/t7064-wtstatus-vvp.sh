#!/bin/sh

test_description='git status --porcelain --verbose --verbose

This test exercises very verbose output for git status.'


. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	git config --local core.autocrlf false &&
	echo x >file_x &&
	echo y >file_y &&
	echo z >file_z &&
	mkdir dir1 &&
	echo a >dir1/file_a &&
	echo b >dir1/file_b
'


##################################################################
## Confirm VVP output prior to initial commit.
##################################################################

test_expect_success pre_initial_commit_0 '
	cat >expected <<EOF &&
### state: clean
### track: (initial) master
??? actual
??? dir1/
??? expected
??? file_x
??? file_y
??? file_z
EOF
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=normal >actual &&
	test_cmp expected actual
'


test_expect_success pre_initial_commit_1 '
	git add file_x file_y file_z dir1 &&
	cat >expected <<EOF &&
### state: clean
### track: (initial) master
A   000000 100644 100644 0000000000000000000000000000000000000000 78981922613b2afb6025042ff6bd878ac1994e85 dir1/file_a
A   000000 100644 100644 0000000000000000000000000000000000000000 61780798228d17af2d34fce4cfbdf35556832472 dir1/file_b
A   000000 100644 100644 0000000000000000000000000000000000000000 587be6b4c3f93f93c489c0111bba5596147a26cb file_x
A   000000 100644 100644 0000000000000000000000000000000000000000 975fbec8256d3e8a3797e7a3611380f27c49f4ac file_y
A   000000 100644 100644 0000000000000000000000000000000000000000 b68025345d5301abad4d9ec9166f455243a0d746 file_z
??? actual
??? expected
EOF
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=all >actual &&
	test_cmp expected actual
'


##################################################################
## Create first commit. Confirm commit sha in new track header.
## Then make some changes on top of it.
##################################################################

test_expect_success initial_commit_0 '
	git commit -m initial &&
	H0=`git rev-parse HEAD` &&
	cat >expected <<EOF &&
### state: clean
### track: $H0 master
??? actual
??? expected
EOF
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=all >actual &&
	test_cmp expected actual
'


test_expect_success initial_commit_1 '
	echo x >>file_x &&
	rm file_z &&
	H0=`git rev-parse HEAD` &&
	cat >expected <<EOF &&
### state: clean
### track: $H0 master
 M  100644 100644 100644 587be6b4c3f93f93c489c0111bba5596147a26cb 587be6b4c3f93f93c489c0111bba5596147a26cb file_x
 D  100644 100644 000000 b68025345d5301abad4d9ec9166f455243a0d746 b68025345d5301abad4d9ec9166f455243a0d746 file_z
??? actual
??? expected
EOF
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=all >actual &&
	test_cmp expected actual
'


test_expect_success initial_commit_2 '
	git add file_x &&
	git rm file_z &&
	H0=`git rev-parse HEAD` &&
	cat >expected <<EOF &&
### state: clean
### track: $H0 master
M   100644 100644 100644 587be6b4c3f93f93c489c0111bba5596147a26cb 560e017033beac775889ae5ff81a070fe16c6e8e file_x
D   100644 000000 000000 b68025345d5301abad4d9ec9166f455243a0d746 0000000000000000000000000000000000000000 file_z
??? actual
??? expected
EOF
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=all >actual &&
	test_cmp expected actual
'


test_expect_success initial_commit_3 '
	git mv file_y renamed_y &&
	H0=`git rev-parse HEAD` &&
	printf "### state: clean\n" >expected &&
	printf "### track: $H0 master\n" >>expected &&
	printf "M   100644 100644 100644 587be6b4c3f93f93c489c0111bba5596147a26cb 560e017033beac775889ae5ff81a070fe16c6e8e file_x\n" >>expected &&
	printf "D   100644 000000 000000 b68025345d5301abad4d9ec9166f455243a0d746 0000000000000000000000000000000000000000 file_z\n" >>expected &&
	printf "R   100644 100644 100644 975fbec8256d3e8a3797e7a3611380f27c49f4ac 975fbec8256d3e8a3797e7a3611380f27c49f4ac 100 file_y\trenamed_y\n" >>expected &&
	printf "??? actual\n" >>expected &&
	printf "??? expected\n" >>expected &&
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=all >actual &&
	test_cmp expected actual
'


##################################################################
## Create second commit.
##################################################################

test_expect_success second_commit_0 '
	git commit -m second &&
	H1=`git rev-parse HEAD` &&
	cat >expected <<EOF &&
### state: clean
### track: $H1 master
??? actual
??? expected
EOF
	git status --porcelain --branch --verbose --verbose --ignored --untracked-files=all >actual &&
	test_cmp expected actual
'


##################################################################
## The end.
##################################################################

test_done
