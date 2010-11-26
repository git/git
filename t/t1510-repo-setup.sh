#!/bin/sh

test_description='Tests of cwd/prefix/worktree/gitdir setup in all cases'

. ./test-lib.sh

#
# A few rules for repo setup:
#
# 1. GIT_DIR is relative to user's cwd. --git-dir is equivalent to
#    GIT_DIR.
#
# 2. .git file is relative to parent directory. .git file is basically
#    symlink in disguise. The directory where .git file points to will
#    become new git_dir.
#
# 3. core.worktree is relative to git_dir.
#
# 4. GIT_WORK_TREE is relative to user's cwd. --work-tree is
#    equivalent to GIT_WORK_TREE.
#
# 5. GIT_WORK_TREE/core.worktree is only effective if GIT_DIR is set
#    Uneffective worktree settings should be warned.
#
# 6. Effective GIT_WORK_TREE overrides core.worktree and core.bare
#
# 7. Effective core.worktree conflicts with core.bare
#
# 8. If GIT_DIR is set but neither worktree nor bare setting is given,
#    original cwd becomes worktree.
#
# 9. If .git discovery is done inside a repo, the repo becomes a bare
#    repo. .git discovery is performed if GIT_DIR is not set.
#
# 10. If no worktree is available, cwd remains unchanged, prefix is
#     NULL.
#
# 11. When user's cwd is outside worktree, cwd remains unchanged,
#     prefix is NULL.
#

test_repo() {
	(
	cd "$1" &&
	if test -n "$2"; then GIT_DIR="$2" && export GIT_DIR; fi &&
	if test -n "$3"; then GIT_WORK_TREE="$3" && export GIT_WORK_TREE; fi &&
	rm -f trace &&
	GIT_TRACE="`pwd`/trace" git symbolic-ref HEAD >/dev/null &&
	grep '^setup: ' trace >result &&
	test_cmp expected result
	)
}

# Bit 0 = GIT_WORK_TREE
# Bit 1 = GIT_DIR
# Bit 2 = core.worktree
# Bit 3 = .git is a file
# Bit 4 = bare repo
# Case# = encoding of the above 5 bits

#
# Case #0
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
#  - worktree is .git's parent directory
#  - cwd is at worktree root dir
#  - prefix is calculated
#  - git_dir is set to ".git"
#  - cwd can't be outside worktree

test_expect_success '#0: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 0 0/sub &&
	cd 0 && git init && cd ..
'

test_expect_success '#0: at root' '
	cat >0/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/0
setup: cwd: $TRASH_DIRECTORY/0
setup: prefix: (null)
EOF
	test_repo 0
'

test_expect_success '#0: in subdir' '
	cat >0/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/0
setup: cwd: $TRASH_DIRECTORY/0
setup: prefix: sub/
EOF
	test_repo 0/sub
'

#
# case #1
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# GIT_WORK_TREE is ignored -> #0

test_expect_success '#1: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 1 1/sub 1.wt 1.wt/sub 1/wt 1/wt/sub &&
	cd 1 &&
	git init &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_failure '#1: at root' '
	cat >1/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/1
setup: cwd: $TRASH_DIRECTORY/1
setup: prefix: (null)
EOF
	test_repo 1
'

test_expect_failure '#1: in subdir' '
	cat >1/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/1
setup: cwd: $TRASH_DIRECTORY/1
setup: prefix: sub/
EOF
	test_repo 1/sub
'

#
# case #2
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
#  - worktree is at original cwd
#  - cwd is unchanged
#  - prefix is NULL
#  - git_dir is set to $GIT_DIR
#  - cwd can't be outside worktree

test_expect_success '#2: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 2 2/sub &&
	cd 2 && git init && cd ..
'

test_expect_success '#2: at root' '
	cat >2/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/2/.git
setup: worktree: $TRASH_DIRECTORY/2
setup: cwd: $TRASH_DIRECTORY/2
setup: prefix: (null)
EOF
	test_repo 2 "$TRASH_DIRECTORY/2/.git"
'

test_expect_success '#2: in subdir' '
	cat >2/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/2/.git
setup: worktree: $TRASH_DIRECTORY/2/sub
setup: cwd: $TRASH_DIRECTORY/2/sub
setup: prefix: (null)
EOF
	test_repo 2/sub "$TRASH_DIRECTORY/2/.git"
'

test_expect_success '#2: relative GIT_DIR at root' '
	cat >2/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/2
setup: cwd: $TRASH_DIRECTORY/2
setup: prefix: (null)
EOF
	test_repo 2 .git
'

test_expect_success '#2: relative GIT_DIR in subdir' '
	cat >2/sub/expected <<EOF &&
setup: git_dir: ../.git
setup: worktree: $TRASH_DIRECTORY/2/sub
setup: cwd: $TRASH_DIRECTORY/2/sub
setup: prefix: (null)
EOF
	test_repo 2/sub ../.git
'

#
# case #3
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
#  - worktree is set to $GIT_WORK_TREE
#  - cwd is at worktree root
#  - prefix is calculated
#  - git_dir is set to $GIT_DIR
#  - cwd can be outside worktree

test_expect_success '#3: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 3 3/sub 3/sub/sub 3.wt 3.wt/sub 3/wt 3/wt/sub &&
	cd 3 && git init && cd ..
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 .git "$TRASH_DIRECTORY/3"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 .git .
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 "$TRASH_DIRECTORY/3/.git" "$TRASH_DIRECTORY/3"
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 "$TRASH_DIRECTORY/3/.git" .
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: sub/sub/
EOF
	test_repo 3/sub/sub ../../.git "$TRASH_DIRECTORY/3"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: sub/sub/
EOF
	test_repo 3/sub/sub ../../.git ../..
'

test_expect_success '#3: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >3/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: sub/
EOF
	test_repo 3/sub "$TRASH_DIRECTORY/3/.git" "$TRASH_DIRECTORY/3"
'

test_expect_success '#3: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: sub/sub/
EOF
	test_repo 3/sub/sub "$TRASH_DIRECTORY/3/.git" ../..
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 .git "$TRASH_DIRECTORY/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 .git wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 "$TRASH_DIRECTORY/3/.git" wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3
setup: prefix: (null)
EOF
	test_repo 3 "$TRASH_DIRECTORY/3/.git" "$TRASH_DIRECTORY/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub ../../.git "$TRASH_DIRECTORY/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub ../../.git ../../wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub "$TRASH_DIRECTORY/3/.git" ../../wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY/3/wt
setup: cwd: $TRASH_DIRECTORY/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub "$TRASH_DIRECTORY/3/.git" "$TRASH_DIRECTORY/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/
EOF
	test_repo 3 .git "$TRASH_DIRECTORY"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/
EOF
	test_repo 3 .git ..
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/
EOF
	test_repo 3 "$TRASH_DIRECTORY/3/.git" ..
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/
EOF
	test_repo 3 "$TRASH_DIRECTORY/3/.git" "$TRASH_DIRECTORY"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub ../../.git "$TRASH_DIRECTORY"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub ../../.git ../../..
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub "$TRASH_DIRECTORY/3/.git" ../../../
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/3/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub "$TRASH_DIRECTORY/3/.git" "$TRASH_DIRECTORY"
'

#
# case #4
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# core.worktree is ignored -> #0

test_expect_success '#4: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 4 4/sub &&
	cd 4 &&
	git init &&
	git config core.worktree non-existent &&
	cd ..
'

test_expect_failure '#4: at root' '
	cat >4/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/4
setup: cwd: $TRASH_DIRECTORY/4
setup: prefix: (null)
EOF
	test_repo 4
'

test_expect_failure '#4: in subdir' '
	cat >4/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/4
setup: cwd: $TRASH_DIRECTORY/4
setup: prefix: sub/
EOF
	test_repo 4/sub
'

#
# case #5
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# GIT_WORK_TREE/core.worktree are ignored -> #0

test_expect_success '#5: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 5 5/sub &&
	cd 5 &&
	git init &&
	git config core.worktree non-existent &&
	GIT_WORK_TREE=non-existent-too &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_failure '#5: at root' '
	cat >5/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/5
setup: cwd: $TRASH_DIRECTORY/5
setup: prefix: (null)
EOF
	test_repo 5
'

test_expect_failure '#5: in subdir' '
	cat >5/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/5
setup: cwd: $TRASH_DIRECTORY/5
setup: prefix: sub/
EOF
	test_repo 5/sub
'

#
# case #6
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
#  - worktree is at core.worktree
#  - cwd is at worktree root
#  - prefix is calculated
#  - git_dir is at $GIT_DIR
#  - cwd can be outside worktree

test_expect_success '#6: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 6 6/sub 6/sub/sub 6.wt 6.wt/sub 6/wt 6/wt/sub &&
	cd 6 && git init && cd ..
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6" &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree .. &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR, core.worktree=.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6" &&
	test_repo 6 "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree .. &&
	test_repo 6 "$TRASH_DIRECTORY/6/.git"
'

test_expect_failure '#6: GIT_DIR(rel), core.worktree=.. in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6" &&
	test_repo 6/sub/sub ../../.git
'

test_expect_failure '#6: GIT_DIR(rel), core.worktree=..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree .. &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR, core.worktree=.. in subdir' '
	cat >6/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6" &&
	test_repo 6/sub "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree .. &&
	test_repo 6/sub/sub "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6/wt" &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../wt &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../wt &&
	test_repo 6 "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6/wt" &&
	test_repo 6 "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6/wt" &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../wt &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../wt &&
	test_repo 6/sub/sub "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY/6/wt
setup: cwd: $TRASH_DIRECTORY/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY/6/wt" &&
	test_repo 6/sub/sub "$TRASH_DIRECTORY/6/.git"
'

test_expect_failure '#6: GIT_DIR(rel), core.worktree=../.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 6 .git
'

test_expect_failure '#6: GIT_DIR(rel), core.worktree=../..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../../ &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR, core.worktree=../..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../../ &&
	test_repo 6 "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 6 "$TRASH_DIRECTORY/6/.git"
'

test_expect_failure '#6: GIT_DIR(rel), core.worktree=../.. in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 6/sub/sub ../../.git
'

test_expect_failure '#6: GIT_DIR(rel), core.worktree=../..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../.. &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR, core.worktree=../..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree ../.. &&
	test_repo 6/sub/sub "$TRASH_DIRECTORY/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../.. in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/6/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 6/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/6/.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 6/sub/sub "$TRASH_DIRECTORY/6/.git"
'

#
# case #7
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# core.worktree is overridden by GIT_WORK_TREE -> #3

test_expect_success '#7: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 7 7/sub 7/sub/sub 7.wt 7.wt/sub 7/wt 7/wt/sub &&
	cd 7 &&
	git init &&
	git config core.worktree non-existent &&
	cd ..
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 .git "$TRASH_DIRECTORY/7"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 .git .
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 "$TRASH_DIRECTORY/7/.git" "$TRASH_DIRECTORY/7"
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 "$TRASH_DIRECTORY/7/.git" .
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: sub/sub/
EOF
	test_repo 7/sub/sub ../../.git "$TRASH_DIRECTORY/7"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: sub/sub/
EOF
	test_repo 7/sub/sub ../../.git ../..
'

test_expect_success '#7: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >7/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: sub/
EOF
	test_repo 7/sub "$TRASH_DIRECTORY/7/.git" "$TRASH_DIRECTORY/7"
'

test_expect_success '#7: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: sub/sub/
EOF
	test_repo 7/sub/sub "$TRASH_DIRECTORY/7/.git" ../..
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 .git "$TRASH_DIRECTORY/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 .git wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 "$TRASH_DIRECTORY/7/.git" wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7
setup: prefix: (null)
EOF
	test_repo 7 "$TRASH_DIRECTORY/7/.git" "$TRASH_DIRECTORY/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub ../../.git "$TRASH_DIRECTORY/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub ../../.git ../../wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub "$TRASH_DIRECTORY/7/.git" ../../wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY/7/wt
setup: cwd: $TRASH_DIRECTORY/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub "$TRASH_DIRECTORY/7/.git" "$TRASH_DIRECTORY/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/
EOF
	test_repo 7 .git "$TRASH_DIRECTORY"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/
EOF
	test_repo 7 .git ..
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/
EOF
	test_repo 7 "$TRASH_DIRECTORY/7/.git" ..
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/
EOF
	test_repo 7 "$TRASH_DIRECTORY/7/.git" "$TRASH_DIRECTORY"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub ../../.git "$TRASH_DIRECTORY"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub ../../.git ../../..
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub "$TRASH_DIRECTORY/7/.git" ../../../
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/7/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub "$TRASH_DIRECTORY/7/.git" "$TRASH_DIRECTORY"
'

#
# case #8
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #0 except that git_dir is set by .git file

test_expect_success '#8: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 8 8/sub &&
	cd 8 &&
	git init &&
	mv .git ../8.git &&
	echo gitdir: ../8.git >.git &&
	cd ..
'

test_expect_success '#8: at root' '
	cat >8/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/8.git
setup: worktree: $TRASH_DIRECTORY/8
setup: cwd: $TRASH_DIRECTORY/8
setup: prefix: (null)
EOF
	test_repo 8
'

test_expect_success '#8: in subdir' '
	cat >8/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/8.git
setup: worktree: $TRASH_DIRECTORY/8
setup: cwd: $TRASH_DIRECTORY/8
setup: prefix: sub/
EOF
	test_repo 8/sub
'

#
# case #9
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #1 except that git_dir is set by .git file

test_expect_success '#9: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 9 9/sub 9.wt 9.wt/sub 9/wt 9/wt/sub &&
	cd 9 &&
	git init &&
	mv .git ../9.git &&
	echo gitdir: ../9.git >.git &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_failure '#9: at root' '
	cat >9/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/9.git
setup: worktree: $TRASH_DIRECTORY/9
setup: cwd: $TRASH_DIRECTORY/9
setup: prefix: (null)
EOF
	test_repo 9
'

test_expect_failure '#9: in subdir' '
	cat >9/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/9.git
setup: worktree: $TRASH_DIRECTORY/9
setup: cwd: $TRASH_DIRECTORY/9
setup: prefix: sub/
EOF
	test_repo 9/sub
'

#
# case #10
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #2 except that git_dir is set by .git file

test_expect_success '#10: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 10 10/sub &&
	cd 10 &&
	git init &&
	mv .git ../10.git &&
	echo gitdir: ../10.git >.git &&
	cd ..
'

test_expect_failure '#10: at root' '
	cat >10/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/10.git
setup: worktree: $TRASH_DIRECTORY/10
setup: cwd: $TRASH_DIRECTORY/10
setup: prefix: (null)
EOF
	test_repo 10 "$TRASH_DIRECTORY/10/.git"
'

test_expect_failure '#10: in subdir' '
	cat >10/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/10.git
setup: worktree: $TRASH_DIRECTORY/10/sub
setup: cwd: $TRASH_DIRECTORY/10/sub
setup: prefix: (null)
EOF
	test_repo 10/sub "$TRASH_DIRECTORY/10/.git"
'

test_expect_failure '#10: relative GIT_DIR at root' '
	cat >10/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/10.git
setup: worktree: $TRASH_DIRECTORY/10
setup: cwd: $TRASH_DIRECTORY/10
setup: prefix: (null)
EOF
	test_repo 10 .git
'

test_expect_failure '#10: relative GIT_DIR in subdir' '
	cat >10/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/10.git
setup: worktree: $TRASH_DIRECTORY/10/sub
setup: cwd: $TRASH_DIRECTORY/10/sub
setup: prefix: (null)
EOF
	test_repo 10/sub ../.git
'

#
# case #11
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #3 except that git_dir is set by .git file

test_expect_success '#11: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 11 11/sub 11/sub/sub 11.wt 11.wt/sub 11/wt 11/wt/sub &&
	cd 11 &&
	git init &&
	mv .git ../11.git &&
	echo gitdir: ../11.git >.git &&
	cd ..
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 .git "$TRASH_DIRECTORY/11"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 .git .
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 "$TRASH_DIRECTORY/11/.git" "$TRASH_DIRECTORY/11"
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 "$TRASH_DIRECTORY/11/.git" .
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: sub/sub/
EOF
	test_repo 11/sub/sub ../../.git "$TRASH_DIRECTORY/11"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: sub/sub/
EOF
	test_repo 11/sub/sub ../../.git ../..
'

test_expect_failure '#11: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >11/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: sub/
EOF
	test_repo 11/sub "$TRASH_DIRECTORY/11/.git" "$TRASH_DIRECTORY/11"
'

test_expect_failure '#11: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: sub/sub/
EOF
	test_repo 11/sub/sub "$TRASH_DIRECTORY/11/.git" ../..
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 .git "$TRASH_DIRECTORY/11/wt"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 .git wt
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 "$TRASH_DIRECTORY/11/.git" wt
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11
setup: prefix: (null)
EOF
	test_repo 11 "$TRASH_DIRECTORY/11/.git" "$TRASH_DIRECTORY/11/wt"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub ../../.git "$TRASH_DIRECTORY/11/wt"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub ../../.git ../../wt
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub "$TRASH_DIRECTORY/11/.git" ../../wt
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY/11/wt
setup: cwd: $TRASH_DIRECTORY/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub "$TRASH_DIRECTORY/11/.git" "$TRASH_DIRECTORY/11/wt"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/
EOF
	test_repo 11 .git "$TRASH_DIRECTORY"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/
EOF
	test_repo 11 .git ..
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/
EOF
	test_repo 11 "$TRASH_DIRECTORY/11/.git" ..
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/
EOF
	test_repo 11 "$TRASH_DIRECTORY/11/.git" "$TRASH_DIRECTORY"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub ../../.git "$TRASH_DIRECTORY"
'

test_expect_failure '#11: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub ../../.git ../../..
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub "$TRASH_DIRECTORY/11/.git" ../../../
'

test_expect_failure '#11: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/11.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub "$TRASH_DIRECTORY/11/.git" "$TRASH_DIRECTORY"
'

#
# case #12
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #4 except that git_dir is set by .git file


test_expect_success '#12: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 12 12/sub 12/sub/sub 12.wt 12.wt/sub 12/wt 12/wt/sub &&
	cd 12 &&
	git init &&
	git config core.worktree non-existent &&
	mv .git ../12.git &&
	echo gitdir: ../12.git >.git &&
	cd ..
'

test_expect_failure '#12: at root' '
	cat >12/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/12.git
setup: worktree: $TRASH_DIRECTORY/12
setup: cwd: $TRASH_DIRECTORY/12
setup: prefix: (null)
EOF
	test_repo 12
'

test_expect_failure '#12: in subdir' '
	cat >12/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/12.git
setup: worktree: $TRASH_DIRECTORY/12
setup: cwd: $TRASH_DIRECTORY/12
setup: prefix: sub/
EOF
	test_repo 12/sub
'

#
# case #13
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #5 except that git_dir is set by .git file

test_expect_success '#13: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 13 13/sub 13/sub/sub 13.wt 13.wt/sub 13/wt 13/wt/sub &&
	cd 13 &&
	git init &&
	git config core.worktree non-existent &&
	GIT_WORK_TREE=non-existent-too &&
	export GIT_WORK_TREE &&
	mv .git ../13.git &&
	echo gitdir: ../13.git >.git &&
	cd ..
'

test_expect_failure '#13: at root' '
	cat >13/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/13.git
setup: worktree: $TRASH_DIRECTORY/13
setup: cwd: $TRASH_DIRECTORY/13
setup: prefix: (null)
EOF
	test_repo 13
'

test_expect_failure '#13: in subdir' '
	cat >13/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/13.git
setup: worktree: $TRASH_DIRECTORY/13
setup: cwd: $TRASH_DIRECTORY/13
setup: prefix: sub/
EOF
	test_repo 13/sub
'

#
# case #14
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #6 except that git_dir is set by .git file

test_expect_success '#14: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 14 14/sub 14/sub/sub 14.wt 14.wt/sub 14/wt 14/wt/sub &&
	cd 14 &&
	git init &&
	mv .git ../14.git &&
	echo gitdir: ../14.git >.git &&
	cd ..
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14 at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14" &&
	test_repo 14 .git
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14 &&
	test_repo 14 .git
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14 at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14" &&
	test_repo 14 "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14 &&
	test_repo 14 "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14 in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14" &&
	test_repo 14/sub/sub ../../.git
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14 &&
	test_repo 14/sub/sub ../../.git
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14 in subdir' '
	cat >14/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14" &&
	test_repo 14/sub "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14 &&
	test_repo 14/sub/sub "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14/wt at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14/wt" &&
	test_repo 14 .git
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14/wt(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14/wt &&
	test_repo 14 .git
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14/wt(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14/wt &&
	test_repo 14 "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14/wt at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14/wt" &&
	test_repo 14 "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14/wt in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14/wt" &&
	test_repo 14/sub/sub ../../.git
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=../14/wt(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14/wt &&
	test_repo 14/sub/sub ../../.git
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14/wt(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree ../14/wt &&
	test_repo 14/sub/sub "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR, core.worktree=../14/wt in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY/14/wt
setup: cwd: $TRASH_DIRECTORY/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY/14/wt" &&
	test_repo 14/sub/sub "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=.. at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 14 .git
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=..(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree .. &&
	test_repo 14 .git
'

test_expect_failure '#14: GIT_DIR, core.worktree=..(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree .. &&
	test_repo 14 "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR, core.worktree=.. at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 14 "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=.. in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 14/sub/sub ../../.git
'

test_expect_failure '#14: GIT_DIR(rel), core.worktree=..(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree .. &&
	test_repo 14/sub/sub ../../.git
'

test_expect_failure '#14: GIT_DIR, core.worktree=..(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree .. &&
	test_repo 14/sub/sub "$TRASH_DIRECTORY/14/.git"
'

test_expect_failure '#14: GIT_DIR, core.worktree=.. in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/14.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 14/sub/sub/
EOF
	git config --file="$TRASH_DIRECTORY/14.git/config" core.worktree "$TRASH_DIRECTORY" &&
	test_repo 14/sub/sub "$TRASH_DIRECTORY/14/.git"
'

#
# case #15
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is not set, cwd is outside .git
#
# Output:
#
# #7 except that git_dir is set by .git file

test_expect_success '#15: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 15 15/sub 15/sub/sub 15.wt 15.wt/sub 15/wt 15/wt/sub &&
	cd 15 &&
	git init &&
	git config core.worktree non-existent &&
	mv .git ../15.git &&
	echo gitdir: ../15.git >.git &&
	cd ..
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 .git "$TRASH_DIRECTORY/15"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 .git .
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 "$TRASH_DIRECTORY/15/.git" "$TRASH_DIRECTORY/15"
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 "$TRASH_DIRECTORY/15/.git" .
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: sub/sub/
EOF
	test_repo 15/sub/sub ../../.git "$TRASH_DIRECTORY/15"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: sub/sub/
EOF
	test_repo 15/sub/sub ../../.git ../..
'

test_expect_failure '#15: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >15/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: sub/
EOF
	test_repo 15/sub "$TRASH_DIRECTORY/15/.git" "$TRASH_DIRECTORY/15"
'

test_expect_failure '#15: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: sub/sub/
EOF
	test_repo 15/sub/sub "$TRASH_DIRECTORY/15/.git" ../..
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 .git "$TRASH_DIRECTORY/15/wt"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 .git wt
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 "$TRASH_DIRECTORY/15/.git" wt
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15
setup: prefix: (null)
EOF
	test_repo 15 "$TRASH_DIRECTORY/15/.git" "$TRASH_DIRECTORY/15/wt"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub ../../.git "$TRASH_DIRECTORY/15/wt"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub ../../.git ../../wt
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub "$TRASH_DIRECTORY/15/.git" ../../wt
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY/15/wt
setup: cwd: $TRASH_DIRECTORY/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub "$TRASH_DIRECTORY/15/.git" "$TRASH_DIRECTORY/15/wt"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/
EOF
	test_repo 15 .git "$TRASH_DIRECTORY"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/
EOF
	test_repo 15 .git ..
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/
EOF
	test_repo 15 "$TRASH_DIRECTORY/15/.git" ..
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/
EOF
	test_repo 15 "$TRASH_DIRECTORY/15/.git" "$TRASH_DIRECTORY"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub ../../.git "$TRASH_DIRECTORY"
'

test_expect_failure '#15: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub ../../.git ../../..
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub "$TRASH_DIRECTORY/15/.git" ../../../
'

test_expect_failure '#15: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/15.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub "$TRASH_DIRECTORY/15/.git" "$TRASH_DIRECTORY"
'

#
# case #16.1
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a directory
#  - cwd is inside .git
#
# Output:
#
#  - no worktree
#  - cwd is unchanged
#  - prefix is NULL
#  - git_dir is set
#  - cwd can't be outside worktree

test_expect_success '#16.1: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 16 16/sub &&
	cd 16 &&
	git init &&
	mkdir .git/wt .git/wt/sub &&
	cd ..
'

test_expect_success '#16.1: at .git' '
	cat >16/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/.git
setup: prefix: (null)
EOF
	test_repo 16/.git
'

test_expect_success '#16.1: in .git/wt' '
	cat >16/.git/wt/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/16/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/.git/wt
setup: prefix: (null)
EOF
	test_repo 16/.git/wt
'

test_expect_success '#16.1: in .git/wt/sub' '
	cat >16/.git/wt/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/16/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 16/.git/wt/sub
'

#
# case #16.2
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
#  - no worktree
#  - cwd is unchanged
#  - prefix is NULL
#  - git_dir is set
#  - cwd can't be outside worktree

test_expect_success '#16.2: setup' '
	git config --file="$TRASH_DIRECTORY/16/.git/config" core.bare true
'

test_expect_success '#16.2: at .git' '
	cat >16/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/.git
setup: prefix: (null)
EOF
	test_repo 16/.git
'

test_expect_success '#16.2: in .git/wt' '
	cat >16/.git/wt/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/16/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/.git/wt
setup: prefix: (null)
EOF
	test_repo 16/.git/wt
'

test_expect_success '#16.2: in .git/wt/sub' '
	cat >16/.git/wt/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/16/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 16/.git/wt/sub
'

test_expect_success '#16.2: at root' '
	cat >16/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16
setup: prefix: (null)
EOF
	test_repo 16
'

test_expect_failure '#16.2: in subdir' '
	cat >16/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/16/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/16/sub
setup: prefix: (null)
EOF
	test_repo 16/sub
'

#
# case #17.1
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a directory
#  - cwd is inside .git
#
# Output:
#
# GIT_WORK_TREE is ignored -> #16.1 (with warnings perhaps)

test_expect_success '#17.1: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 17 17/sub &&
	cd 17 &&
	git init &&
	mkdir .git/wt .git/wt/sub &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_failure '#17.1: at .git' '
	cat >17/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/.git
setup: prefix: (null)
EOF
	test_repo 17/.git
'

test_expect_failure '#17.1: in .git/wt' '
	cat >17/.git/wt/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/17/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/.git/wt
setup: prefix: (null)
EOF
	test_repo 17/.git/wt
'

test_expect_failure '#17.1: in .git/wt/sub' '
	cat >17/.git/wt/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/17/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 17/.git/wt/sub
'

#
# case #17.2
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
# GIT_WORK_TREE is ignored -> #16.2 (with warnings perhaps)

test_expect_success '#17.2: setup' '
	git config --file="$TRASH_DIRECTORY/17/.git/config" core.bare true
'

test_expect_failure '#17.2: at .git' '
	cat >17/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/.git
setup: prefix: (null)
EOF
	test_repo 17/.git
'

test_expect_failure '#17.2: in .git/wt' '
	cat >17/.git/wt/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/17/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/.git/wt
setup: prefix: (null)
EOF
	test_repo 17/.git/wt
'

test_expect_failure '#17.2: in .git/wt/sub' '
	cat >17/.git/wt/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/17/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 17/.git/wt/sub
'

test_expect_failure '#17.2: at root' '
	cat >17/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17
setup: prefix: (null)
EOF
	test_repo 17
'

test_expect_failure '#17.2: in subdir' '
	cat >17/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/17/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/17/sub
setup: prefix: (null)
EOF
	test_repo 17/sub
'

#
# case #18
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is not set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
#  - no worktree (rule #8)
#  - cwd is unchanged
#  - prefix is NULL
#  - git_dir is set to $GIT_DIR
#  - cwd can't be outside worktree

test_expect_success '#18: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 18 18/sub &&
	cd 18 &&
	git init &&
	mkdir .git/wt .git/wt/sub &&
	git config core.bare true &&
	cd ..
'

test_expect_success '#18: (rel) at root' '
	cat >18/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/18
setup: prefix: (null)
EOF
	 test_repo 18 .git
'

test_expect_success '#18: at root' '
	cat >18/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/18/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/18
setup: prefix: (null)
EOF
	 test_repo 18 "$TRASH_DIRECTORY/18/.git"
'

test_expect_success '#18: (rel) in subdir' '
	cat >18/sub/expected <<EOF &&
setup: git_dir: ../.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/18/sub
setup: prefix: (null)
EOF
	test_repo 18/sub ../.git
'

test_expect_success '#18: in subdir' '
	cat >18/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/18/.git
setup: worktree: (null)
setup: cwd: $TRASH_DIRECTORY/18/sub
setup: prefix: (null)
EOF
	test_repo 18/sub "$TRASH_DIRECTORY/18/.git"
'

#
# case #19
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - .git is a directory
#  - core.worktree is not set
#  - core.bare is set
#
# Output:
#
# bare repo is overridden by GIT_WORK_TREE -> #3

test_expect_success '#19: setup' '
	unset GIT_DIR GIT_WORK_TREE &&
	mkdir 19 19/sub 19/sub/sub 19.wt 19.wt/sub 19/wt 19/wt/sub &&
	cd 19 &&
	git init &&
	git config core.bare true &&
	cd ..
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 .git "$TRASH_DIRECTORY/19"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 .git .
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 "$TRASH_DIRECTORY/19/.git" "$TRASH_DIRECTORY/19"
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 "$TRASH_DIRECTORY/19/.git" .
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: sub/sub/
EOF
	test_repo 19/sub/sub ../../.git "$TRASH_DIRECTORY/19"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: sub/sub/
EOF
	test_repo 19/sub/sub ../../.git ../..
'

test_expect_success '#19: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >19/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: sub/
EOF
	test_repo 19/sub "$TRASH_DIRECTORY/19/.git" "$TRASH_DIRECTORY/19"
'

test_expect_success '#19: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: sub/sub/
EOF
	test_repo 19/sub/sub "$TRASH_DIRECTORY/19/.git" ../..
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 .git "$TRASH_DIRECTORY/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 .git wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 "$TRASH_DIRECTORY/19/.git" wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19
setup: prefix: (null)
EOF
	test_repo 19 "$TRASH_DIRECTORY/19/.git" "$TRASH_DIRECTORY/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub ../../.git "$TRASH_DIRECTORY/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub ../../.git ../../wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub "$TRASH_DIRECTORY/19/.git" ../../wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY/19/wt
setup: cwd: $TRASH_DIRECTORY/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub "$TRASH_DIRECTORY/19/.git" "$TRASH_DIRECTORY/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/
EOF
	test_repo 19 .git "$TRASH_DIRECTORY"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/
EOF
	test_repo 19 .git ..
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/
EOF
	test_repo 19 "$TRASH_DIRECTORY/19/.git" ..
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/
EOF
	test_repo 19 "$TRASH_DIRECTORY/19/.git" "$TRASH_DIRECTORY"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub ../../.git "$TRASH_DIRECTORY"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub ../../.git ../../..
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub "$TRASH_DIRECTORY/19/.git" ../../../
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $TRASH_DIRECTORY/19/.git
setup: worktree: $TRASH_DIRECTORY
setup: cwd: $TRASH_DIRECTORY
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub "$TRASH_DIRECTORY/19/.git" "$TRASH_DIRECTORY"
'

test_done
