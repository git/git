#!/bin/sh

test_description='translate WSL/Cygwin /mnt/<x>/ paths in worktree gitfiles

Verify that `git worktree add` artefacts written from inside WSL2 or
Cygwin/MSYS - which use POSIX-mounted paths like `/mnt/c/...` or
`/cygdrive/c/...` - are still resolvable when read back from native
Windows git.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Convert any drive-prefixed path Windows git might emit to a POSIX-mount
# form rooted at $1. Handles MSYS form (/c/foo), Windows forward-slash
# form (C:/foo), and Windows backslash form (C:\foo). $1 may be empty,
# in which case the result is the MSYS2 mount form (/c/foo). MINGW-only.
mount_form () {
	prefix=$1
	path=$2
	case "$path" in
	/[A-Za-z]/*)
		echo "$path" | sed -E "s|^/([A-Za-z])/|$prefix/\\L\\1/|"
		;;
	[A-Za-z]:/*)
		echo "$path" | sed -E "s|^([A-Za-z]):/|$prefix/\\L\\1/|"
		;;
	[A-Za-z]:'\'*)
		echo "$path" | sed -E "s|^([A-Za-z]):.|$prefix/\\L\\1/|; s|\\\\|/|g"
		;;
	*)
		echo "$path"
		;;
	esac
}

to_mnt ()      { mount_form /mnt      "$1"; }
to_cygdrive () { mount_form /cygdrive "$1"; }
to_msys ()     { mount_form ""        "$1"; }

test_expect_success MINGW 'setup main repo' '
	git init repo &&
	test_commit -C repo init
'

test_expect_success MINGW 'read_gitfile_gently translates /mnt/<x>/ gitdir' '
	test_when_finished "rm -rf wtlink actual" &&
	REAL=$(cd repo/.git && pwd) &&
	MNT=$(to_mnt "$REAL") &&

	# Sanity: the path must actually start with /mnt/ - if it does not,
	# the host shell did not give us a path with a drive prefix and the
	# rest of the test would be silently meaningless.
	case "$MNT" in
	/mnt/*) : ok ;;
	*) BUG "to_mnt produced $MNT from $REAL" ;;
	esac &&

	mkdir wtlink &&
	printf "gitdir: %s\n" "$MNT" >wtlink/.git &&

	(cd wtlink && git rev-parse --git-dir) >actual &&
	test_path_is_dir "$(cat actual)"
'

test_expect_success MINGW 'read_gitfile_gently translates /cygdrive/<x>/ gitdir' '
	test_when_finished "rm -rf wtlink actual" &&
	REAL=$(cd repo/.git && pwd) &&
	CYG=$(to_cygdrive "$REAL") &&

	mkdir wtlink &&
	printf "gitdir: %s\n" "$CYG" >wtlink/.git &&

	(cd wtlink && git rev-parse --git-dir) >actual &&
	test_path_is_dir "$(cat actual)"
'

test_expect_success MINGW 'read_gitfile_gently translates /<x>/ MSYS2 gitdir' '
	test_when_finished "rm -rf wtlink actual" &&
	REAL=$(cd repo/.git && pwd) &&
	MSYS_PATH=$(to_msys "$REAL") &&

	case "$MSYS_PATH" in
	/[A-Za-z]/*) : ok ;;
	*) BUG "to_msys produced $MSYS_PATH from $REAL" ;;
	esac &&

	mkdir wtlink &&
	printf "gitdir: %s\n" "$MSYS_PATH" >wtlink/.git &&

	(cd wtlink && git rev-parse --git-dir) >actual &&
	test_path_is_dir "$(cat actual)"
'

test_expect_success MINGW 'read_gitfile_gently leaves /mnt/<multichar>/ alone' '
	test_when_finished "rm -rf wtlink" &&
	mkdir wtlink &&
	# "storage" is not a single drive letter, so this must not be
	# translated. The path does not exist on Windows, so the open fails.
	echo "gitdir: /mnt/storage/no/such/repo" >wtlink/.git &&

	test_must_fail git -C wtlink rev-parse --git-dir 2>err &&
	test_grep "not a git repository" err
'

test_expect_success MINGW 'get_linked_worktree finds worktree recorded with /mnt/<x>/ path' '
	test_when_finished "rm -rf repo/wt repo/.git/worktrees/wt" &&

	git -C repo worktree add --detach wt &&
	WT_REAL=$(cd repo/wt && pwd) &&
	WT_MNT=$(to_mnt "$WT_REAL") &&

	# Overwrite the recorded worktree path with the WSL form, mimicking
	# what `git worktree add` writes when run from inside WSL.
	printf "%s/.git\n" "$WT_MNT" >repo/.git/worktrees/wt/gitdir &&

	# `git worktree list` reads that file via get_linked_worktree.
	# After translation the worktree must still be reachable: it must
	# NOT be flagged prunable, and a git operation inside the worktree
	# directory must succeed.
	git -C repo worktree list --porcelain >list &&
	! grep -q "^prunable" list &&
	(cd "$WT_REAL" && git rev-parse --is-inside-work-tree)
'

test_expect_success MINGW 'get_common_dir_noenv translates /mnt/<x>/ commondir' '
	test_when_finished "rm -rf wtdir wt actual" &&

	REAL=$(cd repo/.git && pwd) &&
	MNT=$(to_mnt "$REAL") &&

	# Build a synthetic linked-worktree gitdir that points at the main
	# repo via a /mnt/<x>/ commondir record.
	mkdir wtdir &&
	echo "$(cd repo && git rev-parse HEAD)" >wtdir/HEAD &&
	echo "$MNT" >wtdir/commondir &&
	printf "%s/.git\n" "$(pwd)" >wtdir/gitdir &&

	# rev-parse --git-common-dir on a checkout that points here should
	# resolve through the translated commondir.
	mkdir wt &&
	printf "gitdir: %s\n" "$(pwd)/wtdir" >wt/.git &&
	(cd wt && git rev-parse --git-common-dir) >actual &&
	test_path_is_dir "$(cat actual)"
'

test_done
