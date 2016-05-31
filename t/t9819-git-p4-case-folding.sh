#!/bin/sh

test_description='interaction with P4 case-folding'

. ./lib-git-p4.sh

if test_have_prereq CASE_INSENSITIVE_FS
then
	skip_all='skipping P4 case-folding tests; case insensitive file system detected'
	test_done
fi

test_expect_success 'start p4d with case folding enabled' '
	start_p4d -C1
'

test_expect_success 'Create a repo, name is lowercase' '
	(
		client_view "//depot/... //client/..." &&
		cd "$cli" &&
		mkdir -p lc UC &&
		>lc/file.txt && >UC/file.txt &&
		p4 add lc/file.txt UC/file.txt &&
		p4 submit -d "Add initial lc and UC repos"
	)
'

test_expect_success 'Check p4 is in case-folding mode' '
	(
		cd "$cli" &&
		>lc/FILE.TXT &&
		p4 add lc/FILE.TXT &&
		test_must_fail p4 submit -d "Cannot add file differing only in case" lc/FILE.TXT
	)
'

# Check we created the repo properly
test_expect_success 'Clone lc repo using lc name' '
	git p4 clone //depot/lc/... &&
	test_path_is_file lc/file.txt &&
	git p4 clone //depot/UC/... &&
	test_path_is_file UC/file.txt
'

# The clone should fail, since there is no repo called LC, but because
# we have case-insensitive p4d enabled, it appears to go ahead and work,
# but leaves an empty git repo in place.
test_expect_failure 'Clone lc repo using uc name' '
	test_must_fail git p4 clone //depot/LC/...
'

test_expect_failure 'Clone UC repo with lc name' '
	test_must_fail git p4 clone //depot/uc/...
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
