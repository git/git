#!/bin/sh

test_description='Clone repositories with non ASCII paths'

. ./lib-git-p4.sh

UTF8_ESCAPED="a-\303\244_o-\303\266_u-\303\274.txt"
ISO8859_ESCAPED="a-\344_o-\366_u-\374.txt"

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'Create a repo containing iso8859-1 encoded paths' '
	(
		cd "$cli" &&
		ISO8859="$(printf "$ISO8859_ESCAPED")" &&
		echo content123 >"$ISO8859" &&
		p4 add "$ISO8859" &&
		p4 submit -d "test commit"
	)
'

test_expect_failure 'Clone auto-detects depot with iso8859-1 paths' '
	git p4 clone --destination="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		UTF8="$(printf "$UTF8_ESCAPED")" &&
		echo "$UTF8" >expect &&
		git -c core.quotepath=false ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone repo containing iso8859-1 encoded paths with git-p4.pathEncoding' '
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config git-p4.pathEncoding iso8859-1 &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		UTF8="$(printf "$UTF8_ESCAPED")" &&
		echo "$UTF8" >expect &&
		git -c core.quotepath=false ls-files >actual &&
		test_cmp expect actual &&

		echo content123 >expect &&
		cat "$UTF8" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
