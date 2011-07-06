#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git-status'

. ./test-lib.sh

test_expect_success 'setup' '
	: > tracked &&
	: > modified &&
	mkdir dir1 &&
	: > dir1/tracked &&
	: > dir1/modified &&
	mkdir dir2 &&
	: > dir1/tracked &&
	: > dir1/modified &&
	git add . &&

	git status >output &&

	test_tick &&
	git commit -m initial &&
	: > untracked &&
	: > dir1/untracked &&
	: > dir2/untracked &&
	echo 1 > dir1/modified &&
	echo 2 > dir2/modified &&
	echo 3 > dir2/added &&
	git add dir2/added
'

test_expect_success 'status (1)' '

	grep "use \"git rm --cached <file>\.\.\.\" to unstage" output

'

cat > expect << \EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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

	git status > output &&
	test_cmp expect output

'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
#
#	modified:   dir1/modified
#
# Untracked files not listed (use -u option to show untracked files)
EOF
test_expect_success 'status -uno' '
	mkdir dir3 &&
	: > dir3/untracked1 &&
	: > dir3/untracked2 &&
	git status -uno >output &&
	test_cmp expect output
'

test_expect_success 'status (status.showUntrackedFiles no)' '
	git config status.showuntrackedfiles no
	git status >output &&
	test_cmp expect output
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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
	test_cmp expect output
'

test_expect_success 'status (status.showUntrackedFiles normal)' '
	git config status.showuntrackedfiles normal
	git status >output &&
	test_cmp expect output
'

cat >expect <<EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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
	test_cmp expect output
'
test_expect_success 'status (status.showUntrackedFiles all)' '
	git config status.showuntrackedfiles all
	git status >output &&
	rm -rf dir3 &&
	git config --unset status.showuntrackedfiles &&
	test_cmp expect output
'

cat > expect << \EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   ../dir2/added
#
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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

	(cd dir1 && git status) > output &&
	test_cmp expect output

'

cat > expect << \EOF
# On branch master
# Changes to be committed:
#   (use "git reset HEAD <file>..." to unstage)
#
#	new file:   dir2/added
#
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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

	git config status.relativePaths false
	(cd dir1 && git status) > output &&
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
test_expect_success 'status of partial commit excluding new file in index' '
	git status dir1/modified >output &&
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
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
#
#	modified:   dir1/modified
#
# Modified submodules:
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
	test_cmp expect output
'


cat >expect <<EOF
# On branch master
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
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
test_expect_success 'status submodule summary (clean submodule)' '
	git commit -m "commit submodule" &&
	git config status.submodulesummary 10 &&
	test_must_fail git status >output &&
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
# Changed but not updated:
#   (use "git add <file>..." to update what will be committed)
#
#	modified:   dir1/modified
#
# Modified submodules:
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
test_expect_success 'status submodule summary (--amend)' '
	git config status.submodulesummary 10 &&
	git status --amend >output &&
	test_cmp expect output
'

test_done
