#!/bin/sh
#
# Copyright (c) 2009 Johan Herland
#

test_description='Test "git submodule foreach"

This test verifies that "git submodule foreach" correctly visits all submodules
that are currently checked out.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


test_expect_success 'setup a submodule tree' '
	git config --global protocol.file.allow always &&
	echo file > file &&
	git add file &&
	test_tick &&
	git commit -m upstream &&
	git clone . super &&
	git clone super submodule &&
	(
		cd super &&
		git submodule add ../submodule sub1 &&
		git submodule add ../submodule sub2 &&
		git submodule add ../submodule sub3 &&
		git config -f .gitmodules --rename-section \
			submodule.sub1 submodule.foo1 &&
		git config -f .gitmodules --rename-section \
			submodule.sub2 submodule.foo2 &&
		git config -f .gitmodules --rename-section \
			submodule.sub3 submodule.foo3 &&
		git add .gitmodules &&
		test_tick &&
		git commit -m "submodules" &&
		git submodule init sub1 &&
		git submodule init sub2 &&
		git submodule init sub3
	) &&
	(
		cd submodule &&
		echo different > file &&
		git add file &&
		test_tick &&
		git commit -m "different"
	) &&
	(
		cd super &&
		(
			cd sub3 &&
			git pull
		) &&
		git add sub3 &&
		test_tick &&
		git commit -m "update sub3"
	)
'

sub1sha1=$(cd super/sub1 && git rev-parse HEAD)
sub3sha1=$(cd super/sub3 && git rev-parse HEAD)

pwd=$(pwd)

cat > expect <<EOF
Entering 'sub1'
$pwd/clone-foo1-sub1-$sub1sha1
Entering 'sub3'
$pwd/clone-foo3-sub3-$sub3sha1
EOF

test_expect_success 'test basic "submodule foreach" usage' '
	git clone super clone &&
	(
		cd clone &&
		git submodule update --init -- sub1 sub3 &&
		git submodule foreach "echo \$toplevel-\$name-\$path-\$sha1" > ../actual &&
		git config foo.bar zar &&
		git submodule foreach "git config --file \"\$toplevel/.git/config\" foo.bar"
	) &&
	test_cmp expect actual
'

cat >expect <<EOF
Entering '../sub1'
$pwd/clone-foo1-sub1-../sub1-$sub1sha1
Entering '../sub3'
$pwd/clone-foo3-sub3-../sub3-$sub3sha1
EOF

test_expect_success 'test "submodule foreach" from subdirectory' '
	mkdir clone/sub &&
	(
		cd clone/sub &&
		git submodule foreach "echo \$toplevel-\$name-\$sm_path-\$displaypath-\$sha1" >../../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'setup nested submodules' '
	git clone submodule nested1 &&
	git clone submodule nested2 &&
	git clone submodule nested3 &&
	(
		cd nested3 &&
		git submodule add ../submodule submodule &&
		test_tick &&
		git commit -m "submodule" &&
		git submodule init submodule
	) &&
	(
		cd nested2 &&
		git submodule add ../nested3 nested3 &&
		test_tick &&
		git commit -m "nested3" &&
		git submodule init nested3
	) &&
	(
		cd nested1 &&
		git submodule add ../nested2 nested2 &&
		test_tick &&
		git commit -m "nested2" &&
		git submodule init nested2
	) &&
	(
		cd super &&
		git submodule add ../nested1 nested1 &&
		test_tick &&
		git commit -m "nested1" &&
		git submodule init nested1
	)
'

test_expect_success 'use "submodule foreach" to checkout 2nd level submodule' '
	git clone super clone2 &&
	(
		cd clone2 &&
		test_must_fail git rev-parse --resolve-git-dir sub1/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub2/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub3/.git &&
		test_must_fail git rev-parse --resolve-git-dir nested1/.git &&
		git submodule update --init &&
		git rev-parse --resolve-git-dir sub1/.git &&
		git rev-parse --resolve-git-dir sub2/.git &&
		git rev-parse --resolve-git-dir sub3/.git &&
		git rev-parse --resolve-git-dir nested1/.git &&
		test_must_fail git rev-parse --resolve-git-dir nested1/nested2/.git &&
		git submodule foreach "git submodule update --init" &&
		git rev-parse --resolve-git-dir nested1/nested2/.git &&
		test_must_fail git rev-parse --resolve-git-dir nested1/nested2/nested3/.git
	)
'

test_expect_success 'usage: foreach -- --not-an-option' '
	test_expect_code 1 git submodule foreach -- --not-an-option &&
	test_expect_code 1 git -C clone2 submodule foreach -- --not-an-option
'

test_expect_success 'use "foreach --recursive" to checkout all submodules' '
	(
		cd clone2 &&
		git submodule foreach --recursive "git submodule update --init" &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/submodule/.git
	)
'

cat > expect <<EOF
Entering 'nested1'
Entering 'nested1/nested2'
Entering 'nested1/nested2/nested3'
Entering 'nested1/nested2/nested3/submodule'
Entering 'sub1'
Entering 'sub2'
Entering 'sub3'
EOF

test_expect_success 'test messages from "foreach --recursive"' '
	(
		cd clone2 &&
		git submodule foreach --recursive "true" > ../actual
	) &&
	test_cmp expect actual
'

cat > expect <<EOF
Entering '../nested1'
Entering '../nested1/nested2'
Entering '../nested1/nested2/nested3'
Entering '../nested1/nested2/nested3/submodule'
Entering '../sub1'
Entering '../sub2'
Entering '../sub3'
EOF

test_expect_success 'test messages from "foreach --recursive" from subdirectory' '
	(
		cd clone2 &&
		mkdir untracked &&
		cd untracked &&
		git submodule foreach --recursive >../../actual
	) &&
	test_cmp expect actual
'
sub1sha1=$(cd clone2/sub1 && git rev-parse HEAD)
sub2sha1=$(cd clone2/sub2 && git rev-parse HEAD)
sub3sha1=$(cd clone2/sub3 && git rev-parse HEAD)
nested1sha1=$(cd clone2/nested1 && git rev-parse HEAD)
nested2sha1=$(cd clone2/nested1/nested2 && git rev-parse HEAD)
nested3sha1=$(cd clone2/nested1/nested2/nested3 && git rev-parse HEAD)
submodulesha1=$(cd clone2/nested1/nested2/nested3/submodule && git rev-parse HEAD)

cat >expect <<EOF
Entering '../nested1'
toplevel: $pwd/clone2 name: nested1 path: nested1 displaypath: ../nested1 hash: $nested1sha1
Entering '../nested1/nested2'
toplevel: $pwd/clone2/nested1 name: nested2 path: nested2 displaypath: ../nested1/nested2 hash: $nested2sha1
Entering '../nested1/nested2/nested3'
toplevel: $pwd/clone2/nested1/nested2 name: nested3 path: nested3 displaypath: ../nested1/nested2/nested3 hash: $nested3sha1
Entering '../nested1/nested2/nested3/submodule'
toplevel: $pwd/clone2/nested1/nested2/nested3 name: submodule path: submodule displaypath: ../nested1/nested2/nested3/submodule hash: $submodulesha1
Entering '../sub1'
toplevel: $pwd/clone2 name: foo1 path: sub1 displaypath: ../sub1 hash: $sub1sha1
Entering '../sub2'
toplevel: $pwd/clone2 name: foo2 path: sub2 displaypath: ../sub2 hash: $sub2sha1
Entering '../sub3'
toplevel: $pwd/clone2 name: foo3 path: sub3 displaypath: ../sub3 hash: $sub3sha1
EOF

test_expect_success 'test "submodule foreach --recursive" from subdirectory' '
	(
		cd clone2/untracked &&
		git submodule foreach --recursive "echo toplevel: \$toplevel name: \$name path: \$sm_path displaypath: \$displaypath hash: \$sha1" >../../actual
	) &&
	test_cmp expect actual
'

cat > expect <<EOF
nested1-nested1
nested2-nested2
nested3-nested3
submodule-submodule
foo1-sub1
foo2-sub2
foo3-sub3
EOF

test_expect_success 'test "foreach --quiet --recursive"' '
	(
		cd clone2 &&
		git submodule foreach -q --recursive "echo \$name-\$path" > ../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'use "update --recursive" to checkout all submodules' '
	git clone super clone3 &&
	(
		cd clone3 &&
		test_must_fail git rev-parse --resolve-git-dir sub1/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub2/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub3/.git &&
		test_must_fail git rev-parse --resolve-git-dir nested1/.git &&
		git submodule update --init --recursive &&
		git rev-parse --resolve-git-dir sub1/.git &&
		git rev-parse --resolve-git-dir sub2/.git &&
		git rev-parse --resolve-git-dir sub3/.git &&
		git rev-parse --resolve-git-dir nested1/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/submodule/.git
	)
'

nested1sha1=$(cd clone3/nested1 && git rev-parse HEAD)
nested2sha1=$(cd clone3/nested1/nested2 && git rev-parse HEAD)
nested3sha1=$(cd clone3/nested1/nested2/nested3 && git rev-parse HEAD)
submodulesha1=$(cd clone3/nested1/nested2/nested3/submodule && git rev-parse HEAD)
sub1sha1=$(cd clone3/sub1 && git rev-parse HEAD)
sub2sha1=$(cd clone3/sub2 && git rev-parse HEAD)
sub3sha1=$(cd clone3/sub3 && git rev-parse HEAD)
sub1sha1_short=$(cd clone3/sub1 && git rev-parse --short HEAD)
sub2sha1_short=$(cd clone3/sub2 && git rev-parse --short HEAD)

cat > expect <<EOF
 $nested1sha1 nested1 (heads/main)
 $nested2sha1 nested1/nested2 (heads/main)
 $nested3sha1 nested1/nested2/nested3 (heads/main)
 $submodulesha1 nested1/nested2/nested3/submodule (heads/main)
 $sub1sha1 sub1 ($sub1sha1_short)
 $sub2sha1 sub2 ($sub2sha1_short)
 $sub3sha1 sub3 (heads/main)
EOF

test_expect_success 'test "status --recursive"' '
	(
		cd clone3 &&
		git submodule status --recursive > ../actual
	) &&
	test_cmp expect actual
'

cat > expect <<EOF
 $nested1sha1 nested1 (heads/main)
+$nested2sha1 nested1/nested2 (file2~1)
 $nested3sha1 nested1/nested2/nested3 (heads/main)
 $submodulesha1 nested1/nested2/nested3/submodule (heads/main)
EOF

test_expect_success 'ensure "status --cached --recursive" preserves the --cached flag' '
	(
		cd clone3 &&
		(
			cd nested1/nested2 &&
			test_commit file2
		) &&
		git submodule status --cached --recursive -- nested1 > ../actual
	) &&
	test_cmp expect actual
'

nested2sha1=$(git -C clone3/nested1/nested2 rev-parse HEAD)

cat > expect <<EOF
 $nested1sha1 ../nested1 (heads/main)
+$nested2sha1 ../nested1/nested2 (file2)
 $nested3sha1 ../nested1/nested2/nested3 (heads/main)
 $submodulesha1 ../nested1/nested2/nested3/submodule (heads/main)
 $sub1sha1 ../sub1 ($sub1sha1_short)
 $sub2sha1 ../sub2 ($sub2sha1_short)
 $sub3sha1 ../sub3 (heads/main)
EOF

test_expect_success 'test "status --recursive" from sub directory' '
	(
		cd clone3 &&
		mkdir tmp && cd tmp &&
		git submodule status --recursive > ../../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'use "git clone --recursive" to checkout all submodules' '
	git clone --recursive super clone4 &&
	(
		cd clone4 &&
		git rev-parse --resolve-git-dir .git &&
		git rev-parse --resolve-git-dir sub1/.git &&
		git rev-parse --resolve-git-dir sub2/.git &&
		git rev-parse --resolve-git-dir sub3/.git &&
		git rev-parse --resolve-git-dir nested1/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/submodule/.git
	)
'

test_expect_success 'test "update --recursive" with a flag with spaces' '
	git clone super "common objects" &&
	git clone super clone5 &&
	(
		cd clone5 &&
		test_must_fail git rev-parse --resolve-git-dir d nested1/.git &&
		git submodule update --init --recursive --reference="$(dirname "$PWD")/common objects" &&
		git rev-parse --resolve-git-dir nested1/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/.git &&
		test -f .git/modules/nested1/objects/info/alternates &&
		test -f .git/modules/nested1/modules/nested2/objects/info/alternates &&
		test -f .git/modules/nested1/modules/nested2/modules/nested3/objects/info/alternates
	)
'

test_expect_success 'use "update --recursive nested1" to checkout all submodules rooted in nested1' '
	git clone super clone6 &&
	(
		cd clone6 &&
		test_must_fail git rev-parse --resolve-git-dir sub1/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub2/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub3/.git &&
		test_must_fail git rev-parse --resolve-git-dir nested1/.git &&
		git submodule update --init --recursive -- nested1 &&
		test_must_fail git rev-parse --resolve-git-dir sub1/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub2/.git &&
		test_must_fail git rev-parse --resolve-git-dir sub3/.git &&
		git rev-parse --resolve-git-dir nested1/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/.git &&
		git rev-parse --resolve-git-dir nested1/nested2/nested3/submodule/.git
	)
'

test_expect_success 'command passed to foreach retains notion of stdin' '
	(
		cd super &&
		git submodule foreach echo success >../expected &&
		yes | git submodule foreach "read y && test \"x\$y\" = xy && echo success" >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'command passed to foreach --recursive retains notion of stdin' '
	(
		cd clone2 &&
		git submodule foreach --recursive echo success >../expected &&
		yes | git submodule foreach --recursive "read y && test \"x\$y\" = xy && echo success" >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'multi-argument command passed to foreach is not shell-evaluated twice' '
	(
		cd super &&
		git submodule foreach "echo \\\"quoted\\\"" > ../expected &&
		git submodule foreach echo \"quoted\" > ../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'option-like arguments passed to foreach commands are not lost' '
	(
		cd super &&
		git submodule foreach "echo be --quiet" > ../expected &&
		git submodule foreach echo be --quiet > ../actual
	) &&
	grep -sq -e "--quiet" expected &&
	test_cmp expected actual
'

test_expect_success 'option-like arguments passed to foreach recurse correctly' '
	git -C clone2 submodule foreach --recursive "echo be --an-option" >expect &&
	git -C clone2 submodule foreach --recursive echo be --an-option >actual &&
	grep -e "--an-option" expect &&
	test_cmp expect actual
'

test_done
