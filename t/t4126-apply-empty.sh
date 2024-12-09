#!/bin/sh

test_description='apply empty'

. ./test-lib.sh

test_expect_success setup '
	>empty &&
	git add empty &&
	test_tick &&
	git commit -m initial &&
	git commit --allow-empty -m "empty commit" &&
	git format-patch --always HEAD~ >empty.patch &&
	test_write_lines a b c d e >empty &&
	cat empty >expect &&
	git diff |
	sed -e "/^diff --git/d" \
	    -e "/^index /d" \
	    -e "s|a/empty|empty.orig|" \
	    -e "s|b/empty|empty|" >patch0 &&
	sed -e "s|empty|missing|" patch0 >patch1 &&
	>empty &&
	git update-index --refresh
'

test_expect_success 'apply empty' '
	rm -f missing &&
	test_when_finished "git reset --hard" &&
	git apply patch0 &&
	test_cmp expect empty
'

test_expect_success 'apply empty patch fails' '
	test_when_finished "git reset --hard" &&
	test_must_fail git apply empty.patch &&
	test_must_fail git apply - </dev/null
'

test_expect_success 'apply with --allow-empty succeeds' '
	test_when_finished "git reset --hard" &&
	git apply --allow-empty empty.patch &&
	git apply --allow-empty - </dev/null
'

test_expect_success 'apply --index empty' '
	rm -f missing &&
	test_when_finished "git reset --hard" &&
	git apply --index patch0 &&
	test_cmp expect empty &&
	git diff --exit-code
'

test_expect_success 'apply create' '
	rm -f missing &&
	test_when_finished "git reset --hard" &&
	git apply patch1 &&
	test_cmp expect missing
'

test_expect_success 'apply --index create' '
	rm -f missing &&
	test_when_finished "git reset --hard" &&
	git apply --index patch1 &&
	test_cmp expect missing &&
	git diff --exit-code
'

test_expect_success !MINGW 'apply with no-contents and a funny pathname' '
	test_when_finished "rm -fr \"funny \"; git reset --hard" &&

	mkdir "funny " &&
	>"funny /empty" &&
	git add "funny /empty" &&
	git diff HEAD -- "funny /" >sample.patch &&
	git diff -R HEAD -- "funny /" >elpmas.patch &&

	git reset --hard &&

	git apply --stat --check --apply sample.patch &&
	test_must_be_empty "funny /empty" &&

	git apply --stat --check --apply elpmas.patch &&
	test_path_is_missing "funny /empty" &&

	git apply -R --stat --check --apply elpmas.patch &&
	test_must_be_empty "funny /empty" &&

	git apply -R --stat --check --apply sample.patch &&
	test_path_is_missing "funny /empty"
'

test_done
