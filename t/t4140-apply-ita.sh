#!/bin/sh

test_description='but apply of i-t-a file'

. ./test-lib.sh

test_expect_success setup '
	test_write_lines 1 2 3 4 5 >blueprint &&

	cat blueprint >test-file &&
	but add -N test-file &&
	but diff >creation-patch &&
	grep "new file mode 100644" creation-patch &&

	rm -f test-file &&
	but diff >deletion-patch &&
	grep "deleted file mode 100644" deletion-patch
'

test_expect_success 'apply creation patch to ita path (--cached)' '
	but rm -f test-file &&
	cat blueprint >test-file &&
	but add -N test-file &&

	but apply --cached creation-patch &&
	but cat-file blob :test-file >actual &&
	test_cmp blueprint actual
'

test_expect_success 'apply creation patch to ita path (--index)' '
	but rm -f test-file &&
	cat blueprint >test-file &&
	but add -N test-file &&
	rm -f test-file &&

	test_must_fail but apply --index creation-patch
'

test_expect_success 'apply deletion patch to ita path (--cached)' '
	but rm -f test-file &&
	cat blueprint >test-file &&
	but add -N test-file &&

	but apply --cached deletion-patch &&
	test_must_fail but ls-files --stage --error-unmatch test-file
'

test_expect_success 'apply deletion patch to ita path (--index)' '
	cat blueprint >test-file &&
	but add -N test-file &&

	test_must_fail but apply --index deletion-patch &&
	but ls-files --stage --error-unmatch test-file
'

test_done
