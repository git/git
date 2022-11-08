#!/bin/sh

test_description='git status for submodule'

TEST_PASSES_SANITIZE_LEAK=true
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

sanitize_output () {
	sed -e "s/$OID_REGEX/HASH/" -e "s/$OID_REGEX/HASH/" output >output2 &&
	mv output2 output
}

sanitize_diff () {
	sed -e "/^index [0-9a-f,]*\.\.[0-9a-f]*/d" "$1"
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

test_expect_success 'status with modified file in submodule (short)' '
	(cd sub && git reset --hard) &&
	echo "changed" >sub/foo &&
	git status --short >output &&
	diff output - <<-\EOF
	 m sub
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

test_expect_success 'status with added file in submodule (short)' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status --short >output &&
	diff output - <<-\EOF
	 m sub
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

test_expect_success 'status with untracked file in submodule (short)' '
	git status --short >output &&
	diff output - <<-\EOF
	 ? sub
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
	 rm -f new-file &&
	 REAL="$(pwd)/../.real" &&
	 mv .git "$REAL" &&
	 echo "gitdir: $REAL" >.git) &&
	 echo .real >>.gitignore &&
	 git commit -m "added .real to .gitignore" .gitignore
'

test_expect_success 'status with added file in modified submodule with .git file' '
	(cd sub && git reset --hard && echo >foo && git add foo) &&
	git status >output &&
	test_i18ngrep "modified:   sub (new commits, modified content)" output
'

test_expect_success 'status with a lot of untracked files in the submodule' '
	(
		cd sub &&
		i=0 &&
		while test $i -lt 1024
		do
			>some-file-$i &&
			i=$(( $i + 1 )) || exit 1
		done
	) &&
	git status --porcelain sub 2>err.actual &&
	test_must_be_empty err.actual &&
	rm err.actual
'

test_expect_success 'rm submodule contents' '
	rm -rf sub &&
	mkdir sub
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
	test_config_global protocol.file.allow always &&
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
	sanitize_diff diff_actual >diff_sanitized &&
	test_cmp diff_expect diff_sanitized
'

test_expect_success 'diff --submodule with merge conflict in .gitmodules' '
	(
		cd super &&
		git diff --submodule >../diff_submodule_actual 2>&1
	) &&
	sanitize_diff diff_submodule_actual >diff_sanitized &&
	test_cmp diff_submodule_expect diff_sanitized
'

# We'll setup different cases for further testing:
# sub1 will contain a nested submodule,
# sub2 will have an untracked file
# sub3 will have an untracked repository
test_expect_success 'setup superproject with untracked file in nested submodule' '
	test_config_global protocol.file.allow always &&
	(
		cd super &&
		git clean -dfx &&
		git rm .gitmodules &&
		git commit -m "remove .gitmodules" &&
		git submodule add -f ./sub1 &&
		git submodule add -f ./sub2 &&
		git submodule add -f ./sub1 sub3 &&
		git commit -a -m "messy merge in superproject" &&
		(
			cd sub1 &&
			git submodule add ../sub2 &&
			git commit -a -m "add sub2 to sub1"
		) &&
		git add sub1 &&
		git commit -a -m "update sub1 to contain nested sub"
	) &&
	echo content >super/sub1/sub2/file &&
	echo content >super/sub2/file &&
	git -C super/sub3 clone ../../sub2 untracked_repository
'

test_expect_success 'status with untracked file in nested submodule (porcelain)' '
	git -C super status --porcelain >output &&
	diff output - <<-\EOF
	 M sub1
	 M sub2
	 M sub3
	EOF
'

test_expect_success 'status with untracked file in nested submodule (porcelain=2)' '
	git -C super status --porcelain=2 >output &&
	sanitize_output output &&
	diff output - <<-\EOF
	1 .M S..U 160000 160000 160000 HASH HASH sub1
	1 .M S..U 160000 160000 160000 HASH HASH sub2
	1 .M S..U 160000 160000 160000 HASH HASH sub3
	EOF
'

test_expect_success 'status with untracked file in nested submodule (short)' '
	git -C super status --short >output &&
	diff output - <<-\EOF
	 ? sub1
	 ? sub2
	 ? sub3
	EOF
'

test_expect_success 'setup superproject with modified file in nested submodule' '
	git -C super/sub1/sub2 add file &&
	git -C super/sub2 add file
'

test_expect_success 'status with added file in nested submodule (porcelain)' '
	git -C super status --porcelain >output &&
	diff output - <<-\EOF
	 M sub1
	 M sub2
	 M sub3
	EOF
'

test_expect_success 'status with added file in nested submodule (porcelain=2)' '
	git -C super status --porcelain=2 >output &&
	sanitize_output output &&
	diff output - <<-\EOF
	1 .M S.M. 160000 160000 160000 HASH HASH sub1
	1 .M S.M. 160000 160000 160000 HASH HASH sub2
	1 .M S..U 160000 160000 160000 HASH HASH sub3
	EOF
'

test_expect_success 'status with added file in nested submodule (short)' '
	git -C super status --short >output &&
	diff output - <<-\EOF
	 m sub1
	 m sub2
	 ? sub3
	EOF
'

test_expect_success 'status in superproject with submodules respects parallel settings' '
	test_when_finished "rm -f trace.out" &&
	(
		GIT_TRACE=$(pwd)/trace.out git status &&
		grep "1 tasks" trace.out &&
		>trace.out &&

		git config submodule.diffJobs 8 &&
		GIT_TRACE=$(pwd)/trace.out git status &&
		grep "8 tasks" trace.out &&
		>trace.out &&

		GIT_TRACE=$(pwd)/trace.out git -c submodule.diffJobs=0 status &&
		grep "preparing to run up to [0-9]* tasks" trace.out &&
		! grep "up to 0 tasks" trace.out &&
		>trace.out
	)
'

test_done
