#!/bin/sh

test_description='Clone repositories with path case variations'

. ./lib-git-p4.sh

test_expect_success 'start p4d with case folding enabled' '
	start_p4d -C1
'

test_expect_success 'Create a repo with path case variations' '
	client_view "//depot/... //client/..." &&
	(
		cd "$cli" &&

		mkdir -p Path/to &&
		>Path/to/File2.txt &&
		p4 add Path/to/File2.txt &&
		p4 submit -d "Add file2" &&
		rm -rf Path &&

		mkdir -p path/TO &&
		>path/TO/file1.txt &&
		p4 add path/TO/file1.txt &&
		p4 submit -d "Add file1" &&
		rm -rf path &&

		mkdir -p path/to &&
		>path/to/file3.txt &&
		p4 add path/to/file3.txt &&
		p4 submit -d "Add file3" &&
		rm -rf path &&

		mkdir -p x-outside-spec &&
		>x-outside-spec/file4.txt &&
		p4 add x-outside-spec/file4.txt &&
		p4 submit -d "Add file4" &&
		rm -rf x-outside-spec
	)
'

test_expect_success 'Clone root' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase false &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		# This method is used instead of "test -f" to ensure the case is
		# checked even if the test is executed on case-insensitive file systems.
		# All files are there as expected although the path cases look random.
		cat >expect <<-\EOF &&
		Path/to/File2.txt
		path/TO/file1.txt
		path/to/file3.txt
		x-outside-spec/file4.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone root (ignorecase)' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase true &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		# This method is used instead of "test -f" to ensure the case is
		# checked even if the test is executed on case-insensitive file systems.
		# All files are there as expected although the path cases look random.
		cat >expect <<-\EOF &&
		path/TO/File2.txt
		path/TO/file1.txt
		path/TO/file3.txt
		x-outside-spec/file4.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone root and ignore one file' '
	client_view \
		"//depot/... //client/..." \
		"-//depot/path/TO/file1.txt //client/path/TO/file1.txt" &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase false &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		# We ignore one file in the client spec and all path cases change from
		# "TO" to "to"!
		cat >expect <<-\EOF &&
		Path/to/File2.txt
		path/to/file3.txt
		x-outside-spec/file4.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone root and ignore one file (ignorecase)' '
	client_view \
		"//depot/... //client/..." \
		"-//depot/path/TO/file1.txt //client/path/TO/file1.txt" &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase true &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		# We ignore one file in the client spec and all path cases change from
		# "TO" to "to"!
		cat >expect <<-\EOF &&
		Path/to/File2.txt
		Path/to/file3.txt
		x-outside-spec/file4.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone path' '
	client_view "//depot/Path/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase false &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		cat >expect <<-\EOF &&
		to/File2.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'Clone path (ignorecase)' '
	client_view "//depot/Path/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase true &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		cat >expect <<-\EOF &&
		TO/File2.txt
		TO/file1.txt
		TO/file3.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

# It looks like P4 determines the path case based on the first file in
# lexicographical order. Please note the lower case "to" directory for all
# files triggered through the addition of "File0.txt".
test_expect_success 'Add a new file and clone path with new file (ignorecase)' '
	client_view "//depot/... //client/..." &&
	(
		cd "$cli" &&
		mkdir -p Path/to &&
		>Path/to/File0.txt &&
		p4 add Path/to/File0.txt &&
		p4 submit -d "Add file" &&
		rm -rf Path
	) &&

	client_view "//depot/Path/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config core.ignorecase true &&
		git p4 clone --use-client-spec --destination="$git" //depot &&
		cat >expect <<-\EOF &&
		to/File0.txt
		to/File2.txt
		to/file1.txt
		to/file3.txt
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
