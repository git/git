#!/bin/sh

test_description='am --abort'

. ./test-lib.sh

test_expect_success setup '
	for i in a b c d e f g
	do
		echo $i
	done >file-1 &&
	cp file-1 file-2 &&
	test_tick &&
	git add file-1 file-2 &&
	git commit -m initial &&
	git tag initial &&
	git format-patch --stdout --root initial >initial.patch &&
	for i in 2 3 4 5 6
	do
		echo $i >>file-1 &&
		echo $i >otherfile-$i &&
		git add otherfile-$i &&
		test_tick &&
		git commit -a -m $i || return 1
	done &&
	git format-patch --no-numbered initial &&
	git checkout -b side initial &&
	echo local change >file-2-expect
'

for with3 in '' ' -3'
do
	test_expect_success "am$with3 stops at a patch that does not apply" '

		git reset --hard initial &&
		cp file-2-expect file-2 &&

		test_must_fail git am$with3 000[1245]-*.patch &&
		git log --pretty=tformat:%s >actual &&
		for i in 3 2 initial
		do
			echo $i
		done >expect &&
		test_cmp expect actual
	'

	test_expect_success "am$with3 --skip continue after failed am$with3" '
		test_must_fail git am$with3 --skip >output &&
		test_i18ngrep "^Applying: 6$" output &&
		test_cmp file-2-expect file-2 &&
		test ! -f .git/MERGE_RR
	'

	test_expect_success "am --abort goes back after failed am$with3" '
		git am --abort &&
		git rev-parse HEAD >actual &&
		git rev-parse initial >expect &&
		test_cmp expect actual &&
		test_cmp file-2-expect file-2 &&
		git diff-index --exit-code --cached HEAD &&
		test ! -f .git/MERGE_RR
	'

done

test_expect_success 'am -3 --skip removes otherfile-4' '
	git reset --hard initial &&
	test_must_fail git am -3 0003-*.patch &&
	test 3 -eq $(git ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	git am --skip &&
	test_cmp_rev initial HEAD &&
	test -z "$(git ls-files -u)" &&
	test_path_is_missing otherfile-4
'

test_expect_success 'am -3 --abort removes otherfile-4' '
	git reset --hard initial &&
	test_must_fail git am -3 0003-*.patch &&
	test 3 -eq $(git ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	git am --abort &&
	test_cmp_rev initial HEAD &&
	test -z "$(git ls-files -u)" &&
	test_path_is_missing otherfile-4
'

test_expect_success 'am --abort will keep the local commits intact' '
	test_must_fail git am 0004-*.patch &&
	test_commit unrelated &&
	git rev-parse HEAD >expect &&
	git am --abort &&
	git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'am --abort will keep dirty index intact' '
	git reset --hard initial &&
	echo dirtyfile >dirtyfile &&
	cp dirtyfile dirtyfile.expected &&
	git add dirtyfile &&
	test_must_fail git am 0001-*.patch &&
	test_cmp_rev initial HEAD &&
	test_path_is_file dirtyfile &&
	test_cmp dirtyfile.expected dirtyfile &&
	git am --abort &&
	test_cmp_rev initial HEAD &&
	test_path_is_file dirtyfile &&
	test_cmp dirtyfile.expected dirtyfile
'

test_expect_success 'am -3 stops on conflict on unborn branch' '
	git checkout -f --orphan orphan &&
	git reset &&
	rm -f otherfile-4 &&
	test_must_fail git am -3 0003-*.patch &&
	test 2 -eq $(git ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)"
'

test_expect_success 'am -3 --skip clears index on unborn branch' '
	test_path_is_dir .git/rebase-apply &&
	echo tmpfile >tmpfile &&
	git add tmpfile &&
	git am --skip &&
	test -z "$(git ls-files)" &&
	test_path_is_missing otherfile-4 &&
	test_path_is_missing tmpfile
'

test_expect_success 'am -3 --abort removes otherfile-4 on unborn branch' '
	git checkout -f --orphan orphan &&
	git reset &&
	rm -f otherfile-4 file-1 &&
	test_must_fail git am -3 0003-*.patch &&
	test 2 -eq $(git ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	git am --abort &&
	test -z "$(git ls-files -u)" &&
	test_path_is_missing otherfile-4
'

test_expect_success 'am -3 --abort on unborn branch removes applied commits' '
	git checkout -f --orphan orphan &&
	git reset &&
	rm -f otherfile-4 otherfile-2 file-1 file-2 &&
	test_must_fail git am -3 initial.patch 0003-*.patch &&
	test 3 -eq $(git ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	git am --abort &&
	test -z "$(git ls-files -u)" &&
	test_path_is_missing otherfile-4 &&
	test_path_is_missing file-1 &&
	test_path_is_missing file-2 &&
	test 0 -eq $(git log --oneline 2>/dev/null | wc -l) &&
	test refs/heads/orphan = "$(git symbolic-ref HEAD)"
'

test_expect_success 'am --abort on unborn branch will keep local commits intact' '
	git checkout -f --orphan orphan &&
	git reset &&
	test_must_fail git am 0004-*.patch &&
	test_commit unrelated2 &&
	git rev-parse HEAD >expect &&
	git am --abort &&
	git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'am --skip leaves index stat info alone' '
	git checkout -f --orphan skip-stat-info &&
	git reset &&
	test_commit skip-should-be-untouched &&
	test-tool chmtime =0 skip-should-be-untouched.t &&
	git update-index --refresh &&
	git diff-files --exit-code --quiet &&
	test_must_fail git am 0001-*.patch &&
	git am --skip &&
	git diff-files --exit-code --quiet
'

test_expect_success 'am --abort leaves index stat info alone' '
	git checkout -f --orphan abort-stat-info &&
	git reset &&
	test_commit abort-should-be-untouched &&
	test-tool chmtime =0 abort-should-be-untouched.t &&
	git update-index --refresh &&
	git diff-files --exit-code --quiet &&
	test_must_fail git am 0001-*.patch &&
	git am --abort &&
	git diff-files --exit-code --quiet
'

test_done
