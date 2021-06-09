#!/bin/sh

test_description='git apply of i-t-a file'

. ./test-lib.sh

test_expect_success setup '
	test_write_lines 1 2 3 4 5 >blueprint &&

	cat blueprint >test-file &&
	git add -N test-file &&
	git diff >creation-patch &&
	grep "new file mode 100644" creation-patch &&

	rm -f test-file &&
	git diff >deletion-patch &&
	grep "deleted file mode 100644" deletion-patch
'

test_expect_success 'apply creation patch to ita path (--cached)' '
	git rm -f test-file &&
	cat blueprint >test-file &&
	git add -N test-file &&

	git apply --cached creation-patch &&
	git cat-file blob :test-file >actual &&
	test_cmp blueprint actual
'

test_expect_success 'apply creation patch to ita path (--index)' '
	git rm -f test-file &&
	cat blueprint >test-file &&
	git add -N test-file &&
	rm -f test-file &&

	test_must_fail git apply --index creation-patch
'

test_expect_success 'apply deletion patch to ita path (--cached)' '
	git rm -f test-file &&
	cat blueprint >test-file &&
	git add -N test-file &&

	git apply --cached deletion-patch &&
	test_must_fail git ls-files --stage --error-unmatch test-file
'

test_expect_success 'apply deletion patch to ita path (--index)' '
	cat blueprint >test-file &&
	git add -N test-file &&

	test_must_fail git apply --index deletion-patch &&
	git ls-files --stage --error-unmatch test-file
'

test_done
