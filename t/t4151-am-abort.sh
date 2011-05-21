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
	for i in 2 3 4 5 6
	do
		echo $i >>file-1 &&
		echo $i >otherfile-$i &&
		git add otherfile-$i &&
		test_tick &&
		git commit -a -m $i || break
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
		test_i18ngrep "^Applying" output >output.applying &&
		test_i18ngrep "^Applying: 6$" output.applying &&
		test_i18ncmp file-2-expect file-2 &&
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

test_expect_success 'am --abort will keep the local commits intact' '
	test_must_fail git am 0004-*.patch &&
	test_commit unrelated &&
	git rev-parse HEAD >expect &&
	git am --abort &&
	git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_done
