#!/bin/sh

test_description='git status for submodule'

. ./test-lib.sh

test_expect_success 'setup' '
	test_create_repo sub &&
	(
		cd sub &&
		: >bar &&
		git add bar &&
		git commit -m " Add bar" &&
		: >foo &&
		git add foo &&
		git commit -m " Add foo"
	) &&
	echo output > .gitignore &&
	git add sub .gitignore &&
	git commit -m "Add submodule sub"
'

test_expect_success 'status clean' '
	git status >output &&
	grep "nothing to commit" output
'

test_expect_success 'commit --dry-run -a clean' '
	test_must_fail git commit --dry-run -a >output &&
	grep "nothing to commit" output
'

test_expect_success 'status with modified file in submodule' '
	(cd sub && git reset --hard) &&
	echo "changed" >sub/foo &&
	git status >output &&
	grep "modified:   sub (modified content)" output
'

test_expect_success 'status with modified file in submodule (porcelain)' '
	(cd sub && git reset --hard) &&
	echo "changed" >sub/foo &&
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added file in submodule' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status >output &&
	grep "modified:   sub (modified content)" output
'

test_expect_success 'status with added file in submodule (porcelain)' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with untracked file in submodule' '
	(cd sub && git reset --hard) &&
	echo "content" >sub/new-file &&
	git status >output &&
	grep "modified:   sub (untracked content)" output
'

test_expect_success 'status -uno with untracked file in submodule' '
	git status -uno >output &&
	grep "^nothing to commit" output
'

test_expect_success 'status with untracked file in submodule (porcelain)' '
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added and untracked file in submodule' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	echo "content" >sub/new-file &&
	git status >output &&
	grep "modified:   sub (modified content, untracked content)" output
'

test_expect_success 'status with added and untracked file in submodule (porcelain)' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	echo "content" >sub/new-file &&
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with modified file in modified submodule' '
	(cd sub && git reset --hard) &&
	rm sub/new-file &&
	(cd sub && echo "next change" >foo && git commit -m "next change" foo) &&
	echo "changed" >sub/foo &&
	git status >output &&
	grep "modified:   sub (new commits, modified content)" output
'

test_expect_success 'status with modified file in modified submodule (porcelain)' '
	(cd sub && git reset --hard) &&
	echo "changed" >sub/foo &&
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added file in modified submodule' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status >output &&
	grep "modified:   sub (new commits, modified content)" output
'

test_expect_success 'status with added file in modified submodule (porcelain)' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with untracked file in modified submodule' '
	(cd sub && git reset --hard) &&
	echo "content" >sub/new-file &&
	git status >output &&
	grep "modified:   sub (new commits, untracked content)" output
'

test_expect_success 'status with untracked file in modified submodule (porcelain)' '
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added and untracked file in modified submodule' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	echo "content" >sub/new-file &&
	git status >output &&
	grep "modified:   sub (new commits, modified content, untracked content)" output
'

test_expect_success 'status with added and untracked file in modified submodule (porcelain)' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	echo "content" >sub/new-file &&
	git status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'setup .git file for sub' '
	(cd sub &&
	 rm -f new-file
	 REAL="$(pwd)/../.real" &&
	 mv .git "$REAL"
	 echo "gitdir: $REAL" >.git) &&
	 echo .real >>.gitignore &&
	 git commit -m "added .real to .gitignore" .gitignore
'

test_expect_success 'status with added file in modified submodule with .git file' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status >output &&
	grep "modified:   sub (new commits, modified content)" output
'

test_expect_success 'rm submodule contents' '
	rm -rf sub/* sub/.git
'

test_expect_success 'status clean (empty submodule dir)' '
	git status >output &&
	grep "nothing to commit" output
'

test_expect_success 'status -a clean (empty submodule dir)' '
	test_must_fail git commit --dry-run -a >output &&
	grep "nothing to commit" output
'

test_done
