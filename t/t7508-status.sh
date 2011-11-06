#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git status'

. ./test-lib.sh

test_expect_success 'status -h in broken repository' '
	mkdir broken &&
	test_when_finished "rm -fr broken" &&
	(
		cd broken &&
		git init &&
		echo "[status] showuntrackedfiles = CORRUPT" >>.git/config &&
		test_expect_code 129 git status -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'commit -h in broken repository' '
	mkdir broken &&
	test_when_finished "rm -fr broken" &&
	(
		cd broken &&
		git init &&
		echo "[status] showuntrackedfiles = CORRUPT" >>.git/config &&
		test_expect_code 129 git commit -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'setup' '
	: >tracked &&
	: >modified &&
	mkdir dir1 &&
	: >dir1/tracked &&
	: >dir1/modified &&
	mkdir dir2 &&
	: >dir1/tracked &&
	: >dir1/modified &&
	git add . &&

	git status >output &&

	test_tick &&
	git commit -m initial &&
	: >untracked &&
	: >dir1/untracked &&
	: >dir2/untracked &&
	echo 1 >dir1/modified &&
	echo 2 >dir2/modified &&
	echo 3 >dir2/added &&
	git add dir2/added
'

test_expect_success 'status (1)' '
	test_i18ngrep "use \"git rm --cached <file>\.\.\.\" to unstage" output
'

cat >expect <<\EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF

test_expect_success 'status (2)' '
	git status >output &&
	test_i18ncmp expect output
'

cat >expect <<\EOF
# On branch master
# Changes to be committed:
#	new file:   dir2/added
#
# Changes not staged for commit:
#	modified:   dir1/modified
#
# Untracked files:
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF

test_expect_success 'status (advice.statusHints false)' '
	test_when_finished "git config --unset advice.statusHints" &&
	git config advice.statusHints false &&
	git status >output &&
	test_i18ncmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF

test_expect_success 'status -s' '

	git status -s >output &&
	test_cmp expect output

'

test_expect_success 'status with gitignore' '
	{
		echo ".gitignore" &&
		echo "expect" &&
		echo "output" &&
		echo "untracked"
	} >.gitignore &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	?? dir2/modified
	EOF
	git status -s >output &&
	test_cmp expect output &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	?? dir2/modified
	!! .gitignore
	!! dir1/untracked
	!! dir2/untracked
	!! expect
	!! output
	!! untracked
	EOF
	git status -s --ignored >output &&
	test_cmp expect output &&

	cat >expect <<-\EOF &&
	# On branch master
	# Changes to be committed:
	#   (use "git reset HEAD <file>..." to unstage)
	#
	#	new file:   dir2/added
	#
	# Changes not staged for commit:
	#   (use "git add <file>..." to update what will be committed)
	#   (use "git checkout -- <file>..." to discard changes in working directory)
	#
	#	modified:   dir1/modified
	#
	# Untracked files:
	#   (use "git add <file>..." to include in what will be committed)
	#
	#	dir2/modified
	# Ignored files:
	#   (use "git add -f <file>..." to include in what will be committed)
	#
	#	.gitignore
	#	dir1/untracked
	#	dir2/untracked
	#	expect
	#	output
	#	untracked
	EOF
	git status --ignored >output &&
	test_i18ncmp expect output
'

test_expect_success 'status with gitignore (nothing untracked)' '
	{
		echo ".gitignore" &&
		echo "expect" &&
		echo "dir2/modified" &&
		echo "output" &&
		echo "untracked"
	} >.gitignore &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	EOF
	git status -s >output &&
	test_cmp expect output &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	!! .gitignore
	!! dir1/untracked
	!! dir2/modified
	!! dir2/untracked
	!! expect
	!! output
	!! untracked
	EOF
	git status -s --ignored >output &&
	test_cmp expect output &&

	cat >expect <<-\EOF &&
	# On branch master
	# Changes to be committed:
	#   (use "git reset HEAD <file>..." to unstage)
	#
	#	new file:   dir2/added
	#
	# Changes not staged for commit:
	#   (use "git add <file>..." to update what will be committed)
	#   (use "git checkout -- <file>..." to discard changes in working directory)
	#
	#	modified:   dir1/modified
	#
	# Ignored files:
	#   (use "git add -f <file>..." to include in what will be committed)
	#
	#	.gitignore
	#	dir1/untracked
	#	dir2/modified
	#	dir2/untracked
	#	expect
	#	output
	#	untracked
	EOF
	git status --ignored >output &&
	test_i18ncmp expect output
'

rm -f .gitignore

cat >expect <<\EOF
## master
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF

test_expect_success 'status -s -b' '

	git status -s -b >output &&
	test_cmp expect output

'

test_expect_success 'setup dir3' '
	mkdir dir3 &&
	: >dir3/untracked1 &&
	: >dir3/untracked2
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files not listed (use -u option to show untracked files)
EOF
test_expect_success 'status -uno' '
	git status -uno >output &&
	test_i18ncmp expect output
'

test_expect_success 'status (status.showUntrackedFiles no)' '
	git config status.showuntrackedfiles no
	test_when_finished "git config --unset status.showuntrackedfiles" &&
	git status >output &&
	test_i18ncmp expect output
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#	new file:   dir2/added
#
# Changes not staged for commit:
#	modified:   dir1/modified
#
# Untracked files not listed
EOF
git config advice.statusHints false
test_expect_success 'status -uno (advice.statusHints false)' '
	git status -uno >output &&
	test_i18ncmp expect output
'
git config --unset advice.statusHints

cat >expect << EOF
 M dir1/modified
A  dir2/added
EOF
test_expect_success 'status -s -uno' '
	git status -s -uno >output &&
	test_cmp expect output
'

test_expect_success 'status -s (status.showUntrackedFiles no)' '
	git config status.showuntrackedfiles no
	git status -s >output &&
	test_cmp expect output
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	dir3/
#	expect
#	output
#	untracked
EOF
test_expect_success 'status -unormal' '
	git status -unormal >output &&
	test_i18ncmp expect output
'

test_expect_success 'status (status.showUntrackedFiles normal)' '
	git config status.showuntrackedfiles normal
	test_when_finished "git config --unset status.showuntrackedfiles" &&
	git status >output &&
	test_i18ncmp expect output
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? dir3/
?? expect
?? output
?? untracked
EOF
test_expect_success 'status -s -unormal' '
	git status -s -unormal >output &&
	test_cmp expect output
'

test_expect_success 'status -s (status.showUntrackedFiles normal)' '
	git config status.showuntrackedfiles normal
	git status -s >output &&
	test_cmp expect output
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	dir3/untracked1
#	dir3/untracked2
#	expect
#	output
#	untracked
EOF
test_expect_success 'status -uall' '
	git status -uall >output &&
	test_i18ncmp expect output
'

test_expect_success 'status (status.showUntrackedFiles all)' '
	git config status.showuntrackedfiles all
	test_when_finished "git config --unset status.showuntrackedfiles" &&
	git status >output &&
	test_i18ncmp expect output
'

test_expect_success 'teardown dir3' '
	rm -rf dir3
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF
test_expect_success 'status -s -uall' '
	git config --unset status.showuntrackedfiles
	git status -s -uall >output &&
	test_cmp expect output
'
test_expect_success 'status -s (status.showUntrackedFiles all)' '
	git config status.showuntrackedfiles all
	git status -s >output &&
	rm -rf dir3 &&
	git config --unset status.showuntrackedfiles &&
	test_cmp expect output
'

cat >expect <<\EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   ../dir2/added
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	untracked
#	../dir2/modified
#	../dir2/untracked
#	../expect
#	../output
#	../untracked
EOF

test_expect_success 'status with relative paths' '
	(cd dir1 && git status) >output &&
	test_i18ncmp expect output
'

cat >expect <<\EOF
 M modified
A  ../dir2/added
?? untracked
?? ../dir2/modified
?? ../dir2/untracked
?? ../expect
?? ../output
?? ../untracked
EOF
test_expect_success 'status -s with relative paths' '

	(cd dir1 && git status -s) >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF

test_expect_success 'status --porcelain ignores relative paths setting' '

	(cd dir1 && git status --porcelain) >output &&
	test_cmp expect output

'

test_expect_success 'setup unique colors' '

	git config status.color.untracked blue &&
	git config status.color.branch green

'

cat >expect <<\EOF
# On branch <GREEN>master<RESET>
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	<GREEN>new file:   dir2/added<RESET>
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	<RED>modified:   dir1/modified<RESET>
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	<BLUE>dir1/untracked<RESET>
#	<BLUE>dir2/modified<RESET>
#	<BLUE>dir2/untracked<RESET>
#	<BLUE>expect<RESET>
#	<BLUE>output<RESET>
#	<BLUE>untracked<RESET>
EOF

test_expect_success 'status with color.ui' '
	git config color.ui always &&
	test_when_finished "git config --unset color.ui" &&
	git status | test_decode_color >output &&
	test_i18ncmp expect output
'

test_expect_success 'status with color.status' '
	git config color.status always &&
	test_when_finished "git config --unset color.status" &&
	git status | test_decode_color >output &&
	test_i18ncmp expect output
'

cat >expect <<\EOF
 <RED>M<RESET> dir1/modified
<GREEN>A<RESET>  dir2/added
<BLUE>??<RESET> dir1/untracked
<BLUE>??<RESET> dir2/modified
<BLUE>??<RESET> dir2/untracked
<BLUE>??<RESET> expect
<BLUE>??<RESET> output
<BLUE>??<RESET> untracked
EOF

test_expect_success 'status -s with color.ui' '

	git config color.ui always &&
	git status -s | test_decode_color >output &&
	test_cmp expect output

'

test_expect_success 'status -s with color.status' '

	git config --unset color.ui &&
	git config color.status always &&
	git status -s | test_decode_color >output &&
	test_cmp expect output

'

cat >expect <<\EOF
## <GREEN>master<RESET>
 <RED>M<RESET> dir1/modified
<GREEN>A<RESET>  dir2/added
<BLUE>??<RESET> dir1/untracked
<BLUE>??<RESET> dir2/modified
<BLUE>??<RESET> dir2/untracked
<BLUE>??<RESET> expect
<BLUE>??<RESET> output
<BLUE>??<RESET> untracked
EOF

test_expect_success 'status -s -b with color.status' '

	git status -s -b | test_decode_color >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF

test_expect_success 'status --porcelain ignores color.ui' '

	git config --unset color.status &&
	git config color.ui always &&
	git status --porcelain | test_decode_color >output &&
	test_cmp expect output

'

test_expect_success 'status --porcelain ignores color.status' '

	git config --unset color.ui &&
	git config color.status always &&
	git status --porcelain | test_decode_color >output &&
	test_cmp expect output

'

# recover unconditionally from color tests
git config --unset color.status
git config --unset color.ui

test_expect_success 'status --porcelain ignores -b' '

	git status --porcelain -b >output &&
	test_cmp expect output

'

cat >expect <<\EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF


test_expect_success 'status without relative paths' '

	git config status.relativePaths false &&
	test_when_finished "git config --unset status.relativePaths" &&
	(cd dir1 && git status) >output &&
	test_i18ncmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF

test_expect_success 'status -s without relative paths' '

	git config status.relativePaths false &&
	test_when_finished "git config --unset status.relativePaths" &&
	(cd dir1 && git status -s) >output &&
	test_cmp expect output

'

cat <<EOF >expect
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/
#	expect
#	output
#	untracked
EOF
test_expect_success 'dry-run of partial commit excluding new file in index' '
	git commit --dry-run dir1/modified >output &&
	test_i18ncmp expect output
'

cat >expect <<EOF
:100644 100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0000000000000000000000000000000000000000 M	dir1/modified
EOF
test_expect_success 'status refreshes the index' '
	touch dir2/added &&
	git status &&
	git diff-files >output &&
	test_cmp expect output
'

test_expect_success 'setup status submodule summary' '
	test_create_repo sm && (
		cd sm &&
		>foo &&
		git add foo &&
		git commit -m "Add foo"
	) &&
	git add sm
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#	new file:   sm
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF
test_expect_success 'status submodule summary is disabled by default' '
	git status >output &&
	test_i18ncmp expect output
'

# we expect the same as the previous test
test_expect_success 'status --untracked-files=all does not show submodule' '
	git status --untracked-files=all >output &&
	test_i18ncmp expect output
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
A  sm
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF
test_expect_success 'status -s submodule summary is disabled by default' '
	git status -s >output &&
	test_cmp expect output
'

# we expect the same as the previous test
test_expect_success 'status -s --untracked-files=all does not show submodule' '
	git status -s --untracked-files=all >output &&
	test_cmp expect output
'

head=$(cd sm && git rev-parse --short=7 --verify HEAD)

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#	new file:   sm
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Submodule changes to be committed:
#
# * sm 0000000...$head (1):
#   > Add foo
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF
test_expect_success 'status submodule summary' '
	git config status.submodulesummary 10 &&
	git status >output &&
	test_i18ncmp expect output
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
A  sm
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF
test_expect_success 'status -s submodule summary' '
	git status -s >output &&
	test_cmp expect output
'

cat >expect <<EOF
# On branch master
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
no changes added to commit (use "git add" and/or "git commit -a")
EOF
test_expect_success 'status submodule summary (clean submodule): commit' '
	git commit -m "commit submodule" &&
	git config status.submodulesummary 10 &&
	test_must_fail git commit --dry-run >output &&
	test_i18ncmp expect output &&
	git status >output &&
	test_i18ncmp expect output
'

cat >expect <<EOF
 M dir1/modified
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? expect
?? output
?? untracked
EOF
test_expect_success 'status -s submodule summary (clean submodule)' '
	git status -s >output &&
	test_cmp expect output
'

test_expect_success 'status -z implies porcelain' '
	git status --porcelain |
	perl -pe "s/\012/\000/g" >expect &&
	git status -z >output &&
	test_cmp expect output
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD^1 <file>..." to unstage)
#
#	new file:   dir2/added
#	new file:   sm
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Submodule changes to be committed:
#
# * sm 0000000...$head (1):
#   > Add foo
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF
test_expect_success 'commit --dry-run submodule summary (--amend)' '
	git config status.submodulesummary 10 &&
	git commit --dry-run --amend >output &&
	test_i18ncmp expect output
'

test_expect_success POSIXPERM,SANITY 'status succeeds in a read-only repository' '
	(
		chmod a-w .git &&
		# make dir1/tracked stat-dirty
		>dir1/tracked1 && mv -f dir1/tracked1 dir1/tracked &&
		git status -s >output &&
		! grep dir1/tracked output &&
		# make sure "status" succeeded without writing index out
		git diff-files | grep dir1/tracked
	)
	status=$?
	chmod 775 .git
	(exit $status)
'

(cd sm && echo > bar && git add bar && git commit -q -m 'Add bar') && git add sm
new_head=$(cd sm && git rev-parse --short=7 --verify HEAD)
touch .gitmodules

cat > expect << EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	modified:   sm
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Submodule changes to be committed:
#
# * sm $head...$new_head (1):
#   > Add bar
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	.gitmodules
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF

test_expect_success '--ignore-submodules=untracked suppresses submodules with untracked content' '
	echo modified  sm/untracked &&
	git status --ignore-submodules=untracked >output &&
	test_i18ncmp expect output
'

test_expect_success '.gitmodules ignore=untracked suppresses submodules with untracked content' '
	git config diff.ignoreSubmodules dirty &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --add -f .gitmodules submodule.subname.ignore untracked &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname &&
	git config --unset diff.ignoreSubmodules
'

test_expect_success '.git/config ignore=untracked suppresses submodules with untracked content' '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore untracked &&
	git config --add submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --remove-section submodule.subname &&
	git config --remove-section -f .gitmodules submodule.subname
'

test_expect_success '--ignore-submodules=dirty suppresses submodules with untracked content' '
	git status --ignore-submodules=dirty >output &&
	test_i18ncmp expect output
'

test_expect_success '.gitmodules ignore=dirty suppresses submodules with untracked content' '
	git config diff.ignoreSubmodules dirty &&
	git status >output &&
	! test -s actual &&
	git config --add -f .gitmodules submodule.subname.ignore dirty &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname &&
	git config --unset diff.ignoreSubmodules
'

test_expect_success '.git/config ignore=dirty suppresses submodules with untracked content' '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore dirty &&
	git config --add submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --remove-section submodule.subname &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_success '--ignore-submodules=dirty suppresses submodules with modified content' '
	echo modified >sm/foo &&
	git status --ignore-submodules=dirty >output &&
	test_i18ncmp expect output
'

test_expect_success '.gitmodules ignore=dirty suppresses submodules with modified content' '
	git config --add -f .gitmodules submodule.subname.ignore dirty &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_success '.git/config ignore=dirty suppresses submodules with modified content' '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore dirty &&
	git config --add submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --remove-section submodule.subname &&
	git config -f .gitmodules  --remove-section submodule.subname
'

cat > expect << EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	modified:   sm
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#   (commit or discard the untracked or modified content in submodules)
#
#	modified:   dir1/modified
#	modified:   sm (modified content)
#
# Submodule changes to be committed:
#
# * sm $head...$new_head (1):
#   > Add bar
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	.gitmodules
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF

test_expect_success "--ignore-submodules=untracked doesn't suppress submodules with modified content" '
	git status --ignore-submodules=untracked > output &&
	test_i18ncmp expect output
'

test_expect_success ".gitmodules ignore=untracked doesn't suppress submodules with modified content" '
	git config --add -f .gitmodules submodule.subname.ignore untracked &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_success ".git/config ignore=untracked doesn't suppress submodules with modified content" '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore untracked &&
	git config --add submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --remove-section submodule.subname &&
	git config -f .gitmodules  --remove-section submodule.subname
'

head2=$(cd sm && git commit -q -m "2nd commit" foo && git rev-parse --short=7 --verify HEAD)

cat > expect << EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	modified:   sm
#
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#	modified:   sm (new commits)
#
# Submodule changes to be committed:
#
# * sm $head...$new_head (1):
#   > Add bar
#
# Submodules changed but not updated:
#
# * sm $new_head...$head2 (1):
#   > 2nd commit
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	.gitmodules
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
EOF

test_expect_success "--ignore-submodules=untracked doesn't suppress submodule summary" '
	git status --ignore-submodules=untracked > output &&
	test_i18ncmp expect output
'

test_expect_success ".gitmodules ignore=untracked doesn't suppress submodule summary" '
	git config --add -f .gitmodules submodule.subname.ignore untracked &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_success ".git/config ignore=untracked doesn't suppress submodule summary" '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore untracked &&
	git config --add submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --remove-section submodule.subname &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_success "--ignore-submodules=dirty doesn't suppress submodule summary" '
	git status --ignore-submodules=dirty > output &&
	test_i18ncmp expect output
'
test_expect_success ".gitmodules ignore=dirty doesn't suppress submodule summary" '
	git config --add -f .gitmodules submodule.subname.ignore dirty &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_success ".git/config ignore=dirty doesn't suppress submodule summary" '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore dirty &&
	git config --add submodule.subname.path sm &&
	git status >output &&
	test_i18ncmp expect output &&
	git config --remove-section submodule.subname &&
	git config -f .gitmodules  --remove-section submodule.subname
'

cat > expect << EOF
# On branch master
# Changes not staged for commit:
#   (use "git add <file>..." to update what will be committed)
#   (use "git checkout -- <file>..." to discard changes in working directory)
#
#	modified:   dir1/modified
#
# Untracked files:
#   (use "git add <file>..." to include in what will be committed)
#
#	.gitmodules
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	expect
#	output
#	untracked
no changes added to commit (use "git add" and/or "git commit -a")
EOF

test_expect_success "--ignore-submodules=all suppresses submodule summary" '
	git status --ignore-submodules=all > output &&
	test_i18ncmp expect output
'

test_expect_failure '.gitmodules ignore=all suppresses submodule summary' '
	git config --add -f .gitmodules submodule.subname.ignore all &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git status > output &&
	test_cmp expect output &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_expect_failure '.git/config ignore=all suppresses submodule summary' '
	git config --add -f .gitmodules submodule.subname.ignore none &&
	git config --add -f .gitmodules submodule.subname.path sm &&
	git config --add submodule.subname.ignore all &&
	git config --add submodule.subname.path sm &&
	git status > output &&
	test_cmp expect output &&
	git config --remove-section submodule.subname &&
	git config -f .gitmodules  --remove-section submodule.subname
'

test_done
