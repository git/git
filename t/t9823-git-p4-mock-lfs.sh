#!/bin/sh

test_description='Clone repositories and store files in Mock LFS'

. ./lib-git-p4.sh

test_file_is_not_in_mock_lfs () {
	FILE="$1" &&
	CONTENT="$2" &&
	echo "$CONTENT" >expect_content &&
	test_path_is_file "$FILE" &&
	test_cmp expect_content "$FILE"
}

test_file_is_in_mock_lfs () {
	FILE="$1" &&
	CONTENT="$2" &&
	LOCAL_STORAGE=".git/mock-storage/local/$CONTENT" &&
	SERVER_STORAGE=".git/mock-storage/remote/$CONTENT" &&
	echo "pointer-$CONTENT" >expect_pointer &&
	echo "$CONTENT" >expect_content &&
	test_path_is_file "$FILE" &&
	test_path_is_file "$LOCAL_STORAGE" &&
	test_path_is_file "$SERVER_STORAGE" &&
	test_cmp expect_pointer "$FILE" &&
	test_cmp expect_content "$LOCAL_STORAGE" &&
	test_cmp expect_content "$SERVER_STORAGE"
}

test_file_is_deleted_in_mock_lfs () {
	FILE="$1" &&
	CONTENT="$2" &&
	LOCAL_STORAGE=".git/mock-storage/local/$CONTENT" &&
	SERVER_STORAGE=".git/mock-storage/remote/$CONTENT" &&
	echo "pointer-$CONTENT" >expect_pointer &&
	echo "$CONTENT" >expect_content &&
	test_path_is_missing "$FILE" &&
	test_path_is_file "$LOCAL_STORAGE" &&
	test_path_is_file "$SERVER_STORAGE" &&
	test_cmp expect_content "$LOCAL_STORAGE" &&
	test_cmp expect_content "$SERVER_STORAGE"
}

test_file_count_in_dir () {
	DIR="$1" &&
	EXPECTED_COUNT="$2" &&
	find "$DIR" -type f >actual &&
	test_line_count = $EXPECTED_COUNT actual
}

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'Create repo with binary files' '
	client_view "//depot/... //client/..." &&
	(
		cd "$cli" &&

		echo "content 1 txt 23 bytes" >file1.txt &&
		p4 add file1.txt &&
		echo "content 2-3 bin 25 bytes" >file2.dat &&
		p4 add file2.dat &&
		p4 submit -d "Add text and binary file" &&

		mkdir "path with spaces" &&
		echo "content 2-3 bin 25 bytes" >"path with spaces/file3.bin" &&
		p4 add "path with spaces/file3.bin" &&
		p4 submit -d "Add another binary file with same content and spaces in path" &&

		echo "content 4 bin 26 bytes XX" >file4.bin &&
		p4 add file4.bin &&
		p4 submit -d "Add another binary file with different content"
	)
'

test_expect_success 'Store files in Mock LFS based on size (>24 bytes)' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config git-p4.useClientSpec true &&
		git config git-p4.largeFileSystem MockLFS &&
		git config git-p4.largeFileThreshold 24 &&
		git config git-p4.largeFilePush True &&
		git p4 clone --destination="$git" //depot@all &&

		test_file_is_not_in_mock_lfs file1.txt "content 1 txt 23 bytes" &&
		test_file_is_in_mock_lfs file2.dat "content 2-3 bin 25 bytes" &&
		test_file_is_in_mock_lfs "path with spaces/file3.bin" "content 2-3 bin 25 bytes" &&
		test_file_is_in_mock_lfs file4.bin "content 4 bin 26 bytes XX" &&

		test_file_count_in_dir ".git/mock-storage/local" 2 &&
		test_file_count_in_dir ".git/mock-storage/remote" 2
	)
'

test_expect_success 'Store files in Mock LFS based on extension (dat)' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config git-p4.useClientSpec true &&
		git config git-p4.largeFileSystem MockLFS &&
		git config git-p4.largeFileExtensions dat &&
		git config git-p4.largeFilePush True &&
		git p4 clone --destination="$git" //depot@all &&

		test_file_is_not_in_mock_lfs file1.txt "content 1 txt 23 bytes" &&
		test_file_is_in_mock_lfs file2.dat "content 2-3 bin 25 bytes" &&
		test_file_is_not_in_mock_lfs "path with spaces/file3.bin" "content 2-3 bin 25 bytes" &&
		test_file_is_not_in_mock_lfs file4.bin "content 4 bin 26 bytes XX" &&

		test_file_count_in_dir ".git/mock-storage/local" 1 &&
		test_file_count_in_dir ".git/mock-storage/remote" 1
	)
'

test_expect_success 'Store files in Mock LFS based on extension (dat) and use git p4 sync and no client spec' '
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init &&
		git config git-p4.useClientSpec true &&
		git config git-p4.largeFileSystem MockLFS &&
		git config git-p4.largeFileExtensions dat &&
		git config git-p4.largeFilePush True &&
		git p4 sync //depot &&
		git checkout p4/master &&

		test_file_is_not_in_mock_lfs file1.txt "content 1 txt 23 bytes" &&
		test_file_is_in_mock_lfs file2.dat "content 2-3 bin 25 bytes" &&
		test_file_is_not_in_mock_lfs "path with spaces/file3.bin" "content 2-3 bin 25 bytes" &&
		test_file_is_not_in_mock_lfs file4.bin "content 4 bin 26 bytes XX" &&

		test_file_count_in_dir ".git/mock-storage/local" 1 &&
		test_file_count_in_dir ".git/mock-storage/remote" 1
	)
'

test_expect_success 'Remove file from repo and store files in Mock LFS based on size (>24 bytes)' '
	client_view "//depot/... //client/..." &&
	(
		cd "$cli" &&
		p4 delete file4.bin &&
		p4 submit -d "Remove file"
	) &&

	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config git-p4.useClientSpec true &&
		git config git-p4.largeFileSystem MockLFS &&
		git config git-p4.largeFileThreshold 24 &&
		git config git-p4.largeFilePush True &&
		git p4 clone --destination="$git" //depot@all &&

		test_file_is_not_in_mock_lfs file1.txt "content 1 txt 23 bytes" &&
		test_file_is_in_mock_lfs file2.dat "content 2-3 bin 25 bytes" &&
		test_file_is_in_mock_lfs "path with spaces/file3.bin" "content 2-3 bin 25 bytes" &&
		test_file_is_deleted_in_mock_lfs file4.bin "content 4 bin 26 bytes XX" &&

		test_file_count_in_dir ".git/mock-storage/local" 2 &&
		test_file_count_in_dir ".git/mock-storage/remote" 2
	)
'

test_expect_success 'Run git p4 submit in repo configured with large file system' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git config git-p4.useClientSpec true &&
		git config git-p4.largeFileSystem MockLFS &&
		git config git-p4.largeFileThreshold 24 &&
		git config git-p4.largeFilePush True &&
		git p4 clone --destination="$git" //depot@all &&

		test_must_fail git p4 submit
	)
'

test_done
