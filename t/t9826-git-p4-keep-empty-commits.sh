#!/bin/sh

test_description='Clone repositories and keep empty commits'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'Create a repo' '
	client_view "//depot/... //client/..." &&
	(
		cd "$cli" &&

		mkdir -p subdir &&

		>subdir/file1.txt &&
		p4 add subdir/file1.txt &&
		p4 submit -d "Add file 1" &&

		>file2.txt &&
		p4 add file2.txt &&
		p4 submit -d "Add file 2" &&

		>subdir/file3.txt &&
		p4 add subdir/file3.txt &&
		p4 submit -d "Add file 3" &&

		>file4.txt &&
		p4 add file4.txt &&
		p4 submit -d "Add file 4" &&

		p4 delete subdir/file3.txt &&
		p4 submit -d "Remove file 3" &&

		p4 delete file4.txt &&
		p4 submit -d "Remove file 4"
	)
'

test_expect_success 'Clone repo root path with all history' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git p4 clone --use-client-spec --destination="$git" //depot@all &&
		cat >expect <<-\EOF &&
		Remove file 4
		[git-p4: depot-paths = "//depot/": change = 6]

		Remove file 3
		[git-p4: depot-paths = "//depot/": change = 5]

		Add file 4
		[git-p4: depot-paths = "//depot/": change = 4]

		Add file 3
		[git-p4: depot-paths = "//depot/": change = 3]

		Add file 2
		[git-p4: depot-paths = "//depot/": change = 2]

		Add file 1
		[git-p4: depot-paths = "//depot/": change = 1]

		EOF
		git log --format=%B >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone repo subdir with all history but keep empty commits' '
	client_view "//depot/subdir/... //client/subdir/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config git-p4.keepEmptyCommits true &&
		git p4 clone --use-client-spec --destination="$git" //depot@all &&
		cat >expect <<-\EOF &&
		Remove file 4
		[git-p4: depot-paths = "//depot/": change = 6]

		Remove file 3
		[git-p4: depot-paths = "//depot/": change = 5]

		Add file 4
		[git-p4: depot-paths = "//depot/": change = 4]

		Add file 3
		[git-p4: depot-paths = "//depot/": change = 3]

		Add file 2
		[git-p4: depot-paths = "//depot/": change = 2]

		Add file 1
		[git-p4: depot-paths = "//depot/": change = 1]

		EOF
		git log --format=%B >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone repo subdir with all history' '
	client_view "//depot/subdir/... //client/subdir/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git p4 clone --use-client-spec --destination="$git" --verbose //depot@all &&
		cat >expect <<-\EOF &&
		Remove file 3
		[git-p4: depot-paths = "//depot/": change = 5]

		Add file 3
		[git-p4: depot-paths = "//depot/": change = 3]

		Add file 1
		[git-p4: depot-paths = "//depot/": change = 1]

		EOF
		git log --format=%B >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
