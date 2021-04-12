#!/bin/sh

test_description='Clone repositories with non ASCII commit messages'

. ./lib-git-p4.sh

UTF8="$(printf "a-\303\244_o-\303\266_u-\303\274")"
ISO8859="$(printf "a-\344_o-\366_u-\374")"

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'create commits in perforce' '
	(
		cd "$cli" &&

		p4_add_user "${UTF8}" &&
		p4_add_user "${ISO8859}" &&

		>dummy-file1 &&
		P4USER="${UTF8}" p4 add dummy-file1 &&
		P4USER="${UTF8}" p4 submit -d "message ${UTF8}" &&

		>dummy-file2 &&
		P4USER="${ISO8859}" p4 add dummy-file2 &&
		P4USER="${ISO8859}" p4 submit -d "message ${ISO8859}"
	)
'

test_expect_success 'check UTF-8 commit' '
	(
		git p4 clone --destination="$git/1" //depot@1,1 &&
		git -C "$git/1" cat-file commit HEAD | grep -q "^message ${UTF8}$" &&
		git -C "$git/1" cat-file commit HEAD | grep -q "^author Dr. ${UTF8} <${UTF8}@example.com>"
	)
'

test_expect_success 'check ISO-8859 commit' '
	(
		git p4 clone --destination="$git/2" //depot@2,2 &&
		git -C "$git/2" cat-file commit HEAD > /tmp/dump.txt &&
		git -C "$git/2" cat-file commit HEAD | grep -q "^message ${ISO8859}$" &&
		git -C "$git/2" cat-file commit HEAD | grep -q "^author Dr. ${ISO8859} <${ISO8859}@example.com>"
	)
'

test_done
