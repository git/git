#!/bin/sh

test_description='test case exclude pathspec'

. ./test-lib.sh

test_expect_success 'setup a submodule' '
	test_create_repo pretzel &&
	: >pretzel/a &&
	but -C pretzel add a &&
	but -C pretzel cummit -m "add a file" -- a &&
	but submodule add ./pretzel sub &&
	but cummit -a -m "add submodule" &&
	but submodule deinit --all
'

cat <<EOF >expect
fatal: Pathspec 'sub/a' is in submodule 'sub'
EOF

test_expect_success 'error message for path inside submodule' '
	echo a >sub/a &&
	test_must_fail but add sub/a 2>actual &&
	test_cmp expect actual
'

test_expect_success 'error message for path inside submodule from within submodule' '
	test_must_fail but -C sub add . 2>actual &&
	test_i18ngrep "in unpopulated submodule" actual
'

test_done
