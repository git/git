#!/bin/sh

test_description='am --abort'

. ./test-lib.sh

test_expect_success setup '
	test_write_lines a b c d e f g >file-1 &&
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
	git branch changes &&
	git format-patch --no-numbered initial &&
	git checkout -b conflicting initial &&
	echo different >>file-1 &&
	echo whatever >new-file &&
	git add file-1 new-file &&
	git commit -m different &&
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
		test_write_lines 3 2 initial >expect &&
		test_cmp expect actual
	'

	test_expect_success "am$with3 --skip continue after failed am$with3" '
		test_must_fail git am$with3 --skip >output &&
		test_grep "^Applying: 6$" output &&
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

test_expect_success 'git am --abort return failed exit status when it fails' '
	test_when_finished "rm -rf file-2/ && git reset --hard && git am --abort" &&
	git checkout changes &&
	git format-patch -1 --stdout conflicting >changes.mbox &&
	test_must_fail git am --3way changes.mbox &&

	git rm file-2 &&
	mkdir file-2 &&
	echo precious >file-2/somefile &&
	test_must_fail git am --abort &&
	test_path_is_dir file-2/
'

test_expect_success 'git am --abort cleans relevant files' '
	git checkout changes &&
	git format-patch -1 --stdout conflicting >changes.mbox &&
	test_must_fail git am --3way changes.mbox &&

	test_path_is_file new-file &&
	echo further changes >>file-1 &&
	echo change other file >>file-2 &&

	# Abort, and expect the files touched by am to be reverted
	git am --abort &&

	test_path_is_missing new-file &&

	# Files not involved in am operation are left modified
	git diff --name-only changes >actual &&
	test_write_lines file-2 >expect &&
	test_cmp expect actual
'

test_done
