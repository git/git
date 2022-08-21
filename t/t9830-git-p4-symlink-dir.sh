#!/bin/sh

test_description='git p4 symlinked directories'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'symlinked directory' '
	(
		cd "$cli" &&
		: >first_file.t &&
		p4 add first_file.t &&
		p4 submit -d "first change"
	) &&
	git p4 clone --dest "$git" //depot &&
	(
		cd "$git" &&
		mkdir -p some/sub/directory &&
		mkdir -p other/subdir2 &&
		: > other/subdir2/file.t &&
		(cd some/sub/directory && ln -s ../../../other/subdir2 .) &&
		git add some other &&
		git commit -m "symlinks" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit -v
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		test -L some/sub/directory/subdir2 &&
		test_path_is_file some/sub/directory/subdir2/file.t
	)

'

test_done
