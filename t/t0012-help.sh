#!/bin/sh

test_description='help'

. ./test-lib.sh

configure_help () {
	test_config help.format html &&

	# Unless the path has "://" in it, Git tries to make sure
	# the documentation directory locally exists. Avoid it as
	# we are only interested in seeing an attempt to correctly
	# invoke a help browser in this test.
	test_config help.htmlpath test://html &&

	# Name a custom browser
	test_config browser.test.cmd ./test-browser &&
	test_config help.browser test
}

test_expect_success "setup" '
	# Just write out which page gets requested
	write_script test-browser <<-\EOF
	echo "$*" >test-browser.log
	EOF
'

test_expect_success "works for commands and guides by default" '
	configure_help &&
	git help status &&
	echo "test://html/git-status.html" >expect &&
	test_cmp expect test-browser.log &&
	git help revisions &&
	echo "test://html/gitrevisions.html" >expect &&
	test_cmp expect test-browser.log
'

test_expect_success "--exclude-guides does not work for guides" '
	>test-browser.log &&
	test_must_fail git help --exclude-guides revisions &&
	test_must_be_empty test-browser.log
'

test_expect_success "--help does not work for guides" "
	cat <<-EOF >expect &&
		git: 'revisions' is not a git command. See 'git --help'.
	EOF
	test_must_fail git revisions --help 2>actual &&
	test_i18ncmp expect actual
"

test_done
