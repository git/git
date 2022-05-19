#!/bin/sh

test_description='but status for submodule'

. ./test-lib.sh

test_create_repo_with_cummit () {
	test_create_repo "$1" &&
	(
		cd "$1" &&
		: >bar &&
		but add bar &&
		but cummit -m " Add bar" &&
		: >foo &&
		but add foo &&
		but cummit -m " Add foo"
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
	test_create_repo_with_cummit sub &&
	echo output > .butignore &&
	but add sub .butignore &&
	but cummit -m "Add submodule sub"
'

test_expect_success 'status clean' '
	but status >output &&
	test_i18ngrep "nothing to cummit" output
'

test_expect_success 'cummit --dry-run -a clean' '
	test_must_fail but cummit --dry-run -a >output &&
	test_i18ngrep "nothing to cummit" output
'

test_expect_success 'status with modified file in submodule' '
	(cd sub && but reset --hard) &&
	echo "changed" >sub/foo &&
	but status >output &&
	test_i18ngrep "modified:   sub (modified content)" output
'

test_expect_success 'status with modified file in submodule (porcelain)' '
	(cd sub && but reset --hard) &&
	echo "changed" >sub/foo &&
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with modified file in submodule (short)' '
	(cd sub && but reset --hard) &&
	echo "changed" >sub/foo &&
	but status --short >output &&
	diff output - <<-\EOF
	 m sub
	EOF
'

test_expect_success 'status with added file in submodule' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	but status >output &&
	test_i18ngrep "modified:   sub (modified content)" output
'

test_expect_success 'status with added file in submodule (porcelain)' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added file in submodule (short)' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	but status --short >output &&
	diff output - <<-\EOF
	 m sub
	EOF
'

test_expect_success 'status with untracked file in submodule' '
	(cd sub && but reset --hard) &&
	echo "content" >sub/new-file &&
	but status >output &&
	test_i18ngrep "modified:   sub (untracked content)" output
'

test_expect_success 'status -uno with untracked file in submodule' '
	but status -uno >output &&
	test_i18ngrep "^nothing to cummit" output
'

test_expect_success 'status with untracked file in submodule (porcelain)' '
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with untracked file in submodule (short)' '
	but status --short >output &&
	diff output - <<-\EOF
	 ? sub
	EOF
'

test_expect_success 'status with added and untracked file in submodule' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	echo "content" >sub/new-file &&
	but status >output &&
	test_i18ngrep "modified:   sub (modified content, untracked content)" output
'

test_expect_success 'status with added and untracked file in submodule (porcelain)' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	echo "content" >sub/new-file &&
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with modified file in modified submodule' '
	(cd sub && but reset --hard) &&
	rm sub/new-file &&
	(cd sub && echo "next change" >foo && but cummit -m "next change" foo) &&
	echo "changed" >sub/foo &&
	but status >output &&
	test_i18ngrep "modified:   sub (new cummits, modified content)" output
'

test_expect_success 'status with modified file in modified submodule (porcelain)' '
	(cd sub && but reset --hard) &&
	echo "changed" >sub/foo &&
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added file in modified submodule' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	but status >output &&
	test_i18ngrep "modified:   sub (new cummits, modified content)" output
'

test_expect_success 'status with added file in modified submodule (porcelain)' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with untracked file in modified submodule' '
	(cd sub && but reset --hard) &&
	echo "content" >sub/new-file &&
	but status >output &&
	test_i18ngrep "modified:   sub (new cummits, untracked content)" output
'

test_expect_success 'status with untracked file in modified submodule (porcelain)' '
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'status with added and untracked file in modified submodule' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	echo "content" >sub/new-file &&
	but status >output &&
	test_i18ngrep "modified:   sub (new cummits, modified content, untracked content)" output
'

test_expect_success 'status with added and untracked file in modified submodule (porcelain)' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	echo "content" >sub/new-file &&
	but status --porcelain >output &&
	diff output - <<-\EOF
	 M sub
	EOF
'

test_expect_success 'setup .but file for sub' '
	(cd sub &&
	 rm -f new-file &&
	 REAL="$(pwd)/../.real" &&
	 mv .but "$REAL" &&
	 echo "butdir: $REAL" >.but) &&
	 echo .real >>.butignore &&
	 but cummit -m "added .real to .butignore" .butignore
'

test_expect_success 'status with added file in modified submodule with .but file' '
	(cd sub && but reset --hard && echo >foo && but add foo) &&
	but status >output &&
	test_i18ngrep "modified:   sub (new cummits, modified content)" output
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
	but status --porcelain sub 2>err.actual &&
	test_must_be_empty err.actual &&
	rm err.actual
'

test_expect_success 'rm submodule contents' '
	rm -rf sub &&
	mkdir sub
'

test_expect_success 'status clean (empty submodule dir)' '
	but status >output &&
	test_i18ngrep "nothing to cummit" output
'

test_expect_success 'status -a clean (empty submodule dir)' '
	test_must_fail but cummit --dry-run -a >output &&
	test_i18ngrep "nothing to cummit" output
'

cat >status_expect <<\EOF
AA .butmodules
A  sub1
EOF

test_expect_success 'status with merge conflict in .butmodules' '
	but clone . super &&
	test_create_repo_with_cummit sub1 &&
	test_tick &&
	test_create_repo_with_cummit sub2 &&
	(
		cd super &&
		prev=$(but rev-parse HEAD) &&
		but checkout -b add_sub1 &&
		but submodule add ../sub1 &&
		but cummit -m "add sub1" &&
		but checkout -b add_sub2 $prev &&
		but submodule add ../sub2 &&
		but cummit -m "add sub2" &&
		but checkout -b merge_conflict_butmodules &&
		test_must_fail but merge add_sub1 &&
		but status -s >../status_actual 2>&1
	) &&
	test_cmp status_actual status_expect
'

sha1_merge_sub1=$(cd sub1 && but rev-parse HEAD)
sha1_merge_sub2=$(cd sub2 && but rev-parse HEAD)
short_sha1_merge_sub1=$(cd sub1 && but rev-parse --short HEAD)
short_sha1_merge_sub2=$(cd sub2 && but rev-parse --short HEAD)
cat >diff_expect <<\EOF
diff --cc .butmodules
--- a/.butmodules
+++ b/.butmodules
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
diff --cc .butmodules
--- a/.butmodules
+++ b/.butmodules
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

test_expect_success 'diff with merge conflict in .butmodules' '
	(
		cd super &&
		but diff >../diff_actual 2>&1
	) &&
	sanitize_diff diff_actual >diff_sanitized &&
	test_cmp diff_expect diff_sanitized
'

test_expect_success 'diff --submodule with merge conflict in .butmodules' '
	(
		cd super &&
		but diff --submodule >../diff_submodule_actual 2>&1
	) &&
	sanitize_diff diff_submodule_actual >diff_sanitized &&
	test_cmp diff_submodule_expect diff_sanitized
'

# We'll setup different cases for further testing:
# sub1 will contain a nested submodule,
# sub2 will have an untracked file
# sub3 will have an untracked repository
test_expect_success 'setup superproject with untracked file in nested submodule' '
	(
		cd super &&
		but clean -dfx &&
		but rm .butmodules &&
		but cummit -m "remove .butmodules" &&
		but submodule add -f ./sub1 &&
		but submodule add -f ./sub2 &&
		but submodule add -f ./sub1 sub3 &&
		but cummit -a -m "messy merge in superproject" &&
		(
			cd sub1 &&
			but submodule add ../sub2 &&
			but cummit -a -m "add sub2 to sub1"
		) &&
		but add sub1 &&
		but cummit -a -m "update sub1 to contain nested sub"
	) &&
	echo content >super/sub1/sub2/file &&
	echo content >super/sub2/file &&
	but -C super/sub3 clone ../../sub2 untracked_repository
'

test_expect_success 'status with untracked file in nested submodule (porcelain)' '
	but -C super status --porcelain >output &&
	diff output - <<-\EOF
	 M sub1
	 M sub2
	 M sub3
	EOF
'

test_expect_success 'status with untracked file in nested submodule (porcelain=2)' '
	but -C super status --porcelain=2 >output &&
	sanitize_output output &&
	diff output - <<-\EOF
	1 .M S..U 160000 160000 160000 HASH HASH sub1
	1 .M S..U 160000 160000 160000 HASH HASH sub2
	1 .M S..U 160000 160000 160000 HASH HASH sub3
	EOF
'

test_expect_success 'status with untracked file in nested submodule (short)' '
	but -C super status --short >output &&
	diff output - <<-\EOF
	 ? sub1
	 ? sub2
	 ? sub3
	EOF
'

test_expect_success 'setup superproject with modified file in nested submodule' '
	but -C super/sub1/sub2 add file &&
	but -C super/sub2 add file
'

test_expect_success 'status with added file in nested submodule (porcelain)' '
	but -C super status --porcelain >output &&
	diff output - <<-\EOF
	 M sub1
	 M sub2
	 M sub3
	EOF
'

test_expect_success 'status with added file in nested submodule (porcelain=2)' '
	but -C super status --porcelain=2 >output &&
	sanitize_output output &&
	diff output - <<-\EOF
	1 .M S.M. 160000 160000 160000 HASH HASH sub1
	1 .M S.M. 160000 160000 160000 HASH HASH sub2
	1 .M S..U 160000 160000 160000 HASH HASH sub3
	EOF
'

test_expect_success 'status with added file in nested submodule (short)' '
	but -C super status --short >output &&
	diff output - <<-\EOF
	 m sub1
	 m sub2
	 ? sub3
	EOF
'

test_done
