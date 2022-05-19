#!/bin/sh

test_description="Tests of cwd/prefix/worktree/butdir setup in all cases

A few rules for repo setup:

1. BUT_DIR is relative to user's cwd. --but-dir is equivalent to
   BUT_DIR.

2. .but file is relative to parent directory. .but file is basically
   symlink in disguise. The directory where .but file points to will
   become new but_dir.

3. core.worktree is relative to but_dir.

4. BUT_WORK_TREE is relative to user's cwd. --work-tree is
   equivalent to BUT_WORK_TREE.

5. BUT_WORK_TREE/core.worktree was originally meant to work only if
   BUT_DIR is set, but earlier but didn't enforce it, and some scripts
   depend on the implementation that happened to first discover .but by
   going up from the users $cwd and then using the specified working tree
   that may or may not have any relation to where .but was found in.  This
   historical behaviour must be kept.

6. Effective BUT_WORK_TREE overrides core.worktree and core.bare

7. Effective core.worktree conflicts with core.bare

8. If BUT_DIR is set but neither worktree nor bare setting is given,
   original cwd becomes worktree.

9. If .but discovery is done inside a repo, the repo becomes a bare
   repo. .but discovery is performed if BUT_DIR is not set.

10. If no worktree is available, cwd remains unchanged, prefix is
    NULL.

11. When user's cwd is outside worktree, cwd remains unchanged,
    prefix is NULL.
"

# This test heavily relies on the standard error of nested function calls.
test_untraceable=UnfortunatelyYes

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

here=$(pwd)

test_repo () {
	(
		cd "$1" &&
		if test -n "$2"
		then
			BUT_DIR="$2" &&
			export BUT_DIR
		fi &&
		if test -n "$3"
		then
			BUT_WORK_TREE="$3" &&
			export BUT_WORK_TREE
		fi &&
		rm -f trace &&
		BUT_TRACE_SETUP="$(pwd)/trace" but symbolic-ref HEAD >/dev/null &&
		grep '^setup: ' trace >result &&
		test_cmp expected result
	)
}

maybe_config () {
	file=$1 var=$2 value=$3 &&
	if test "$value" != unset
	then
		but config --file="$file" "$var" "$value"
	fi
}

setup_repo () {
	name=$1 worktreecfg=$2 butfile=$3 barecfg=$4 &&
	sane_unset BUT_DIR BUT_WORK_TREE &&

	but -c init.defaultBranch=initial init "$name" &&
	maybe_config "$name/.but/config" core.worktree "$worktreecfg" &&
	maybe_config "$name/.but/config" core.bare "$barecfg" &&
	mkdir -p "$name/sub/sub" &&

	if test "${butfile:+set}"
	then
		mv "$name/.but" "$name.but" &&
		echo "butdir: ../$name.but" >"$name/.but"
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
	worktreenv=$1 butdirenv=$2 &&
	sane_unset BUT_DIR BUT_WORK_TREE &&
	maybe_set BUT_DIR "$butdirenv" &&
	maybe_set BUT_WORK_TREE "$worktreeenv"
}

expect () {
	cat >"$1/expected" <<-EOF
	setup: but_dir: $2
	setup: but_common_dir: $2
	setup: worktree: $3
	setup: cwd: $4
	setup: prefix: $5
	EOF
}

try_case () {
	name=$1 worktreeenv=$2 butdirenv=$3 &&
	setup_env "$worktreeenv" "$butdirenv" &&
	expect "$name" "$4" "$5" "$6" "$7" &&
	test_repo "$name"
}

run_wt_tests () {
	N=$1 butfile=$2

	absbut="$here/$N/.but"
	dotbut=.but
	dotdotbut=../../.but

	if test "$butfile"
	then
		absbut="$here/$N.but"
		dotbut=$absbut dotdotbut=$absbut
	fi

	test_expect_success "#$N: explicit BUT_WORK_TREE and BUT_DIR at toplevel" '
		try_case $N "$here/$N" .but \
			"$dotbut" "$here/$N" "$here/$N" "(null)" &&
		try_case $N . .but \
			"$dotbut" "$here/$N" "$here/$N" "(null)" &&
		try_case $N "$here/$N" "$here/$N/.but" \
			"$absbut" "$here/$N" "$here/$N" "(null)" &&
		try_case $N . "$here/$N/.but" \
			"$absbut" "$here/$N" "$here/$N" "(null)"
	'

	test_expect_success "#$N: explicit BUT_WORK_TREE and BUT_DIR in subdir" '
		try_case $N/sub/sub "$here/$N" ../../.but \
			"$absbut" "$here/$N" "$here/$N" sub/sub/ &&
		try_case $N/sub/sub ../.. ../../.but \
			"$absbut" "$here/$N" "$here/$N" sub/sub/ &&
		try_case $N/sub/sub "$here/$N" "$here/$N/.but" \
			"$absbut" "$here/$N" "$here/$N" sub/sub/ &&
		try_case $N/sub/sub ../.. "$here/$N/.but" \
			"$absbut" "$here/$N" "$here/$N" sub/sub/
	'

	test_expect_success "#$N: explicit BUT_WORK_TREE from parent of worktree" '
		try_case $N "$here/$N/wt" .but \
			"$dotbut" "$here/$N/wt" "$here/$N" "(null)" &&
		try_case $N wt .but \
			"$dotbut" "$here/$N/wt" "$here/$N" "(null)" &&
		try_case $N wt "$here/$N/.but" \
			"$absbut" "$here/$N/wt" "$here/$N" "(null)" &&
		try_case $N "$here/$N/wt" "$here/$N/.but" \
			"$absbut" "$here/$N/wt" "$here/$N" "(null)"
	'

	test_expect_success "#$N: explicit BUT_WORK_TREE from nephew of worktree" '
		try_case $N/sub/sub "$here/$N/wt" ../../.but \
			"$dotdotbut" "$here/$N/wt" "$here/$N/sub/sub" "(null)" &&
		try_case $N/sub/sub ../../wt ../../.but \
			"$dotdotbut" "$here/$N/wt" "$here/$N/sub/sub" "(null)" &&
		try_case $N/sub/sub ../../wt "$here/$N/.but" \
			"$absbut" "$here/$N/wt" "$here/$N/sub/sub" "(null)" &&
		try_case $N/sub/sub "$here/$N/wt" "$here/$N/.but" \
			"$absbut" "$here/$N/wt" "$here/$N/sub/sub" "(null)"
	'

	test_expect_success "#$N: chdir_to_toplevel uses worktree, not but dir" '
		try_case $N "$here" .but \
			"$absbut" "$here" "$here" $N/ &&
		try_case $N .. .but \
			"$absbut" "$here" "$here" $N/ &&
		try_case $N .. "$here/$N/.but" \
			"$absbut" "$here" "$here" $N/ &&
		try_case $N "$here" "$here/$N/.but" \
			"$absbut" "$here" "$here" $N/
	'

	test_expect_success "#$N: chdir_to_toplevel uses worktree (from subdir)" '
		try_case $N/sub/sub "$here" ../../.but \
			"$absbut" "$here" "$here" $N/sub/sub/ &&
		try_case $N/sub/sub ../../.. ../../.but \
			"$absbut" "$here" "$here" $N/sub/sub/ &&
		try_case $N/sub/sub ../../../ "$here/$N/.but" \
			"$absbut" "$here" "$here" $N/sub/sub/ &&
		try_case $N/sub/sub "$here" "$here/$N/.but" \
			"$absbut" "$here" "$here" $N/sub/sub/
	'
}

# try_repo #c BUT_WORK_TREE BUT_DIR core.worktree .butfile? core.bare \
#	(but dir) (work tree) (cwd) (prefix) \	<-- at toplevel
#	(but dir) (work tree) (cwd) (prefix)	<-- from subdir
try_repo () {
	name=$1 worktreeenv=$2 butdirenv=$3 &&
	setup_repo "$name" "$4" "$5" "$6" &&
	shift 6 &&
	try_case "$name" "$worktreeenv" "$butdirenv" \
		"$1" "$2" "$3" "$4" &&
	shift 4 &&
	case "$butdirenv" in
	/* | ?:/* | unset) ;;
	*)
		butdirenv=../$butdirenv ;;
	esac &&
	try_case "$name/sub" "$worktreeenv" "$butdirenv" \
		"$1" "$2" "$3" "$4"
}

# Bit 0 = BUT_WORK_TREE
# Bit 1 = BUT_DIR
# Bit 2 = core.worktree
# Bit 3 = .but is a file
# Bit 4 = bare repo
# Case# = encoding of the above 5 bits

test_expect_success '#0: nonbare repo, no explicit configuration' '
	try_repo 0 unset unset unset "" unset \
		.but "$here/0" "$here/0" "(null)" \
		.but "$here/0" "$here/0" sub/ 2>message &&
	test_must_be_empty message
'

test_expect_success '#1: BUT_WORK_TREE without explicit BUT_DIR is accepted' '
	try_repo 1 "$here" unset unset "" unset \
		"$here/1/.but" "$here" "$here" 1/ \
		"$here/1/.but" "$here" "$here" 1/sub/ 2>message &&
	test_must_be_empty message
'

test_expect_success '#2: worktree defaults to cwd with explicit BUT_DIR' '
	try_repo 2 unset "$here/2/.but" unset "" unset \
		"$here/2/.but" "$here/2" "$here/2" "(null)" \
		"$here/2/.but" "$here/2/sub" "$here/2/sub" "(null)"
'

test_expect_success '#2b: relative BUT_DIR' '
	try_repo 2b unset ".but" unset "" unset \
		".but" "$here/2b" "$here/2b" "(null)" \
		"../.but" "$here/2b/sub" "$here/2b/sub" "(null)"
'

test_expect_success '#3: setup' '
	setup_repo 3 unset "" unset &&
	mkdir -p 3/sub/sub 3/wt/sub
'
run_wt_tests 3

test_expect_success '#4: core.worktree without BUT_DIR set is accepted' '
	setup_repo 4 ../sub "" unset &&
	mkdir -p 4/sub sub &&
	try_case 4 unset unset \
		.but "$here/4/sub" "$here/4" "(null)" \
		"$here/4/.but" "$here/4/sub" "$here/4/sub" "(null)" 2>message &&
	test_must_be_empty message
'

test_expect_success '#5: core.worktree + BUT_WORK_TREE is accepted' '
	# or: you cannot intimidate away the lack of BUT_DIR setting
	try_repo 5 "$here" unset "$here/5" "" unset \
		"$here/5/.but" "$here" "$here" 5/ \
		"$here/5/.but" "$here" "$here" 5/sub/ 2>message &&
	try_repo 5a .. unset "$here/5a" "" unset \
		"$here/5a/.but" "$here" "$here" 5a/ \
		"$here/5a/.but" "$here/5a" "$here/5a" sub/ &&
	test_must_be_empty message
'

test_expect_success '#6: setting BUT_DIR brings core.worktree to life' '
	setup_repo 6 "$here/6" "" unset &&
	try_case 6 unset .but \
		.but "$here/6" "$here/6" "(null)" &&
	try_case 6 unset "$here/6/.but" \
		"$here/6/.but" "$here/6" "$here/6" "(null)" &&
	try_case 6/sub/sub unset ../../.but \
		"$here/6/.but" "$here/6" "$here/6" sub/sub/ &&
	try_case 6/sub/sub unset "$here/6/.but" \
		"$here/6/.but" "$here/6" "$here/6" sub/sub/
'

test_expect_success '#6b: BUT_DIR set, core.worktree relative' '
	setup_repo 6b .. "" unset &&
	try_case 6b unset .but \
		.but "$here/6b" "$here/6b" "(null)" &&
	try_case 6b unset "$here/6b/.but" \
		"$here/6b/.but" "$here/6b" "$here/6b" "(null)" &&
	try_case 6b/sub/sub unset ../../.but \
		"$here/6b/.but" "$here/6b" "$here/6b" sub/sub/ &&
	try_case 6b/sub/sub unset "$here/6b/.but" \
		"$here/6b/.but" "$here/6b" "$here/6b" sub/sub/
'

test_expect_success '#6c: BUT_DIR set, core.worktree=../wt (absolute)' '
	setup_repo 6c "$here/6c/wt" "" unset &&
	mkdir -p 6c/wt/sub &&

	try_case 6c unset .but \
		.but "$here/6c/wt" "$here/6c" "(null)" &&
	try_case 6c unset "$here/6c/.but" \
		"$here/6c/.but" "$here/6c/wt" "$here/6c" "(null)" &&
	try_case 6c/sub/sub unset ../../.but \
		../../.but "$here/6c/wt" "$here/6c/sub/sub" "(null)" &&
	try_case 6c/sub/sub unset "$here/6c/.but" \
		"$here/6c/.but" "$here/6c/wt" "$here/6c/sub/sub" "(null)"
'

test_expect_success '#6d: BUT_DIR set, core.worktree=../wt (relative)' '
	setup_repo 6d "$here/6d/wt" "" unset &&
	mkdir -p 6d/wt/sub &&

	try_case 6d unset .but \
		.but "$here/6d/wt" "$here/6d" "(null)" &&
	try_case 6d unset "$here/6d/.but" \
		"$here/6d/.but" "$here/6d/wt" "$here/6d" "(null)" &&
	try_case 6d/sub/sub unset ../../.but \
		../../.but "$here/6d/wt" "$here/6d/sub/sub" "(null)" &&
	try_case 6d/sub/sub unset "$here/6d/.but" \
		"$here/6d/.but" "$here/6d/wt" "$here/6d/sub/sub" "(null)"
'

test_expect_success '#6e: BUT_DIR set, core.worktree=../.. (absolute)' '
	setup_repo 6e "$here" "" unset &&
	try_case 6e unset .but \
		"$here/6e/.but" "$here" "$here" 6e/ &&
	try_case 6e unset "$here/6e/.but" \
		"$here/6e/.but" "$here" "$here" 6e/ &&
	try_case 6e/sub/sub unset ../../.but \
		"$here/6e/.but" "$here" "$here" 6e/sub/sub/ &&
	try_case 6e/sub/sub unset "$here/6e/.but" \
		"$here/6e/.but" "$here" "$here" 6e/sub/sub/
'

test_expect_success '#6f: BUT_DIR set, core.worktree=../.. (relative)' '
	setup_repo 6f ../../ "" unset &&
	try_case 6f unset .but \
		"$here/6f/.but" "$here" "$here" 6f/ &&
	try_case 6f unset "$here/6f/.but" \
		"$here/6f/.but" "$here" "$here" 6f/ &&
	try_case 6f/sub/sub unset ../../.but \
		"$here/6f/.but" "$here" "$here" 6f/sub/sub/ &&
	try_case 6f/sub/sub unset "$here/6f/.but" \
		"$here/6f/.but" "$here" "$here" 6f/sub/sub/
'

# case #7: BUT_WORK_TREE overrides core.worktree.
test_expect_success '#7: setup' '
	setup_repo 7 non-existent "" unset &&
	mkdir -p 7/sub/sub 7/wt/sub
'
run_wt_tests 7

test_expect_success '#8: butfile, easy case' '
	try_repo 8 unset unset unset butfile unset \
		"$here/8.but" "$here/8" "$here/8" "(null)" \
		"$here/8.but" "$here/8" "$here/8" sub/
'

test_expect_success '#9: BUT_WORK_TREE accepted with butfile' '
	mkdir -p 9/wt &&
	try_repo 9 wt unset unset butfile unset \
		"$here/9.but" "$here/9/wt" "$here/9" "(null)" \
		"$here/9.but" "$here/9/sub/wt" "$here/9/sub" "(null)" 2>message &&
	test_must_be_empty message
'

test_expect_success '#10: BUT_DIR can point to butfile' '
	try_repo 10 unset "$here/10/.but" unset butfile unset \
		"$here/10.but" "$here/10" "$here/10" "(null)" \
		"$here/10.but" "$here/10/sub" "$here/10/sub" "(null)"
'

test_expect_success '#10b: relative BUT_DIR can point to butfile' '
	try_repo 10b unset .but unset butfile unset \
		"$here/10b.but" "$here/10b" "$here/10b" "(null)" \
		"$here/10b.but" "$here/10b/sub" "$here/10b/sub" "(null)"
'

# case #11: BUT_WORK_TREE works, butfile case.
test_expect_success '#11: setup' '
	setup_repo 11 unset butfile unset &&
	mkdir -p 11/sub/sub 11/wt/sub
'
run_wt_tests 11 butfile

test_expect_success '#12: core.worktree with butfile is accepted' '
	try_repo 12 unset unset "$here/12" butfile unset \
		"$here/12.but" "$here/12" "$here/12" "(null)" \
		"$here/12.but" "$here/12" "$here/12" sub/ 2>message &&
	test_must_be_empty message
'

test_expect_success '#13: core.worktree+BUT_WORK_TREE accepted (with butfile)' '
	# or: you cannot intimidate away the lack of BUT_DIR setting
	try_repo 13 non-existent-too unset non-existent butfile unset \
		"$here/13.but" "$here/13/non-existent-too" "$here/13" "(null)" \
		"$here/13.but" "$here/13/sub/non-existent-too" "$here/13/sub" "(null)" 2>message &&
	test_must_be_empty message
'

# case #14.
# If this were more table-driven, it could share code with case #6.

test_expect_success '#14: core.worktree with BUT_DIR pointing to butfile' '
	setup_repo 14 "$here/14" butfile unset &&
	try_case 14 unset .but \
		"$here/14.but" "$here/14" "$here/14" "(null)" &&
	try_case 14 unset "$here/14/.but" \
		"$here/14.but" "$here/14" "$here/14" "(null)" &&
	try_case 14/sub/sub unset ../../.but \
		"$here/14.but" "$here/14" "$here/14" sub/sub/ &&
	try_case 14/sub/sub unset "$here/14/.but" \
		"$here/14.but" "$here/14" "$here/14" sub/sub/ &&

	setup_repo 14c "$here/14c/wt" butfile unset &&
	mkdir -p 14c/wt/sub &&

	try_case 14c unset .but \
		"$here/14c.but" "$here/14c/wt" "$here/14c" "(null)" &&
	try_case 14c unset "$here/14c/.but" \
		"$here/14c.but" "$here/14c/wt" "$here/14c" "(null)" &&
	try_case 14c/sub/sub unset ../../.but \
		"$here/14c.but" "$here/14c/wt" "$here/14c/sub/sub" "(null)" &&
	try_case 14c/sub/sub unset "$here/14c/.but" \
		"$here/14c.but" "$here/14c/wt" "$here/14c/sub/sub" "(null)" &&

	setup_repo 14d "$here/14d/wt" butfile unset &&
	mkdir -p 14d/wt/sub &&

	try_case 14d unset .but \
		"$here/14d.but" "$here/14d/wt" "$here/14d" "(null)" &&
	try_case 14d unset "$here/14d/.but" \
		"$here/14d.but" "$here/14d/wt" "$here/14d" "(null)" &&
	try_case 14d/sub/sub unset ../../.but \
		"$here/14d.but" "$here/14d/wt" "$here/14d/sub/sub" "(null)" &&
	try_case 14d/sub/sub unset "$here/14d/.but" \
		"$here/14d.but" "$here/14d/wt" "$here/14d/sub/sub" "(null)" &&

	setup_repo 14e "$here" butfile unset &&
	try_case 14e unset .but \
		"$here/14e.but" "$here" "$here" 14e/ &&
	try_case 14e unset "$here/14e/.but" \
		"$here/14e.but" "$here" "$here" 14e/ &&
	try_case 14e/sub/sub unset ../../.but \
		"$here/14e.but" "$here" "$here" 14e/sub/sub/ &&
	try_case 14e/sub/sub unset "$here/14e/.but" \
		"$here/14e.but" "$here" "$here" 14e/sub/sub/
'

test_expect_success '#14b: core.worktree is relative to actual but dir' '
	setup_repo 14b ../14b butfile unset &&
	try_case 14b unset .but \
		"$here/14b.but" "$here/14b" "$here/14b" "(null)" &&
	try_case 14b unset "$here/14b/.but" \
		"$here/14b.but" "$here/14b" "$here/14b" "(null)" &&
	try_case 14b/sub/sub unset ../../.but \
		"$here/14b.but" "$here/14b" "$here/14b" sub/sub/ &&
	try_case 14b/sub/sub unset "$here/14b/.but" \
		"$here/14b.but" "$here/14b" "$here/14b" sub/sub/ &&

	setup_repo 14f ../ butfile unset &&
	try_case 14f unset .but \
		"$here/14f.but" "$here" "$here" 14f/ &&
	try_case 14f unset "$here/14f/.but" \
		"$here/14f.but" "$here" "$here" 14f/ &&
	try_case 14f/sub/sub unset ../../.but \
		"$here/14f.but" "$here" "$here" 14f/sub/sub/ &&
	try_case 14f/sub/sub unset "$here/14f/.but" \
		"$here/14f.but" "$here" "$here" 14f/sub/sub/
'

# case #15: BUT_WORK_TREE overrides core.worktree (butfile case).
test_expect_success '#15: setup' '
	setup_repo 15 non-existent butfile unset &&
	mkdir -p 15/sub/sub 15/wt/sub
'
run_wt_tests 15 butfile

test_expect_success '#16a: implicitly bare repo (cwd inside .but dir)' '
	setup_repo 16a unset "" unset &&
	mkdir -p 16a/.but/wt/sub &&

	try_case 16a/.but unset unset \
		. "(null)" "$here/16a/.but" "(null)" &&
	try_case 16a/.but/wt unset unset \
		"$here/16a/.but" "(null)" "$here/16a/.but/wt" "(null)" &&
	try_case 16a/.but/wt/sub unset unset \
		"$here/16a/.but" "(null)" "$here/16a/.but/wt/sub" "(null)"
'

test_expect_success '#16b: bare .but (cwd inside .but dir)' '
	setup_repo 16b unset "" true &&
	mkdir -p 16b/.but/wt/sub &&

	try_case 16b/.but unset unset \
		. "(null)" "$here/16b/.but" "(null)" &&
	try_case 16b/.but/wt unset unset \
		"$here/16b/.but" "(null)" "$here/16b/.but/wt" "(null)" &&
	try_case 16b/.but/wt/sub unset unset \
		"$here/16b/.but" "(null)" "$here/16b/.but/wt/sub" "(null)"
'

test_expect_success '#16c: bare .but has no worktree' '
	try_repo 16c unset unset unset "" true \
		.but "(null)" "$here/16c" "(null)" \
		"$here/16c/.but" "(null)" "$here/16c/sub" "(null)"
'

test_expect_success '#16d: bareness preserved across alias' '
	setup_repo 16d unset "" unset &&
	(
		cd 16d/.but &&
		test_must_fail but status &&
		but config alias.st status &&
		test_must_fail but st
	)
'

test_expect_success '#16e: bareness preserved by --bare' '
	setup_repo 16e unset "" unset &&
	(
		cd 16e/.but &&
		test_must_fail but status &&
		test_must_fail but --bare status
	)
'

test_expect_success '#17: BUT_WORK_TREE without explicit BUT_DIR is accepted (bare case)' '
	# Just like #16.
	setup_repo 17a unset "" true &&
	setup_repo 17b unset "" true &&
	mkdir -p 17a/.but/wt/sub &&
	mkdir -p 17b/.but/wt/sub &&

	try_case 17a/.but "$here/17a" unset \
		"$here/17a/.but" "$here/17a" "$here/17a" .but/ \
		2>message &&
	try_case 17a/.but/wt "$here/17a" unset \
		"$here/17a/.but" "$here/17a" "$here/17a" .but/wt/ &&
	try_case 17a/.but/wt/sub "$here/17a" unset \
		"$here/17a/.but" "$here/17a" "$here/17a" .but/wt/sub/ &&

	try_case 17b/.but "$here/17b" unset \
		"$here/17b/.but" "$here/17b" "$here/17b" .but/ &&
	try_case 17b/.but/wt "$here/17b" unset \
		"$here/17b/.but" "$here/17b" "$here/17b" .but/wt/ &&
	try_case 17b/.but/wt/sub "$here/17b" unset \
		"$here/17b/.but" "$here/17b" "$here/17b" .but/wt/sub/ &&

	try_repo 17c "$here/17c" unset unset "" true \
		.but "$here/17c" "$here/17c" "(null)" \
		"$here/17c/.but" "$here/17c" "$here/17c" sub/ 2>message &&
	test_must_be_empty message
'

test_expect_success '#18: bare .but named by BUT_DIR has no worktree' '
	try_repo 18 unset .but unset "" true \
		.but "(null)" "$here/18" "(null)" \
		../.but "(null)" "$here/18/sub" "(null)" &&
	try_repo 18b unset "$here/18b/.but" unset "" true \
		"$here/18b/.but" "(null)" "$here/18b" "(null)" \
		"$here/18b/.but" "(null)" "$here/18b/sub" "(null)"
'

# Case #19: BUT_DIR + BUT_WORK_TREE suppresses bareness.
test_expect_success '#19: setup' '
	setup_repo 19 unset "" true &&
	mkdir -p 19/sub/sub 19/wt/sub
'
run_wt_tests 19

test_expect_success '#20a: core.worktree without BUT_DIR accepted (inside .but)' '
	# Unlike case #16a.
	setup_repo 20a "$here/20a" "" unset &&
	mkdir -p 20a/.but/wt/sub &&
	try_case 20a/.but unset unset \
		"$here/20a/.but" "$here/20a" "$here/20a" .but/ 2>message &&
	try_case 20a/.but/wt unset unset \
		"$here/20a/.but" "$here/20a" "$here/20a" .but/wt/ &&
	try_case 20a/.but/wt/sub unset unset \
		"$here/20a/.but" "$here/20a" "$here/20a" .but/wt/sub/ &&
	test_must_be_empty message
'

test_expect_success '#20b/c: core.worktree and core.bare conflict' '
	setup_repo 20b non-existent "" true &&
	mkdir -p 20b/.but/wt/sub &&
	(
		cd 20b/.but &&
		test_must_fail but status >/dev/null
	) 2>message &&
	grep "core.bare and core.worktree" message
'

test_expect_success '#20d: core.worktree and core.bare OK when working tree not needed' '
	setup_repo 20d non-existent "" true &&
	mkdir -p 20d/.but/wt/sub &&
	(
		cd 20d/.but &&
		but config foo.bar value
	)
'

# Case #21: core.worktree/BUT_WORK_TREE overrides core.bare' '
test_expect_success '#21: setup, core.worktree warns before overriding core.bare' '
	setup_repo 21 non-existent "" unset &&
	mkdir -p 21/.but/wt/sub &&
	(
		cd 21/.but &&
		BUT_WORK_TREE="$here/21" &&
		export BUT_WORK_TREE &&
		but status >/dev/null
	) 2>message &&
	test_must_be_empty message

'
run_wt_tests 21

test_expect_success '#22a: core.worktree = BUT_DIR = .but dir' '
	# like case #6.

	setup_repo 22a "$here/22a/.but" "" unset &&
	setup_repo 22ab . "" unset &&
	mkdir -p 22a/.but/sub 22a/sub &&
	mkdir -p 22ab/.but/sub 22ab/sub &&
	try_case 22a/.but unset . \
		. "$here/22a/.but" "$here/22a/.but" "(null)" &&
	try_case 22a/.but unset "$here/22a/.but" \
		"$here/22a/.but" "$here/22a/.but" "$here/22a/.but" "(null)" &&
	try_case 22a/.but/sub unset .. \
		"$here/22a/.but" "$here/22a/.but" "$here/22a/.but" sub/ &&
	try_case 22a/.but/sub unset "$here/22a/.but" \
		"$here/22a/.but" "$here/22a/.but" "$here/22a/.but" sub/ &&

	try_case 22ab/.but unset . \
		. "$here/22ab/.but" "$here/22ab/.but" "(null)" &&
	try_case 22ab/.but unset "$here/22ab/.but" \
		"$here/22ab/.but" "$here/22ab/.but" "$here/22ab/.but" "(null)" &&
	try_case 22ab/.but/sub unset .. \
		"$here/22ab/.but" "$here/22ab/.but" "$here/22ab/.but" sub/ &&
	try_case 22ab/.but unset "$here/22ab/.but" \
		"$here/22ab/.but" "$here/22ab/.but" "$here/22ab/.but" "(null)"
'

test_expect_success '#22b: core.worktree child of .but, BUT_DIR=.but' '
	setup_repo 22b "$here/22b/.but/wt" "" unset &&
	setup_repo 22bb wt "" unset &&
	mkdir -p 22b/.but/sub 22b/sub 22b/.but/wt/sub 22b/wt/sub &&
	mkdir -p 22bb/.but/sub 22bb/sub 22bb/.but/wt 22bb/wt &&

	try_case 22b/.but unset . \
		. "$here/22b/.but/wt" "$here/22b/.but" "(null)" &&
	try_case 22b/.but unset "$here/22b/.but" \
		"$here/22b/.but" "$here/22b/.but/wt" "$here/22b/.but" "(null)" &&
	try_case 22b/.but/sub unset .. \
		.. "$here/22b/.but/wt" "$here/22b/.but/sub" "(null)" &&
	try_case 22b/.but/sub unset "$here/22b/.but" \
		"$here/22b/.but" "$here/22b/.but/wt" "$here/22b/.but/sub" "(null)" &&

	try_case 22bb/.but unset . \
		. "$here/22bb/.but/wt" "$here/22bb/.but" "(null)" &&
	try_case 22bb/.but unset "$here/22bb/.but" \
		"$here/22bb/.but" "$here/22bb/.but/wt" "$here/22bb/.but" "(null)" &&
	try_case 22bb/.but/sub unset .. \
		.. "$here/22bb/.but/wt" "$here/22bb/.but/sub" "(null)" &&
	try_case 22bb/.but/sub unset "$here/22bb/.but" \
		"$here/22bb/.but" "$here/22bb/.but/wt" "$here/22bb/.but/sub" "(null)"
'

test_expect_success '#22c: core.worktree = .but/.., BUT_DIR=.but' '
	setup_repo 22c "$here/22c" "" unset &&
	setup_repo 22cb .. "" unset &&
	mkdir -p 22c/.but/sub 22c/sub &&
	mkdir -p 22cb/.but/sub 22cb/sub &&

	try_case 22c/.but unset . \
		"$here/22c/.but" "$here/22c" "$here/22c" .but/ &&
	try_case 22c/.but unset "$here/22c/.but" \
		"$here/22c/.but" "$here/22c" "$here/22c" .but/ &&
	try_case 22c/.but/sub unset .. \
		"$here/22c/.but" "$here/22c" "$here/22c" .but/sub/ &&
	try_case 22c/.but/sub unset "$here/22c/.but" \
		"$here/22c/.but" "$here/22c" "$here/22c" .but/sub/ &&

	try_case 22cb/.but unset . \
		"$here/22cb/.but" "$here/22cb" "$here/22cb" .but/ &&
	try_case 22cb/.but unset "$here/22cb/.but" \
		"$here/22cb/.but" "$here/22cb" "$here/22cb" .but/ &&
	try_case 22cb/.but/sub unset .. \
		"$here/22cb/.but" "$here/22cb" "$here/22cb" .but/sub/ &&
	try_case 22cb/.but/sub unset "$here/22cb/.but" \
		"$here/22cb/.but" "$here/22cb" "$here/22cb" .but/sub/
'

test_expect_success '#22.2: core.worktree and core.bare conflict' '
	setup_repo 22 "$here/22" "" true &&
	(
		cd 22/.but &&
		BUT_DIR=. &&
		export BUT_DIR &&
		test_must_fail but status 2>result
	) &&
	(
		cd 22 &&
		BUT_DIR=.but &&
		export BUT_DIR &&
		test_must_fail but status 2>result
	) &&
	grep "core.bare and core.worktree" 22/.but/result &&
	grep "core.bare and core.worktree" 22/result
'

# Case #23: BUT_DIR + BUT_WORK_TREE(+core.worktree) suppresses bareness.
test_expect_success '#23: setup' '
	setup_repo 23 non-existent "" true &&
	mkdir -p 23/sub/sub 23/wt/sub
'
run_wt_tests 23

test_expect_success '#24: bare repo has no worktree (butfile case)' '
	try_repo 24 unset unset unset butfile true \
		"$here/24.but" "(null)" "$here/24" "(null)" \
		"$here/24.but" "(null)" "$here/24/sub" "(null)"
'

test_expect_success '#25: BUT_WORK_TREE accepted if BUT_DIR unset (bare butfile case)' '
	try_repo 25 "$here/25" unset unset butfile true \
		"$here/25.but" "$here/25" "$here/25" "(null)"  \
		"$here/25.but" "$here/25" "$here/25" "sub/" 2>message &&
	test_must_be_empty message
'

test_expect_success '#26: bare repo has no worktree (BUT_DIR -> butfile case)' '
	try_repo 26 unset "$here/26/.but" unset butfile true \
		"$here/26.but" "(null)" "$here/26" "(null)" \
		"$here/26.but" "(null)" "$here/26/sub" "(null)" &&
	try_repo 26b unset .but unset butfile true \
		"$here/26b.but" "(null)" "$here/26b" "(null)" \
		"$here/26b.but" "(null)" "$here/26b/sub" "(null)"
'

# Case #27: BUT_DIR + BUT_WORK_TREE suppresses bareness (with butfile).
test_expect_success '#27: setup' '
	setup_repo 27 unset butfile true &&
	mkdir -p 27/sub/sub 27/wt/sub
'
run_wt_tests 27 butfile

test_expect_success '#28: core.worktree and core.bare conflict (butfile case)' '
	setup_repo 28 "$here/28" butfile true &&
	(
		cd 28 &&
		test_must_fail but status
	) 2>message &&
	grep "core.bare and core.worktree" message
'

# Case #29: BUT_WORK_TREE(+core.worktree) overrides core.bare (butfile case).
test_expect_success '#29: setup' '
	setup_repo 29 non-existent butfile true &&
	mkdir -p 29/sub/sub 29/wt/sub &&
	(
		cd 29 &&
		BUT_WORK_TREE="$here/29" &&
		export BUT_WORK_TREE &&
		but status
	) 2>message &&
	test_must_be_empty message
'
run_wt_tests 29 butfile

test_expect_success '#30: core.worktree and core.bare conflict (butfile version)' '
	# Just like case #22.
	setup_repo 30 "$here/30" butfile true &&
	(
		cd 30 &&
		test_must_fail env BUT_DIR=.but but status 2>result
	) &&
	grep "core.bare and core.worktree" 30/result
'

# Case #31: BUT_DIR + BUT_WORK_TREE(+core.worktree) suppresses
# bareness (butfile version).
test_expect_success '#31: setup' '
	setup_repo 31 non-existent butfile true &&
	mkdir -p 31/sub/sub 31/wt/sub
'
run_wt_tests 31 butfile

test_done
