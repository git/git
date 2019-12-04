#!/bin/sh

test_description='Clone repositories with non ASCII paths'

. ./lib-git-p4.sh

# lowercase filename
# UTF8    - HEX:   a-\xc3\xa4_o-\xc3\xb6_u-\xc3\xbc
#         - octal: a-\303\244_o-\303\266_u-\303\274
# ISO8859 - HEX:   a-\xe4_o-\xf6_u-\xfc
UTF8_ESCAPED="a-\303\244_o-\303\266_u-\303\274.txt"
ISO8859_ESCAPED="a-\344_o-\366_u-\374.txt"

# lowercase directory
# UTF8    - HEX:   dir_a-\xc3\xa4_o-\xc3\xb6_u-\xc3\xbc
# ISO8859 - HEX:   dir_a-\xe4_o-\xf6_u-\xfc
DIR_UTF8_ESCAPED="dir_a-\303\244_o-\303\266_u-\303\274"
DIR_ISO8859_ESCAPED="dir_a-\344_o-\366_u-\374"


ISO8859="$(printf "$ISO8859_ESCAPED")" &&
echo content123 >"$ISO8859" &&
rm "$ISO8859" || {
	skip_all="fs does not accept ISO-8859-1 filenames"
	test_done
}

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

test_expect_success 'Clone repo containing iso8859-1 encoded paths with using --encoding parameter' '
	test_when_finished cleanup_git &&
	(
		git p4 clone --encoding iso8859 --destination="$git" //depot &&
		cd "$git" &&
		UTF8="$(printf "$UTF8_ESCAPED")" &&
		echo "$UTF8" >expect &&
		git -c core.quotepath=false ls-files >actual &&
		test_cmp expect actual &&

		echo content123 >expect &&
		cat "$UTF8" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Delete iso8859-1 encoded paths and clone' '
	(
		cd "$cli" &&
		ISO8859="$(printf "$ISO8859_ESCAPED")" &&
		p4 delete "$ISO8859" &&
		p4 submit -d "remove file"
	) &&
	git p4 clone --destination="$git" //depot@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git -c core.quotepath=false ls-files >actual &&
		test_must_be_empty actual
	)
'

# These tests will create a directory with ISO8859-1 characters in both the 
# directory and the path.  Since it is possible to clone a path instead of using
# the whole client-spec.  Check both versions:  client-spec and with a direct
# path using --encoding
test_expect_success 'Create a repo containing iso8859-1 encoded directory and filename' '
	(
		DIR_ISO8859="$(printf "$DIR_ISO8859_ESCAPED")" &&
		ISO8859="$(printf "$ISO8859_ESCAPED")" &&
		cd "$cli" &&
		mkdir "$DIR_ISO8859" &&
		cd "$DIR_ISO8859" &&
		echo content123 >"$ISO8859" &&
		p4 add "$ISO8859" &&
		p4 submit -d "test commit (encoded directory)"
	)
'

test_expect_success 'Clone repo containing iso8859-1 encoded depot path and files with git-p4.pathEncoding' '
	test_when_finished cleanup_git &&
	(
		DIR_ISO8859="$(printf "$DIR_ISO8859_ESCAPED")" &&
		DIR_UTF8="$(printf "$DIR_UTF8_ESCAPED")" &&
		cd "$git" &&
		git init . &&
		git config git-p4.pathEncoding iso8859-1 &&
		git p4 clone --use-client-spec --destination="$git" "//depot/$DIR_ISO8859" &&
		cd "$DIR_UTF8" &&
		UTF8="$(printf "$UTF8_ESCAPED")" &&
		echo "$UTF8" >expect &&
		git -c core.quotepath=false ls-files >actual &&
		test_cmp expect actual &&

		echo content123 >expect &&
		cat "$UTF8" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone repo containing iso8859-1 encoded depot path and files with git-p4.pathEncoding, without --use-client-spec' '
	test_when_finished cleanup_git &&
	(
		DIR_ISO8859="$(printf "$DIR_ISO8859_ESCAPED")" &&
		cd "$git" &&
		git init . &&
		git config git-p4.pathEncoding iso8859-1 &&
		git p4 clone --destination="$git" "//depot/$DIR_ISO8859" &&
		UTF8="$(printf "$UTF8_ESCAPED")" &&
		echo "$UTF8" >expect &&
		git -c core.quotepath=false ls-files >actual &&
		test_cmp expect actual &&

		echo content123 >expect &&
		cat "$UTF8" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone repo containing iso8859-1 encoded depot path and files with using --encoding parameter' '
	test_when_finished cleanup_git &&
	(
		DIR_ISO8859="$(printf "$DIR_ISO8859_ESCAPED")" &&
		git p4 clone --encoding iso8859 --destination="$git" "//depot/$DIR_ISO8859" &&
		cd "$git" &&
		UTF8="$(printf "$UTF8_ESCAPED")" &&
		echo "$UTF8" >expect &&
		git -c core.quotepath=false ls-files >actual &&
		test_cmp expect actual &&

		echo content123 >expect &&
		cat "$UTF8" >actual &&
		test_cmp expect actual
	)
'

test_done
