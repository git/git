#!/bin/sh

test_description='git-merge

Do not overwrite changes.'

. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 > c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 > c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 > c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2 &&
	git reset --hard c1 &&
	echo "c1 a" > c1.c &&
	git add c1.c &&
	git commit -m "c1 a" &&
	git tag c1a &&
	echo "VERY IMPORTANT CHANGES" > important
'

test_expect_success 'will not overwrite untracked file' '
	git reset --hard c1 &&
	cat important > c2.c &&
	! git merge c2 &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite new file' '
	git reset --hard c1 &&
	cat important > c2.c &&
	git add c2.c &&
	! git merge c2 &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite staged changes' '
	git reset --hard c1 &&
	cat important > c2.c &&
	git add c2.c &&
	rm c2.c &&
	! git merge c2 &&
	git checkout c2.c &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite removed file' '
	git reset --hard c1 &&
	git rm c1.c &&
	git commit -m "rm c1.c" &&
	cat important > c1.c &&
	! git merge c1a &&
	test_cmp important c1.c
'

test_expect_success 'will not overwrite re-added file' '
	git reset --hard c1 &&
	git rm c1.c &&
	git commit -m "rm c1.c" &&
	cat important > c1.c &&
	git add c1.c &&
	! git merge c1a &&
	test_cmp important c1.c
'

test_expect_success 'will not overwrite removed file with staged changes' '
	git reset --hard c1 &&
	git rm c1.c &&
	git commit -m "rm c1.c" &&
	cat important > c1.c &&
	git add c1.c &&
	rm c1.c &&
	! git merge c1a &&
	git checkout c1.c &&
	test_cmp important c1.c
'

test_done
