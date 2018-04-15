#!/bin/sh

test_description="Tests of cwd/prefix/worktree/gitdir setup in all cases

A few rules for repo setup:

1. GIT_DIR is relative to user's cwd. --git-dir is equivalent to
   GIT_DIR.

2. .git file is relative to parent directory. .git file is basically
   symlink in disguise. The directory where .git file points to will
   become new git_dir.

3. core.worktree is relative to git_dir.

4. GIT_WORK_TREE is relative to user's cwd. --work-tree is
   equivalent to GIT_WORK_TREE.

5. GIT_WORK_TREE/core.worktree was originally meant to work only if
   GIT_DIR is set, but earlier git didn't enforce it, and some scripts
   depend on the implementation that happened to first discover .git by
   going up from the users $cwd and then using the specified working tree
   that may or may not have any relation to where .git was found in.  This
   historical behaviour must be kept.

6. Effective GIT_WORK_TREE overrides core.worktree and core.bare

7. Effective core.worktree conflicts with core.bare

8. If GIT_DIR is set but neither worktree nor bare setting is given,
   original cwd becomes worktree.

9. If .git discovery is done inside a repo, the repo becomes a bare
   repo. .git discovery is performed if GIT_DIR is not set.

10. If no worktree is available, cwd remains unchanged, prefix is
    NULL.

11. When user's cwd is outside worktree, cwd remains unchanged,
    prefix is NULL.
"

# This test heavily relies on the standard error of nested function calls.
test_untraceable=UnfortunatelyYes

. ./test-lib.sh

here=$(pwd)

test_repo () {
	(
		cd "$1" &&
		if test -n "$2"
		then
			GIT_DIR="$2" &&
			export GIT_DIR
		fi &&
		if test -n "$3"
		then
			GIT_WORK_TREE="$3" &&
			export GIT_WORK_TREE
		fi &&
		rm -f trace &&
		GIT_TRACE_SETUP="$(pwd)/trace" git symbolic-ref HEAD >/dev/null &&
		grep '^setup: ' trace >result &&
		test_cmp expected result
	)
}

maybe_config () {
	file=$1 var=$2 value=$3 &&
	if test "$value" != unset
	then
		git config --file="$file" "$var" "$value"
	fi
}

setup_repo () {
	name=$1 worktreecfg=$2 gitfile=$3 barecfg=$4 &&
	sane_unset GIT_DIR GIT_WORK_TREE &&

	git init "$name" &&
	maybe_config "$name/.git/config" core.worktree "$worktreecfg" &&
	maybe_config "$name/.git/config" core.bare "$barecfg" &&
	mkdir -p "$name/sub/sub" &&

	if test "${gitfile:+set}"
	then
		mv "$name/.git" "$name.git" &&
		echo "gitdir: ../$name.git" >"$name/.git"
	fi
}

maybe_set () {
	var=$1 value=$2 &&
	if test "$value" != unset
	then
		eval "$var=\$value" &&
		export $var
	fi
}

setup_env () {
	worktreenv=$1 gitdirenv=$2 &&
	sane_unset GIT_DIR GIT_WORK_TREE &&
	maybe_set GIT_DIR "$gitdirenv" &&
	maybe_set GIT_WORK_TREE "$worktreeenv"
}

expect () {
	cat >"$1/expected" <<-EOF
	setup: git_dir: $2
	setup: git_common_dir: $2
	setup: worktree: $3
	setup: cwd: $4
	setup: prefix: $5
	EOF
}

try_case () {
	name=$1 worktreeenv=$2 gitdirenv=$3 &&
	setup_env "$worktreeenv" "$gitdirenv" &&
	expect "$name" "$4" "$5" "$6" "$7" &&
	test_repo "$name"
}

run_wt_tests () {
	N=$1 gitfile=$2

	absgit="$here/$N/.git"
	dotgit=.git
	dotdotgit=../../.git

	if test "$gitfile"
	then
		absgit="$here/$N.git"
		dotgit=$absgit dotdotgit=$absgit
	fi

	test_expect_success "#$N: explicit GIT_WORK_TREE and GIT_DIR at toplevel" '
		try_case $N "$here/$N" .git \
			"$dotgit" "$here/$N" "$here/$N" "(null)" &&
		try_case $N . .git \
			"$dotgit" "$here/$N" "$here/$N" "(null)" &&
		try_case $N "$here/$N" "$here/$N/.git" \
			"$absgit" "$here/$N" "$here/$N" "(null)" &&
		try_case $N . "$here/$N/.git" \
			"$absgit" "$here/$N" "$here/$N" "(null)"
	'

	test_expect_success "#$N: explicit GIT_WORK_TREE and GIT_DIR in subdir" '
		try_case $N/sub/sub "$here/$N" ../../.git \
			"$absgit" "$here/$N" "$here/$N" sub/sub/ &&
		try_case $N/sub/sub ../.. ../../.git \
			"$absgit" "$here/$N" "$here/$N" sub/sub/ &&
		try_case $N/sub/sub "$here/$N" "$here/$N/.git" \
			"$absgit" "$here/$N" "$here/$N" sub/sub/ &&
		try_case $N/sub/sub ../.. "$here/$N/.git" \
			"$absgit" "$here/$N" "$here/$N" sub/sub/
	'

	test_expect_success "#$N: explicit GIT_WORK_TREE from parent of worktree" '
		try_case $N "$here/$N/wt" .git \
			"$dotgit" "$here/$N/wt" "$here/$N" "(null)" &&
		try_case $N wt .git \
			"$dotgit" "$here/$N/wt" "$here/$N" "(null)" &&
		try_case $N wt "$here/$N/.git" \
			"$absgit" "$here/$N/wt" "$here/$N" "(null)" &&
		try_case $N "$here/$N/wt" "$here/$N/.git" \
			"$absgit" "$here/$N/wt" "$here/$N" "(null)"
	'

	test_expect_success "#$N: explicit GIT_WORK_TREE from nephew of worktree" '
		try_case $N/sub/sub "$here/$N/wt" ../../.git \
			"$dotdotgit" "$here/$N/wt" "$here/$N/sub/sub" "(null)" &&
		try_case $N/sub/sub ../../wt ../../.git \
			"$dotdotgit" "$here/$N/wt" "$here/$N/sub/sub" "(null)" &&
		try_case $N/sub/sub ../../wt "$here/$N/.git" \
			"$absgit" "$here/$N/wt" "$here/$N/sub/sub" "(null)" &&
		try_case $N/sub/sub "$here/$N/wt" "$here/$N/.git" \
			"$absgit" "$here/$N/wt" "$here/$N/sub/sub" "(null)"
	'

	test_expect_success "#$N: chdir_to_toplevel uses worktree, not git dir" '
		try_case $N "$here" .git \
			"$absgit" "$here" "$here" $N/ &&
		try_case $N .. .git \
			"$absgit" "$here" "$here" $N/ &&
		try_case $N .. "$here/$N/.git" \
			"$absgit" "$here" "$here" $N/ &&
		try_case $N "$here" "$here/$N/.git" \
			"$absgit" "$here" "$here" $N/
	'

	test_expect_success "#$N: chdir_to_toplevel uses worktree (from subdir)" '
		try_case $N/sub/sub "$here" ../../.git \
			"$absgit" "$here" "$here" $N/sub/sub/ &&
		try_case $N/sub/sub ../../.. ../../.git \
			"$absgit" "$here" "$here" $N/sub/sub/ &&
		try_case $N/sub/sub ../../../ "$here/$N/.git" \
			"$absgit" "$here" "$here" $N/sub/sub/ &&
		try_case $N/sub/sub "$here" "$here/$N/.git" \
			"$absgit" "$here" "$here" $N/sub/sub/
	'
}

# try_repo #c GIT_WORK_TREE GIT_DIR core.worktree .gitfile? core.bare \
#	(git dir) (work tree) (cwd) (prefix) \	<-- at toplevel
#	(git dir) (work tree) (cwd) (prefix)	<-- from subdir
try_repo () {
	name=$1 worktreeenv=$2 gitdirenv=$3 &&
	setup_repo "$name" "$4" "$5" "$6" &&
	shift 6 &&
	try_case "$name" "$worktreeenv" "$gitdirenv" \
		"$1" "$2" "$3" "$4" &&
	shift 4 &&
	case "$gitdirenv" in
	/* | ?:/* | unset) ;;
	*)
		gitdirenv=../$gitdirenv ;;
	esac &&
	try_case "$name/sub" "$worktreeenv" "$gitdirenv" \
		"$1" "$2" "$3" "$4"
}

# Bit 0 = GIT_WORK_TREE
# Bit 1 = GIT_DIR
# Bit 2 = core.worktree
# Bit 3 = .git is a file
# Bit 4 = bare repo
# Case# = encoding of the above 5 bits

test_expect_success '#0: nonbare repo, no explicit configuration' '
	try_repo 0 unset unset unset "" unset \
		.git "$here/0" "$here/0" "(null)" \
		.git "$here/0" "$here/0" sub/ 2>message &&
	! test -s message
'

test_expect_success '#1: GIT_WORK_TREE without explicit GIT_DIR is accepted' '
	try_repo 1 "$here" unset unset "" unset \
		"$here/1/.git" "$here" "$here" 1/ \
		"$here/1/.git" "$here" "$here" 1/sub/ 2>message &&
	! test -s message
'

test_expect_success '#2: worktree defaults to cwd with explicit GIT_DIR' '
	try_repo 2 unset "$here/2/.git" unset "" unset \
		"$here/2/.git" "$here/2" "$here/2" "(null)" \
		"$here/2/.git" "$here/2/sub" "$here/2/sub" "(null)"
'

test_expect_success '#2b: relative GIT_DIR' '
	try_repo 2b unset ".git" unset "" unset \
		".git" "$here/2b" "$here/2b" "(null)" \
		"../.git" "$here/2b/sub" "$here/2b/sub" "(null)"
'

test_expect_success '#3: setup' '
	setup_repo 3 unset "" unset &&
	mkdir -p 3/sub/sub 3/wt/sub
'
run_wt_tests 3

test_expect_success '#4: core.worktree without GIT_DIR set is accepted' '
	setup_repo 4 ../sub "" unset &&
	mkdir -p 4/sub sub &&
	try_case 4 unset unset \
		.git "$here/4/sub" "$here/4" "(null)" \
		"$here/4/.git" "$here/4/sub" "$here/4/sub" "(null)" 2>message &&
	! test -s message
'

test_expect_success '#5: core.worktree + GIT_WORK_TREE is accepted' '
	# or: you cannot intimidate away the lack of GIT_DIR setting
	try_repo 5 "$here" unset "$here/5" "" unset \
		"$here/5/.git" "$here" "$here" 5/ \
		"$here/5/.git" "$here" "$here" 5/sub/ 2>message &&
	try_repo 5a .. unset "$here/5a" "" unset \
		"$here/5a/.git" "$here" "$here" 5a/ \
		"$here/5a/.git" "$here/5a" "$here/5a" sub/ &&
	! test -s message
'

test_expect_success '#6: setting GIT_DIR brings core.worktree to life' '
	setup_repo 6 "$here/6" "" unset &&
	try_case 6 unset .git \
		.git "$here/6" "$here/6" "(null)" &&
	try_case 6 unset "$here/6/.git" \
		"$here/6/.git" "$here/6" "$here/6" "(null)" &&
	try_case 6/sub/sub unset ../../.git \
		"$here/6/.git" "$here/6" "$here/6" sub/sub/ &&
	try_case 6/sub/sub unset "$here/6/.git" \
		"$here/6/.git" "$here/6" "$here/6" sub/sub/
'

test_expect_success '#6b: GIT_DIR set, core.worktree relative' '
	setup_repo 6b .. "" unset &&
	try_case 6b unset .git \
		.git "$here/6b" "$here/6b" "(null)" &&
	try_case 6b unset "$here/6b/.git" \
		"$here/6b/.git" "$here/6b" "$here/6b" "(null)" &&
	try_case 6b/sub/sub unset ../../.git \
		"$here/6b/.git" "$here/6b" "$here/6b" sub/sub/ &&
	try_case 6b/sub/sub unset "$here/6b/.git" \
		"$here/6b/.git" "$here/6b" "$here/6b" sub/sub/
'

test_expect_success '#6c: GIT_DIR set, core.worktree=../wt (absolute)' '
	setup_repo 6c "$here/6c/wt" "" unset &&
	mkdir -p 6c/wt/sub &&

	try_case 6c unset .git \
		.git "$here/6c/wt" "$here/6c" "(null)" &&
	try_case 6c unset "$here/6c/.git" \
		"$here/6c/.git" "$here/6c/wt" "$here/6c" "(null)" &&
	try_case 6c/sub/sub unset ../../.git \
		../../.git "$here/6c/wt" "$here/6c/sub/sub" "(null)" &&
	try_case 6c/sub/sub unset "$here/6c/.git" \
		"$here/6c/.git" "$here/6c/wt" "$here/6c/sub/sub" "(null)"
'

test_expect_success '#6d: GIT_DIR set, core.worktree=../wt (relative)' '
	setup_repo 6d "$here/6d/wt" "" unset &&
	mkdir -p 6d/wt/sub &&

	try_case 6d unset .git \
		.git "$here/6d/wt" "$here/6d" "(null)" &&
	try_case 6d unset "$here/6d/.git" \
		"$here/6d/.git" "$here/6d/wt" "$here/6d" "(null)" &&
	try_case 6d/sub/sub unset ../../.git \
		../../.git "$here/6d/wt" "$here/6d/sub/sub" "(null)" &&
	try_case 6d/sub/sub unset "$here/6d/.git" \
		"$here/6d/.git" "$here/6d/wt" "$here/6d/sub/sub" "(null)"
'

test_expect_success '#6e: GIT_DIR set, core.worktree=../.. (absolute)' '
	setup_repo 6e "$here" "" unset &&
	try_case 6e unset .git \
		"$here/6e/.git" "$here" "$here" 6e/ &&
	try_case 6e unset "$here/6e/.git" \
		"$here/6e/.git" "$here" "$here" 6e/ &&
	try_case 6e/sub/sub unset ../../.git \
		"$here/6e/.git" "$here" "$here" 6e/sub/sub/ &&
	try_case 6e/sub/sub unset "$here/6e/.git" \
		"$here/6e/.git" "$here" "$here" 6e/sub/sub/
'

test_expect_success '#6f: GIT_DIR set, core.worktree=../.. (relative)' '
	setup_repo 6f ../../ "" unset &&
	try_case 6f unset .git \
		"$here/6f/.git" "$here" "$here" 6f/ &&
	try_case 6f unset "$here/6f/.git" \
		"$here/6f/.git" "$here" "$here" 6f/ &&
	try_case 6f/sub/sub unset ../../.git \
		"$here/6f/.git" "$here" "$here" 6f/sub/sub/ &&
	try_case 6f/sub/sub unset "$here/6f/.git" \
		"$here/6f/.git" "$here" "$here" 6f/sub/sub/
'

# case #7: GIT_WORK_TREE overrides core.worktree.
test_expect_success '#7: setup' '
	setup_repo 7 non-existent "" unset &&
	mkdir -p 7/sub/sub 7/wt/sub
'
run_wt_tests 7

test_expect_success '#8: gitfile, easy case' '
	try_repo 8 unset unset unset gitfile unset \
		"$here/8.git" "$here/8" "$here/8" "(null)" \
		"$here/8.git" "$here/8" "$here/8" sub/
'

test_expect_success '#9: GIT_WORK_TREE accepted with gitfile' '
	mkdir -p 9/wt &&
	try_repo 9 wt unset unset gitfile unset \
		"$here/9.git" "$here/9/wt" "$here/9" "(null)" \
		"$here/9.git" "$here/9/sub/wt" "$here/9/sub" "(null)" 2>message &&
	! test -s message
'

test_expect_success '#10: GIT_DIR can point to gitfile' '
	try_repo 10 unset "$here/10/.git" unset gitfile unset \
		"$here/10.git" "$here/10" "$here/10" "(null)" \
		"$here/10.git" "$here/10/sub" "$here/10/sub" "(null)"
'

test_expect_success '#10b: relative GIT_DIR can point to gitfile' '
	try_repo 10b unset .git unset gitfile unset \
		"$here/10b.git" "$here/10b" "$here/10b" "(null)" \
		"$here/10b.git" "$here/10b/sub" "$here/10b/sub" "(null)"
'

# case #11: GIT_WORK_TREE works, gitfile case.
test_expect_success '#11: setup' '
	setup_repo 11 unset gitfile unset &&
	mkdir -p 11/sub/sub 11/wt/sub
'
run_wt_tests 11 gitfile

test_expect_success '#12: core.worktree with gitfile is accepted' '
	try_repo 12 unset unset "$here/12" gitfile unset \
		"$here/12.git" "$here/12" "$here/12" "(null)" \
		"$here/12.git" "$here/12" "$here/12" sub/ 2>message &&
	! test -s message
'

test_expect_success '#13: core.worktree+GIT_WORK_TREE accepted (with gitfile)' '
	# or: you cannot intimidate away the lack of GIT_DIR setting
	try_repo 13 non-existent-too unset non-existent gitfile unset \
		"$here/13.git" "$here/13/non-existent-too" "$here/13" "(null)" \
		"$here/13.git" "$here/13/sub/non-existent-too" "$here/13/sub" "(null)" 2>message &&
	! test -s message
'

# case #14.
# If this were more table-driven, it could share code with case #6.

test_expect_success '#14: core.worktree with GIT_DIR pointing to gitfile' '
	setup_repo 14 "$here/14" gitfile unset &&
	try_case 14 unset .git \
		"$here/14.git" "$here/14" "$here/14" "(null)" &&
	try_case 14 unset "$here/14/.git" \
		"$here/14.git" "$here/14" "$here/14" "(null)" &&
	try_case 14/sub/sub unset ../../.git \
		"$here/14.git" "$here/14" "$here/14" sub/sub/ &&
	try_case 14/sub/sub unset "$here/14/.git" \
		"$here/14.git" "$here/14" "$here/14" sub/sub/ &&

	setup_repo 14c "$here/14c/wt" gitfile unset &&
	mkdir -p 14c/wt/sub &&

	try_case 14c unset .git \
		"$here/14c.git" "$here/14c/wt" "$here/14c" "(null)" &&
	try_case 14c unset "$here/14c/.git" \
		"$here/14c.git" "$here/14c/wt" "$here/14c" "(null)" &&
	try_case 14c/sub/sub unset ../../.git \
		"$here/14c.git" "$here/14c/wt" "$here/14c/sub/sub" "(null)" &&
	try_case 14c/sub/sub unset "$here/14c/.git" \
		"$here/14c.git" "$here/14c/wt" "$here/14c/sub/sub" "(null)" &&

	setup_repo 14d "$here/14d/wt" gitfile unset &&
	mkdir -p 14d/wt/sub &&

	try_case 14d unset .git \
		"$here/14d.git" "$here/14d/wt" "$here/14d" "(null)" &&
	try_case 14d unset "$here/14d/.git" \
		"$here/14d.git" "$here/14d/wt" "$here/14d" "(null)" &&
	try_case 14d/sub/sub unset ../../.git \
		"$here/14d.git" "$here/14d/wt" "$here/14d/sub/sub" "(null)" &&
	try_case 14d/sub/sub unset "$here/14d/.git" \
		"$here/14d.git" "$here/14d/wt" "$here/14d/sub/sub" "(null)" &&

	setup_repo 14e "$here" gitfile unset &&
	try_case 14e unset .git \
		"$here/14e.git" "$here" "$here" 14e/ &&
	try_case 14e unset "$here/14e/.git" \
		"$here/14e.git" "$here" "$here" 14e/ &&
	try_case 14e/sub/sub unset ../../.git \
		"$here/14e.git" "$here" "$here" 14e/sub/sub/ &&
	try_case 14e/sub/sub unset "$here/14e/.git" \
		"$here/14e.git" "$here" "$here" 14e/sub/sub/
'

test_expect_success '#14b: core.worktree is relative to actual git dir' '
	setup_repo 14b ../14b gitfile unset &&
	try_case 14b unset .git \
		"$here/14b.git" "$here/14b" "$here/14b" "(null)" &&
	try_case 14b unset "$here/14b/.git" \
		"$here/14b.git" "$here/14b" "$here/14b" "(null)" &&
	try_case 14b/sub/sub unset ../../.git \
		"$here/14b.git" "$here/14b" "$here/14b" sub/sub/ &&
	try_case 14b/sub/sub unset "$here/14b/.git" \
		"$here/14b.git" "$here/14b" "$here/14b" sub/sub/ &&

	setup_repo 14f ../ gitfile unset &&
	try_case 14f unset .git \
		"$here/14f.git" "$here" "$here" 14f/ &&
	try_case 14f unset "$here/14f/.git" \
		"$here/14f.git" "$here" "$here" 14f/ &&
	try_case 14f/sub/sub unset ../../.git \
		"$here/14f.git" "$here" "$here" 14f/sub/sub/ &&
	try_case 14f/sub/sub unset "$here/14f/.git" \
		"$here/14f.git" "$here" "$here" 14f/sub/sub/
'

# case #15: GIT_WORK_TREE overrides core.worktree (gitfile case).
test_expect_success '#15: setup' '
	setup_repo 15 non-existent gitfile unset &&
	mkdir -p 15/sub/sub 15/wt/sub
'
run_wt_tests 15 gitfile

test_expect_success '#16a: implicitly bare repo (cwd inside .git dir)' '
	setup_repo 16a unset "" unset &&
	mkdir -p 16a/.git/wt/sub &&

	try_case 16a/.git unset unset \
		. "(null)" "$here/16a/.git" "(null)" &&
	try_case 16a/.git/wt unset unset \
		"$here/16a/.git" "(null)" "$here/16a/.git/wt" "(null)" &&
	try_case 16a/.git/wt/sub unset unset \
		"$here/16a/.git" "(null)" "$here/16a/.git/wt/sub" "(null)"
'

test_expect_success '#16b: bare .git (cwd inside .git dir)' '
	setup_repo 16b unset "" true &&
	mkdir -p 16b/.git/wt/sub &&

	try_case 16b/.git unset unset \
		. "(null)" "$here/16b/.git" "(null)" &&
	try_case 16b/.git/wt unset unset \
		"$here/16b/.git" "(null)" "$here/16b/.git/wt" "(null)" &&
	try_case 16b/.git/wt/sub unset unset \
		"$here/16b/.git" "(null)" "$here/16b/.git/wt/sub" "(null)"
'

test_expect_success '#16c: bare .git has no worktree' '
	try_repo 16c unset unset unset "" true \
		.git "(null)" "$here/16c" "(null)" \
		"$here/16c/.git" "(null)" "$here/16c/sub" "(null)"
'

test_expect_success '#16d: bareness preserved across alias' '
	setup_repo 16d unset "" unset &&
	(
		cd 16d/.git &&
		test_must_fail git status &&
		git config alias.st status &&
		test_must_fail git st
	)
'

test_expect_success '#16e: bareness preserved by --bare' '
	setup_repo 16e unset "" unset &&
	(
		cd 16e/.git &&
		test_must_fail git status &&
		test_must_fail git --bare status
	)
'

test_expect_success '#17: GIT_WORK_TREE without explicit GIT_DIR is accepted (bare case)' '
	# Just like #16.
	setup_repo 17a unset "" true &&
	setup_repo 17b unset "" true &&
	mkdir -p 17a/.git/wt/sub &&
	mkdir -p 17b/.git/wt/sub &&

	try_case 17a/.git "$here/17a" unset \
		"$here/17a/.git" "$here/17a" "$here/17a" .git/ \
		2>message &&
	try_case 17a/.git/wt "$here/17a" unset \
		"$here/17a/.git" "$here/17a" "$here/17a" .git/wt/ &&
	try_case 17a/.git/wt/sub "$here/17a" unset \
		"$here/17a/.git" "$here/17a" "$here/17a" .git/wt/sub/ &&

	try_case 17b/.git "$here/17b" unset \
		"$here/17b/.git" "$here/17b" "$here/17b" .git/ &&
	try_case 17b/.git/wt "$here/17b" unset \
		"$here/17b/.git" "$here/17b" "$here/17b" .git/wt/ &&
	try_case 17b/.git/wt/sub "$here/17b" unset \
		"$here/17b/.git" "$here/17b" "$here/17b" .git/wt/sub/ &&

	try_repo 17c "$here/17c" unset unset "" true \
		.git "$here/17c" "$here/17c" "(null)" \
		"$here/17c/.git" "$here/17c" "$here/17c" sub/ 2>message &&
	! test -s message
'

test_expect_success '#18: bare .git named by GIT_DIR has no worktree' '
	try_repo 18 unset .git unset "" true \
		.git "(null)" "$here/18" "(null)" \
		../.git "(null)" "$here/18/sub" "(null)" &&
	try_repo 18b unset "$here/18b/.git" unset "" true \
		"$here/18b/.git" "(null)" "$here/18b" "(null)" \
		"$here/18b/.git" "(null)" "$here/18b/sub" "(null)"
'

# Case #19: GIT_DIR + GIT_WORK_TREE suppresses bareness.
test_expect_success '#19: setup' '
	setup_repo 19 unset "" true &&
	mkdir -p 19/sub/sub 19/wt/sub
'
run_wt_tests 19

test_expect_success '#20a: core.worktree without GIT_DIR accepted (inside .git)' '
	# Unlike case #16a.
	setup_repo 20a "$here/20a" "" unset &&
	mkdir -p 20a/.git/wt/sub &&
	try_case 20a/.git unset unset \
		"$here/20a/.git" "$here/20a" "$here/20a" .git/ 2>message &&
	try_case 20a/.git/wt unset unset \
		"$here/20a/.git" "$here/20a" "$here/20a" .git/wt/ &&
	try_case 20a/.git/wt/sub unset unset \
		"$here/20a/.git" "$here/20a" "$here/20a" .git/wt/sub/ &&
	! test -s message
'

test_expect_success '#20b/c: core.worktree and core.bare conflict' '
	setup_repo 20b non-existent "" true &&
	mkdir -p 20b/.git/wt/sub &&
	(
		cd 20b/.git &&
		test_must_fail git status >/dev/null
	) 2>message &&
	grep "core.bare and core.worktree" message
'

test_expect_success '#20d: core.worktree and core.bare OK when working tree not needed' '
	setup_repo 20d non-existent "" true &&
	mkdir -p 20d/.git/wt/sub &&
	(
		cd 20d/.git &&
		git config foo.bar value
	)
'

# Case #21: core.worktree/GIT_WORK_TREE overrides core.bare' '
test_expect_success '#21: setup, core.worktree warns before overriding core.bare' '
	setup_repo 21 non-existent "" unset &&
	mkdir -p 21/.git/wt/sub &&
	(
		cd 21/.git &&
		GIT_WORK_TREE="$here/21" &&
		export GIT_WORK_TREE &&
		git status >/dev/null
	) 2>message &&
	! test -s message

'
run_wt_tests 21

test_expect_success '#22a: core.worktree = GIT_DIR = .git dir' '
	# like case #6.

	setup_repo 22a "$here/22a/.git" "" unset &&
	setup_repo 22ab . "" unset &&
	mkdir -p 22a/.git/sub 22a/sub &&
	mkdir -p 22ab/.git/sub 22ab/sub &&
	try_case 22a/.git unset . \
		. "$here/22a/.git" "$here/22a/.git" "(null)" &&
	try_case 22a/.git unset "$here/22a/.git" \
		"$here/22a/.git" "$here/22a/.git" "$here/22a/.git" "(null)" &&
	try_case 22a/.git/sub unset .. \
		"$here/22a/.git" "$here/22a/.git" "$here/22a/.git" sub/ &&
	try_case 22a/.git/sub unset "$here/22a/.git" \
		"$here/22a/.git" "$here/22a/.git" "$here/22a/.git" sub/ &&

	try_case 22ab/.git unset . \
		. "$here/22ab/.git" "$here/22ab/.git" "(null)" &&
	try_case 22ab/.git unset "$here/22ab/.git" \
		"$here/22ab/.git" "$here/22ab/.git" "$here/22ab/.git" "(null)" &&
	try_case 22ab/.git/sub unset .. \
		"$here/22ab/.git" "$here/22ab/.git" "$here/22ab/.git" sub/ &&
	try_case 22ab/.git unset "$here/22ab/.git" \
		"$here/22ab/.git" "$here/22ab/.git" "$here/22ab/.git" "(null)"
'

test_expect_success '#22b: core.worktree child of .git, GIT_DIR=.git' '
	setup_repo 22b "$here/22b/.git/wt" "" unset &&
	setup_repo 22bb wt "" unset &&
	mkdir -p 22b/.git/sub 22b/sub 22b/.git/wt/sub 22b/wt/sub &&
	mkdir -p 22bb/.git/sub 22bb/sub 22bb/.git/wt 22bb/wt &&

	try_case 22b/.git unset . \
		. "$here/22b/.git/wt" "$here/22b/.git" "(null)" &&
	try_case 22b/.git unset "$here/22b/.git" \
		"$here/22b/.git" "$here/22b/.git/wt" "$here/22b/.git" "(null)" &&
	try_case 22b/.git/sub unset .. \
		.. "$here/22b/.git/wt" "$here/22b/.git/sub" "(null)" &&
	try_case 22b/.git/sub unset "$here/22b/.git" \
		"$here/22b/.git" "$here/22b/.git/wt" "$here/22b/.git/sub" "(null)" &&

	try_case 22bb/.git unset . \
		. "$here/22bb/.git/wt" "$here/22bb/.git" "(null)" &&
	try_case 22bb/.git unset "$here/22bb/.git" \
		"$here/22bb/.git" "$here/22bb/.git/wt" "$here/22bb/.git" "(null)" &&
	try_case 22bb/.git/sub unset .. \
		.. "$here/22bb/.git/wt" "$here/22bb/.git/sub" "(null)" &&
	try_case 22bb/.git/sub unset "$here/22bb/.git" \
		"$here/22bb/.git" "$here/22bb/.git/wt" "$here/22bb/.git/sub" "(null)"
'

test_expect_success '#22c: core.worktree = .git/.., GIT_DIR=.git' '
	setup_repo 22c "$here/22c" "" unset &&
	setup_repo 22cb .. "" unset &&
	mkdir -p 22c/.git/sub 22c/sub &&
	mkdir -p 22cb/.git/sub 22cb/sub &&

	try_case 22c/.git unset . \
		"$here/22c/.git" "$here/22c" "$here/22c" .git/ &&
	try_case 22c/.git unset "$here/22c/.git" \
		"$here/22c/.git" "$here/22c" "$here/22c" .git/ &&
	try_case 22c/.git/sub unset .. \
		"$here/22c/.git" "$here/22c" "$here/22c" .git/sub/ &&
	try_case 22c/.git/sub unset "$here/22c/.git" \
		"$here/22c/.git" "$here/22c" "$here/22c" .git/sub/ &&

	try_case 22cb/.git unset . \
		"$here/22cb/.git" "$here/22cb" "$here/22cb" .git/ &&
	try_case 22cb/.git unset "$here/22cb/.git" \
		"$here/22cb/.git" "$here/22cb" "$here/22cb" .git/ &&
	try_case 22cb/.git/sub unset .. \
		"$here/22cb/.git" "$here/22cb" "$here/22cb" .git/sub/ &&
	try_case 22cb/.git/sub unset "$here/22cb/.git" \
		"$here/22cb/.git" "$here/22cb" "$here/22cb" .git/sub/
'

test_expect_success '#22.2: core.worktree and core.bare conflict' '
	setup_repo 22 "$here/22" "" true &&
	(
		cd 22/.git &&
		GIT_DIR=. &&
		export GIT_DIR &&
		test_must_fail git status 2>result
	) &&
	(
		cd 22 &&
		GIT_DIR=.git &&
		export GIT_DIR &&
		test_must_fail git status 2>result
	) &&
	grep "core.bare and core.worktree" 22/.git/result &&
	grep "core.bare and core.worktree" 22/result
'

# Case #23: GIT_DIR + GIT_WORK_TREE(+core.worktree) suppresses bareness.
test_expect_success '#23: setup' '
	setup_repo 23 non-existent "" true &&
	mkdir -p 23/sub/sub 23/wt/sub
'
run_wt_tests 23

test_expect_success '#24: bare repo has no worktree (gitfile case)' '
	try_repo 24 unset unset unset gitfile true \
		"$here/24.git" "(null)" "$here/24" "(null)" \
		"$here/24.git" "(null)" "$here/24/sub" "(null)"
'

test_expect_success '#25: GIT_WORK_TREE accepted if GIT_DIR unset (bare gitfile case)' '
	try_repo 25 "$here/25" unset unset gitfile true \
		"$here/25.git" "$here/25" "$here/25" "(null)"  \
		"$here/25.git" "$here/25" "$here/25" "sub/" 2>message &&
	! test -s message
'

test_expect_success '#26: bare repo has no worktree (GIT_DIR -> gitfile case)' '
	try_repo 26 unset "$here/26/.git" unset gitfile true \
		"$here/26.git" "(null)" "$here/26" "(null)" \
		"$here/26.git" "(null)" "$here/26/sub" "(null)" &&
	try_repo 26b unset .git unset gitfile true \
		"$here/26b.git" "(null)" "$here/26b" "(null)" \
		"$here/26b.git" "(null)" "$here/26b/sub" "(null)"
'

# Case #27: GIT_DIR + GIT_WORK_TREE suppresses bareness (with gitfile).
test_expect_success '#27: setup' '
	setup_repo 27 unset gitfile true &&
	mkdir -p 27/sub/sub 27/wt/sub
'
run_wt_tests 27 gitfile

test_expect_success '#28: core.worktree and core.bare conflict (gitfile case)' '
	setup_repo 28 "$here/28" gitfile true &&
	(
		cd 28 &&
		test_must_fail git status
	) 2>message &&
	grep "core.bare and core.worktree" message
'

# Case #29: GIT_WORK_TREE(+core.worktree) overrides core.bare (gitfile case).
test_expect_success '#29: setup' '
	setup_repo 29 non-existent gitfile true &&
	mkdir -p 29/sub/sub 29/wt/sub &&
	(
		cd 29 &&
		GIT_WORK_TREE="$here/29" &&
		export GIT_WORK_TREE &&
		git status
	) 2>message &&
	! test -s message
'
run_wt_tests 29 gitfile

test_expect_success '#30: core.worktree and core.bare conflict (gitfile version)' '
	# Just like case #22.
	setup_repo 30 "$here/30" gitfile true &&
	(
		cd 30 &&
		test_must_fail env GIT_DIR=.git git status 2>result
	) &&
	grep "core.bare and core.worktree" 30/result
'

# Case #31: GIT_DIR + GIT_WORK_TREE(+core.worktree) suppresses
# bareness (gitfile version).
test_expect_success '#31: setup' '
	setup_repo 31 non-existent gitfile true &&
	mkdir -p 31/sub/sub 31/wt/sub
'
run_wt_tests 31 gitfile

test_done
