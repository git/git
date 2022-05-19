#!/bin/sh

test_description='am --abort'

. ./test-lib.sh

test_expect_success setup '
	test_write_lines a b c d e f g >file-1 &&
	cp file-1 file-2 &&
	test_tick &&
	but add file-1 file-2 &&
	but cummit -m initial &&
	but tag initial &&
	but format-patch --stdout --root initial >initial.patch &&
	for i in 2 3 4 5 6
	do
		echo $i >>file-1 &&
		echo $i >otherfile-$i &&
		but add otherfile-$i &&
		test_tick &&
		but cummit -a -m $i || return 1
	done &&
	but branch changes &&
	but format-patch --no-numbered initial &&
	but checkout -b conflicting initial &&
	echo different >>file-1 &&
	echo whatever >new-file &&
	but add file-1 new-file &&
	but cummit -m different &&
	but checkout -b side initial &&
	echo local change >file-2-expect
'

for with3 in '' ' -3'
do
	test_expect_success "am$with3 stops at a patch that does not apply" '

		but reset --hard initial &&
		cp file-2-expect file-2 &&

		test_must_fail but am$with3 000[1245]-*.patch &&
		but log --pretty=tformat:%s >actual &&
		test_write_lines 3 2 initial >expect &&
		test_cmp expect actual
	'

	test_expect_success "am$with3 --skip continue after failed am$with3" '
		test_must_fail but am$with3 --skip >output &&
		test_i18ngrep "^Applying: 6$" output &&
		test_cmp file-2-expect file-2 &&
		test ! -f .but/MERGE_RR
	'

	test_expect_success "am --abort goes back after failed am$with3" '
		but am --abort &&
		but rev-parse HEAD >actual &&
		but rev-parse initial >expect &&
		test_cmp expect actual &&
		test_cmp file-2-expect file-2 &&
		but diff-index --exit-code --cached HEAD &&
		test ! -f .but/MERGE_RR
	'

done

test_expect_success 'am -3 --skip removes otherfile-4' '
	but reset --hard initial &&
	test_must_fail but am -3 0003-*.patch &&
	test 3 -eq $(but ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	but am --skip &&
	test_cmp_rev initial HEAD &&
	test -z "$(but ls-files -u)" &&
	test_path_is_missing otherfile-4
'

test_expect_success 'am -3 --abort removes otherfile-4' '
	but reset --hard initial &&
	test_must_fail but am -3 0003-*.patch &&
	test 3 -eq $(but ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	but am --abort &&
	test_cmp_rev initial HEAD &&
	test -z "$(but ls-files -u)" &&
	test_path_is_missing otherfile-4
'

test_expect_success 'am --abort will keep the local cummits intact' '
	test_must_fail but am 0004-*.patch &&
	test_cummit unrelated &&
	but rev-parse HEAD >expect &&
	but am --abort &&
	but rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'am --abort will keep dirty index intact' '
	but reset --hard initial &&
	echo dirtyfile >dirtyfile &&
	cp dirtyfile dirtyfile.expected &&
	but add dirtyfile &&
	test_must_fail but am 0001-*.patch &&
	test_cmp_rev initial HEAD &&
	test_path_is_file dirtyfile &&
	test_cmp dirtyfile.expected dirtyfile &&
	but am --abort &&
	test_cmp_rev initial HEAD &&
	test_path_is_file dirtyfile &&
	test_cmp dirtyfile.expected dirtyfile
'

test_expect_success 'am -3 stops on conflict on unborn branch' '
	but checkout -f --orphan orphan &&
	but reset &&
	rm -f otherfile-4 &&
	test_must_fail but am -3 0003-*.patch &&
	test 2 -eq $(but ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)"
'

test_expect_success 'am -3 --skip clears index on unborn branch' '
	test_path_is_dir .but/rebase-apply &&
	echo tmpfile >tmpfile &&
	but add tmpfile &&
	but am --skip &&
	test -z "$(but ls-files)" &&
	test_path_is_missing otherfile-4 &&
	test_path_is_missing tmpfile
'

test_expect_success 'am -3 --abort removes otherfile-4 on unborn branch' '
	but checkout -f --orphan orphan &&
	but reset &&
	rm -f otherfile-4 file-1 &&
	test_must_fail but am -3 0003-*.patch &&
	test 2 -eq $(but ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	but am --abort &&
	test -z "$(but ls-files -u)" &&
	test_path_is_missing otherfile-4
'

test_expect_success 'am -3 --abort on unborn branch removes applied cummits' '
	but checkout -f --orphan orphan &&
	but reset &&
	rm -f otherfile-4 otherfile-2 file-1 file-2 &&
	test_must_fail but am -3 initial.patch 0003-*.patch &&
	test 3 -eq $(but ls-files -u | wc -l) &&
	test 4 = "$(cat otherfile-4)" &&
	but am --abort &&
	test -z "$(but ls-files -u)" &&
	test_path_is_missing otherfile-4 &&
	test_path_is_missing file-1 &&
	test_path_is_missing file-2 &&
	test 0 -eq $(but log --oneline 2>/dev/null | wc -l) &&
	test refs/heads/orphan = "$(but symbolic-ref HEAD)"
'

test_expect_success 'am --abort on unborn branch will keep local cummits intact' '
	but checkout -f --orphan orphan &&
	but reset &&
	test_must_fail but am 0004-*.patch &&
	test_cummit unrelated2 &&
	but rev-parse HEAD >expect &&
	but am --abort &&
	but rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'am --skip leaves index stat info alone' '
	but checkout -f --orphan skip-stat-info &&
	but reset &&
	test_cummit skip-should-be-untouched &&
	test-tool chmtime =0 skip-should-be-untouched.t &&
	but update-index --refresh &&
	but diff-files --exit-code --quiet &&
	test_must_fail but am 0001-*.patch &&
	but am --skip &&
	but diff-files --exit-code --quiet
'

test_expect_success 'am --abort leaves index stat info alone' '
	but checkout -f --orphan abort-stat-info &&
	but reset &&
	test_cummit abort-should-be-untouched &&
	test-tool chmtime =0 abort-should-be-untouched.t &&
	but update-index --refresh &&
	but diff-files --exit-code --quiet &&
	test_must_fail but am 0001-*.patch &&
	but am --abort &&
	but diff-files --exit-code --quiet
'

test_expect_success 'but am --abort return failed exit status when it fails' '
	test_when_finished "rm -rf file-2/ && but reset --hard && but am --abort" &&
	but checkout changes &&
	but format-patch -1 --stdout conflicting >changes.mbox &&
	test_must_fail but am --3way changes.mbox &&

	but rm file-2 &&
	mkdir file-2 &&
	echo precious >file-2/somefile &&
	test_must_fail but am --abort &&
	test_path_is_dir file-2/
'

test_expect_success 'but am --abort cleans relevant files' '
	but checkout changes &&
	but format-patch -1 --stdout conflicting >changes.mbox &&
	test_must_fail but am --3way changes.mbox &&

	test_path_is_file new-file &&
	echo further changes >>file-1 &&
	echo change other file >>file-2 &&

	# Abort, and expect the files touched by am to be reverted
	but am --abort &&

	test_path_is_missing new-file &&

	# Files not involved in am operation are left modified
	but diff --name-only changes >actual &&
	test_write_lines file-2 >expect &&
	test_cmp expect actual
'

test_done
