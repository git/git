#!/bin/sh

test_description=clone

. ./test-lib.sh

test_expect_success setup '
	echo "#!/bin/sh" > not_ssh &&
	echo "echo \"\$*\" > not_ssh_output" >> not_ssh &&
	echo "exit 1" >> not_ssh &&
	chmod +x not_ssh
'

test_expect_success 'clone calls git upload-pack unqualified with no -u option' '
	test_must_fail env GIT_SSH=./not_ssh git clone localhost:/path/to/repo junk &&
	echo "localhost git-upload-pack '\''/path/to/repo'\''" >expected &&
	test_cmp expected not_ssh_output
'

test_expect_success 'clone calls specified git upload-pack with -u option' '
	test_must_fail env GIT_SSH=./not_ssh \
		git clone -u ./something/bin/git-upload-pack localhost:/path/to/repo junk &&
	echo "localhost ./something/bin/git-upload-pack '\''/path/to/repo'\''" >expected &&
	test_cmp expected not_ssh_output
'

test_done
