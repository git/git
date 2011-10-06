#!/bin/sh
#

test_description='git web--browse basic tests

This test checks that git web--browse can handle various valid URLs.'

. ./test-lib.sh

test_expect_success \
	'URL with an ampersand in it' '
	echo http://example.com/foo\&bar >expect &&
	git config browser.custom.cmd echo &&
	git web--browse --browser=custom \
		http://example.com/foo\&bar >actual &&
	test_cmp expect actual
'

test_expect_success \
	'URL with a semi-colon in it' '
	echo http://example.com/foo\;bar >expect &&
	git config browser.custom.cmd echo &&
	git web--browse --browser=custom \
		http://example.com/foo\;bar >actual &&
	test_cmp expect actual
'

test_expect_success \
	'URL with a hash in it' '
	echo http://example.com/foo#bar >expect &&
	git config browser.custom.cmd echo &&
	git web--browse --browser=custom \
		http://example.com/foo#bar >actual &&
	test_cmp expect actual
'

test_expect_success \
	'browser paths are properly quoted' '
	echo fake: http://example.com/foo >expect &&
	cat >"fake browser" <<-\EOF &&
	#!/bin/sh
	echo fake: "$@"
	EOF
	chmod +x "fake browser" &&
	git config browser.w3m.path "`pwd`/fake browser" &&
	git web--browse --browser=w3m \
		http://example.com/foo >actual &&
	test_cmp expect actual
'

test_expect_success \
	'browser command allows arbitrary shell code' '
	echo "arg: http://example.com/foo" >expect &&
	git config browser.custom.cmd "
		f() {
			for i in \"\$@\"; do
				echo arg: \$i
			done
		}
		f" &&
	git web--browse --browser=custom \
		http://example.com/foo >actual &&
	test_cmp expect actual
'

test_done
