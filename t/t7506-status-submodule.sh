#!/bin/sh

test_description='git status for submodule'

. ./test-lib.sh

test_create_repo_with_commit () {
	test_create_repo "$1" &&
	(
		cd "$1" &&
		: >bar &&
		git add bar &&
		git commit -m " Add bar" &&
		: >foo &&
		git add foo &&
		git commit -m " Add foo"
	)
}

test_expect_success 'setup' '
	test_create_repo_with_commit sub &&
	echo output > .gitignore &&
	git add sub .gitignore &&
	git commit -m "Add submodule sub"
'

test_expect_success 'status clean' '
	git status >output &&
	test_i18ngrep "nothing to commit" output
'

test_expect_success 'commit --dry-run -a clean' '
	test_must_fail git commit --dry-run -a >output &&
	test_i18ngrep "nothing to commit" output
'

test_expect_success 'status with modified file in submodule' '
	(cd sub && git reset --hard) &&
	echo "changed" >sub/foo &&
	git status >output &&
	test_i18ngrep "modified:   sub (modified content)" output
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
	test_i18ngrep "modified:   sub (modified content)" output
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
	test_i18ngrep "modified:   sub (untracked content)" output
'

test_expect_success 'status -uno with untracked file in submodule' '
	git status -uno >output &&
	test_i18ngrep "^nothing to commit" output
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
	test_i18ngrep "modified:   sub (modified content, untracked content)" output
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
	test_i18ngrep "modified:   sub (new commits, modified content)" output
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
	test_i18ngrep "modified:   sub (new commits, modified content)" output
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
	test_i18ngrep "modified:   sub (new commits, untracked content)" output
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
	test_i18ngrep "modified:   sub (new commits, modified content, untracked content)" output
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
	test_i18ngrep "modified:   sub (new commits, modified content)" output
'

test_expect_success 'rm submodule contents' '
	rm -rf sub/* sub/.git
'

test_expect_success 'status clean (empty submodule dir)' '
	git status >output &&
	test_i18ngrep "nothing to commit" output
'

test_expect_success 'status -a clean (empty submodule dir)' '
	test_must_fail git commit --dry-run -a >output &&
	test_i18ngrep "nothing to commit" output
'

cat >status_expect <<\EOF
AA .gitmodules
A  sub1
EOF

test_expect_success 'status with merge conflict in .gitmodules' '
	git clone . super &&
	test_create_repo_with_commit sub1 &&
	test_tick &&
	test_create_repo_with_commit sub2 &&
	(
		cd super &&
		prev=$(git rev-parse HEAD) &&
		git checkout -b add_sub1 &&
		git submodule add ../sub1 &&
		git commit -m "add sub1" &&
		git checkout -b add_sub2 $prev &&
		git submodule add ../sub2 &&
		git commit -m "add sub2" &&
		git checkout -b merge_conflict_gitmodules &&
		test_must_fail git merge add_sub1 &&
		git status -s >../status_actual 2>&1
	) &&
	test_cmp status_actual status_expect
'

sha1_merge_sub1=$(cd sub1 && git rev-parse HEAD)
sha1_merge_sub2=$(cd sub2 && git rev-parse HEAD)
short_sha1_merge_sub1=$(cd sub1 && git rev-parse --short HEAD)
short_sha1_merge_sub2=$(cd sub2 && git rev-parse --short HEAD)
cat >diff_expect <<\EOF
diff --cc .gitmodules
index badaa4c,44f999a..0000000
--- a/.gitmodules
+++ b/.gitmodules
@@@ -1,3 -1,3 +1,9 @@@
++<<<<<<< HEAD
 +[submodule "sub2"]
 +	path = sub2
 +	url = ../sub2
++=======
+ [submodule "sub1"]
+ 	path = sub1
+ 	url = ../sub1
++>>>>>>> add_sub1
EOF

cat >diff_submodule_expect <<\EOF
diff --cc .gitmodules
index badaa4c,44f999a..0000000
--- a/.gitmodules
+++ b/.gitmodules
@@@ -1,3 -1,3 +1,9 @@@
++<<<<<<< HEAD
 +[submodule "sub2"]
 +	path = sub2
 +	url = ../sub2
++=======
+ [submodule "sub1"]
+ 	path = sub1
+ 	url = ../sub1
++>>>>>>> add_sub1
EOF

test_expect_success 'diff with merge conflict in .gitmodules' '
	(
		cd super &&
		git diff >../diff_actual 2>&1
	) &&
	test_cmp diff_actual diff_expect
'

test_expect_success 'diff --submodule with merge conflict in .gitmodules' '
	(
		cd super &&
		git diff --submodule >../diff_submodule_actual 2>&1
	) &&
	test_cmp diff_submodule_actual diff_submodule_expect
'

test_done
