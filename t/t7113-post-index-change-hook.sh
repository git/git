#!/bin/sh

test_description='post index change hook'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir -p dir1 &&
	touch dir1/file1.txt &&
	echo testing >dir1/file2.txt &&
	git add . &&
	git commit -m "initial"
'

test_expect_success 'test status, add, commit, others trigger hook without flags set' '
	mkdir -p .git/hooks &&
	write_script .git/hooks/post-index-change <<-\EOF &&
		if test "$1" -eq 1; then
			echo "Invalid combination of flags passed to hook; updated_workdir is set." >testfailure
			exit 1
		fi
		if test "$2" -eq 1; then
			echo "Invalid combination of flags passed to hook; updated_skipworktree is set." >testfailure
			exit 1
		fi
		if test -f ".git/index.lock"; then
			echo ".git/index.lock exists" >testfailure
			exit 3
		fi
		if ! test -f ".git/index"; then
			echo ".git/index does not exist" >testfailure
			exit 3
		fi
		echo "success" >testsuccess
	EOF
	mkdir -p dir2 &&
	touch dir2/file1.txt &&
	touch dir2/file2.txt &&
	: force index to be dirty &&
	test-tool chmtime +60 dir1/file1.txt &&
	git status &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git add . &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git commit -m "second" &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git checkout -- dir1/file1.txt &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git update-index &&
	test_path_is_missing testsuccess &&
	test_path_is_missing testfailure &&
	git reset --soft &&
	test_path_is_missing testsuccess &&
	test_path_is_missing testfailure
'

test_expect_success 'test checkout and reset trigger the hook' '
	write_script .git/hooks/post-index-change <<-\EOF &&
		if test "$1" -eq 1 && test "$2" -eq 1; then
			echo "Invalid combination of flags passed to hook; updated_workdir and updated_skipworktree are both set." >testfailure
			exit 1
		fi
		if test "$1" -eq 0 && test "$2" -eq 0; then
			echo "Invalid combination of flags passed to hook; neither updated_workdir or updated_skipworktree are set." >testfailure
			exit 2
		fi
		if test "$1" -eq 1; then
			if test -f ".git/index.lock"; then
				echo "updated_workdir set but .git/index.lock exists" >testfailure
				exit 3
			fi
			if ! test -f ".git/index"; then
				echo "updated_workdir set but .git/index does not exist" >testfailure
				exit 3
			fi
		else
			echo "update_workdir should be set for checkout" >testfailure
			exit 4
		fi
		echo "success" >testsuccess
	EOF
	: force index to be dirty &&
	test-tool chmtime +60 dir1/file1.txt &&
	git checkout main &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	test-tool chmtime +60 dir1/file1.txt &&
	git checkout HEAD &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	test-tool chmtime +60 dir1/file1.txt &&
	git reset --hard &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git checkout -B test &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure
'

test_expect_success 'test reset --mixed and update-index triggers the hook' '
	write_script .git/hooks/post-index-change <<-\EOF &&
		if test "$1" -eq 1 && test "$2" -eq 1; then
			echo "Invalid combination of flags passed to hook; updated_workdir and updated_skipworktree are both set." >testfailure
			exit 1
		fi
		if test "$1" -eq 0 && test "$2" -eq 0; then
			echo "Invalid combination of flags passed to hook; neither updated_workdir or updated_skipworktree are set." >testfailure
			exit 2
		fi
		if test "$2" -eq 1; then
			if test -f ".git/index.lock"; then
				echo "updated_skipworktree set but .git/index.lock exists" >testfailure
				exit 3
			fi
			if ! test -f ".git/index"; then
				echo "updated_skipworktree set but .git/index does not exist" >testfailure
				exit 3
			fi
		else
			echo "updated_skipworktree should be set for reset --mixed and update-index" >testfailure
			exit 4
		fi
		echo "success" >testsuccess
	EOF
	: force index to be dirty &&
	test-tool chmtime +60 dir1/file1.txt &&
	git reset --mixed --quiet HEAD~1 &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git hash-object -w --stdin <dir1/file2.txt >expect &&
	git update-index --cacheinfo 100644 "$(cat expect)" dir1/file1.txt &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure &&
	git update-index --skip-worktree dir1/file2.txt &&
	git update-index --remove dir1/file2.txt &&
	test_path_is_file testsuccess && rm -f testsuccess &&
	test_path_is_missing testfailure
'

test_done
