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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 0 0/sub &&
	(cd 0 && git init) &&
	here=$(pwd)
'

test_expect_success '#0: at root' '
	cat >0/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/0
setup: cwd: $here/0
setup: prefix: (null)
EOF
	test_repo 0
'

test_expect_success '#0: in subdir' '
	cat >0/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/0
setup: cwd: $here/0
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 1 1/sub 1.wt 1.wt/sub 1/wt 1/wt/sub &&
	cd 1 &&
	git init &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_success '#1: at root' '
	cat >1/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/1
setup: cwd: $here/1
setup: prefix: (null)
EOF
	test_repo 1
'

test_expect_success '#1: in subdir' '
	cat >1/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/1
setup: cwd: $here/1
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 2 2/sub &&
	cd 2 && git init && cd ..
'

test_expect_success '#2: at root' '
	cat >2/expected <<EOF &&
setup: git_dir: $here/2/.git
setup: worktree: $here/2
setup: cwd: $here/2
setup: prefix: (null)
EOF
	test_repo 2 "$here/2/.git"
'

test_expect_success '#2: in subdir' '
	cat >2/sub/expected <<EOF &&
setup: git_dir: $here/2/.git
setup: worktree: $here/2/sub
setup: cwd: $here/2/sub
setup: prefix: (null)
EOF
	test_repo 2/sub "$here/2/.git"
'

test_expect_success '#2: relative GIT_DIR at root' '
	cat >2/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/2
setup: cwd: $here/2
setup: prefix: (null)
EOF
	test_repo 2 .git
'

test_expect_success '#2: relative GIT_DIR in subdir' '
	cat >2/sub/expected <<EOF &&
setup: git_dir: ../.git
setup: worktree: $here/2/sub
setup: cwd: $here/2/sub
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 3 3/sub 3/sub/sub 3.wt 3.wt/sub 3/wt 3/wt/sub &&
	cd 3 && git init && cd ..
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 .git "$here/3"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 .git .
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 "$here/3/.git" "$here/3"
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 "$here/3/.git" .
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: sub/sub/
EOF
	test_repo 3/sub/sub ../../.git "$here/3"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: sub/sub/
EOF
	test_repo 3/sub/sub ../../.git ../..
'

test_expect_success '#3: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >3/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: sub/
EOF
	test_repo 3/sub "$here/3/.git" "$here/3"
'

test_expect_success '#3: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3
setup: cwd: $here/3
setup: prefix: sub/sub/
EOF
	test_repo 3/sub/sub "$here/3/.git" ../..
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/3/wt
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 .git "$here/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/3/wt
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 .git wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3/wt
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 "$here/3/.git" wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3/wt
setup: cwd: $here/3
setup: prefix: (null)
EOF
	test_repo 3 "$here/3/.git" "$here/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/3/wt
setup: cwd: $here/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub ../../.git "$here/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/3/wt
setup: cwd: $here/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub ../../.git ../../wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3/wt
setup: cwd: $here/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub "$here/3/.git" ../../wt
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here/3/wt
setup: cwd: $here/3/sub/sub
setup: prefix: (null)
EOF
	test_repo 3/sub/sub "$here/3/.git" "$here/3/wt"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/
EOF
	test_repo 3 .git "$here"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/
EOF
	test_repo 3 .git ..
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/
EOF
	test_repo 3 "$here/3/.git" ..
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >3/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/
EOF
	test_repo 3 "$here/3/.git" "$here"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub ../../.git "$here"
'

test_expect_success '#3: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub ../../.git ../../..
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub "$here/3/.git" ../../../
'

test_expect_success '#3: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >3/sub/sub/expected <<EOF &&
setup: git_dir: $here/3/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 3/sub/sub/
EOF
	test_repo 3/sub/sub "$here/3/.git" "$here"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 4 4/sub &&
	cd 4 &&
	git init &&
	git config core.worktree non-existent &&
	cd ..
'

test_expect_success '#4: at root' '
	cat >4/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/4
setup: cwd: $here/4
setup: prefix: (null)
EOF
	test_repo 4
'

test_expect_success '#4: in subdir' '
	cat >4/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/4
setup: cwd: $here/4
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 5 5/sub &&
	cd 5 &&
	git init &&
	git config core.worktree non-existent &&
	GIT_WORK_TREE=non-existent-too &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_success '#5: at root' '
	cat >5/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/5
setup: cwd: $here/5
setup: prefix: (null)
EOF
	test_repo 5
'

test_expect_success '#5: in subdir' '
	cat >5/sub/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/5
setup: cwd: $here/5
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 6 6/sub 6/sub/sub 6.wt 6.wt/sub 6/wt 6/wt/sub &&
	cd 6 && git init && cd ..
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6" &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree .. &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR, core.worktree=.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6" &&
	test_repo 6 "$here/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree .. &&
	test_repo 6 "$here/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=.. in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6" &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree .. &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR, core.worktree=.. in subdir' '
	cat >6/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: sub/
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6" &&
	test_repo 6/sub "$here/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6
setup: cwd: $here/6
setup: prefix: sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree .. &&
	test_repo 6/sub/sub "$here/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/6/wt
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6/wt" &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/6/wt
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree ../wt &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6/wt
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree ../wt &&
	test_repo 6 "$here/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6/wt
setup: cwd: $here/6
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6/wt" &&
	test_repo 6 "$here/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/6/wt
setup: cwd: $here/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6/wt" &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../wt(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/6/wt
setup: cwd: $here/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree ../wt &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6/wt
setup: cwd: $here/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree ../wt &&
	test_repo 6/sub/sub "$here/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../wt in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here/6/wt
setup: cwd: $here/6/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/6/.git/config" core.worktree "$here/6/wt" &&
	test_repo 6/sub/sub "$here/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/
EOF
	git config --file="$here/6/.git/config" core.worktree "$here" &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/
EOF
	git config --file="$here/6/.git/config" core.worktree ../../ &&
	test_repo 6 .git
'

test_expect_success '#6: GIT_DIR, core.worktree=../..(rel) at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/
EOF
	git config --file="$here/6/.git/config" core.worktree ../../ &&
	test_repo 6 "$here/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../.. at root' '
	cat >6/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/
EOF
	git config --file="$here/6/.git/config" core.worktree "$here" &&
	test_repo 6 "$here/6/.git"
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../.. in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree "$here" &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR(rel), core.worktree=../..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree ../.. &&
	test_repo 6/sub/sub ../../.git
'

test_expect_success '#6: GIT_DIR, core.worktree=../..(rel) in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree ../.. &&
	test_repo 6/sub/sub "$here/6/.git"
'

test_expect_success '#6: GIT_DIR, core.worktree=../.. in subdir' '
	cat >6/sub/sub/expected <<EOF &&
setup: git_dir: $here/6/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 6/sub/sub/
EOF
	git config --file="$here/6/.git/config" core.worktree "$here" &&
	test_repo 6/sub/sub "$here/6/.git"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 7 7/sub 7/sub/sub 7.wt 7.wt/sub 7/wt 7/wt/sub &&
	cd 7 &&
	git init &&
	git config core.worktree non-existent &&
	cd ..
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 .git "$here/7"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 .git .
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 "$here/7/.git" "$here/7"
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 "$here/7/.git" .
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: sub/sub/
EOF
	test_repo 7/sub/sub ../../.git "$here/7"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: sub/sub/
EOF
	test_repo 7/sub/sub ../../.git ../..
'

test_expect_success '#7: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >7/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: sub/
EOF
	test_repo 7/sub "$here/7/.git" "$here/7"
'

test_expect_success '#7: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7
setup: cwd: $here/7
setup: prefix: sub/sub/
EOF
	test_repo 7/sub/sub "$here/7/.git" ../..
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/7/wt
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 .git "$here/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/7/wt
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 .git wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7/wt
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 "$here/7/.git" wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7/wt
setup: cwd: $here/7
setup: prefix: (null)
EOF
	test_repo 7 "$here/7/.git" "$here/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/7/wt
setup: cwd: $here/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub ../../.git "$here/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/7/wt
setup: cwd: $here/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub ../../.git ../../wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7/wt
setup: cwd: $here/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub "$here/7/.git" ../../wt
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here/7/wt
setup: cwd: $here/7/sub/sub
setup: prefix: (null)
EOF
	test_repo 7/sub/sub "$here/7/.git" "$here/7/wt"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/
EOF
	test_repo 7 .git "$here"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/
EOF
	test_repo 7 .git ..
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/
EOF
	test_repo 7 "$here/7/.git" ..
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >7/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/
EOF
	test_repo 7 "$here/7/.git" "$here"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub ../../.git "$here"
'

test_expect_success '#7: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub ../../.git ../../..
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub "$here/7/.git" ../../../
'

test_expect_success '#7: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >7/sub/sub/expected <<EOF &&
setup: git_dir: $here/7/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 7/sub/sub/
EOF
	test_repo 7/sub/sub "$here/7/.git" "$here"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 8 8/sub &&
	cd 8 &&
	git init &&
	mv .git ../8.git &&
	echo gitdir: ../8.git >.git &&
	cd ..
'

test_expect_success '#8: at root' '
	cat >8/expected <<EOF &&
setup: git_dir: $here/8.git
setup: worktree: $here/8
setup: cwd: $here/8
setup: prefix: (null)
EOF
	test_repo 8
'

test_expect_success '#8: in subdir' '
	cat >8/sub/expected <<EOF &&
setup: git_dir: $here/8.git
setup: worktree: $here/8
setup: cwd: $here/8
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 9 9/sub 9.wt 9.wt/sub 9/wt 9/wt/sub &&
	cd 9 &&
	git init &&
	mv .git ../9.git &&
	echo gitdir: ../9.git >.git &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_success '#9: at root' '
	cat >9/expected <<EOF &&
setup: git_dir: $here/9.git
setup: worktree: $here/9
setup: cwd: $here/9
setup: prefix: (null)
EOF
	test_repo 9
'

test_expect_success '#9: in subdir' '
	cat >9/sub/expected <<EOF &&
setup: git_dir: $here/9.git
setup: worktree: $here/9
setup: cwd: $here/9
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 10 10/sub &&
	cd 10 &&
	git init &&
	mv .git ../10.git &&
	echo gitdir: ../10.git >.git &&
	cd ..
'

test_expect_success '#10: at root' '
	cat >10/expected <<EOF &&
setup: git_dir: $here/10.git
setup: worktree: $here/10
setup: cwd: $here/10
setup: prefix: (null)
EOF
	test_repo 10 "$here/10/.git"
'

test_expect_success '#10: in subdir' '
	cat >10/sub/expected <<EOF &&
setup: git_dir: $here/10.git
setup: worktree: $here/10/sub
setup: cwd: $here/10/sub
setup: prefix: (null)
EOF
	test_repo 10/sub "$here/10/.git"
'

test_expect_success '#10: relative GIT_DIR at root' '
	cat >10/expected <<EOF &&
setup: git_dir: $here/10.git
setup: worktree: $here/10
setup: cwd: $here/10
setup: prefix: (null)
EOF
	test_repo 10 .git
'

test_expect_success '#10: relative GIT_DIR in subdir' '
	cat >10/sub/expected <<EOF &&
setup: git_dir: $here/10.git
setup: worktree: $here/10/sub
setup: cwd: $here/10/sub
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 11 11/sub 11/sub/sub 11.wt 11.wt/sub 11/wt 11/wt/sub &&
	cd 11 &&
	git init &&
	mv .git ../11.git &&
	echo gitdir: ../11.git >.git &&
	cd ..
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 .git "$here/11"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 .git .
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 "$here/11/.git" "$here/11"
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 "$here/11/.git" .
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: sub/sub/
EOF
	test_repo 11/sub/sub ../../.git "$here/11"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: sub/sub/
EOF
	test_repo 11/sub/sub ../../.git ../..
'

test_expect_success '#11: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >11/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: sub/
EOF
	test_repo 11/sub "$here/11/.git" "$here/11"
'

test_expect_success '#11: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11
setup: cwd: $here/11
setup: prefix: sub/sub/
EOF
	test_repo 11/sub/sub "$here/11/.git" ../..
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 .git "$here/11/wt"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 .git wt
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 "$here/11/.git" wt
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11
setup: prefix: (null)
EOF
	test_repo 11 "$here/11/.git" "$here/11/wt"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub ../../.git "$here/11/wt"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub ../../.git ../../wt
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub "$here/11/.git" ../../wt
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here/11/wt
setup: cwd: $here/11/sub/sub
setup: prefix: (null)
EOF
	test_repo 11/sub/sub "$here/11/.git" "$here/11/wt"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/
EOF
	test_repo 11 .git "$here"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/
EOF
	test_repo 11 .git ..
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/
EOF
	test_repo 11 "$here/11/.git" ..
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >11/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/
EOF
	test_repo 11 "$here/11/.git" "$here"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub ../../.git "$here"
'

test_expect_success '#11: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub ../../.git ../../..
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub "$here/11/.git" ../../../
'

test_expect_success '#11: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >11/sub/sub/expected <<EOF &&
setup: git_dir: $here/11.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 11/sub/sub/
EOF
	test_repo 11/sub/sub "$here/11/.git" "$here"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 12 12/sub 12/sub/sub 12.wt 12.wt/sub 12/wt 12/wt/sub &&
	cd 12 &&
	git init &&
	git config core.worktree non-existent &&
	mv .git ../12.git &&
	echo gitdir: ../12.git >.git &&
	cd ..
'

test_expect_success '#12: at root' '
	cat >12/expected <<EOF &&
setup: git_dir: $here/12.git
setup: worktree: $here/12
setup: cwd: $here/12
setup: prefix: (null)
EOF
	test_repo 12
'

test_expect_success '#12: in subdir' '
	cat >12/sub/expected <<EOF &&
setup: git_dir: $here/12.git
setup: worktree: $here/12
setup: cwd: $here/12
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
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

test_expect_success '#13: at root' '
	cat >13/expected <<EOF &&
setup: git_dir: $here/13.git
setup: worktree: $here/13
setup: cwd: $here/13
setup: prefix: (null)
EOF
	test_repo 13
'

test_expect_success '#13: in subdir' '
	cat >13/sub/expected <<EOF &&
setup: git_dir: $here/13.git
setup: worktree: $here/13
setup: cwd: $here/13
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 14 14/sub 14/sub/sub 14.wt 14.wt/sub 14/wt 14/wt/sub &&
	cd 14 &&
	git init &&
	mv .git ../14.git &&
	echo gitdir: ../14.git >.git &&
	cd ..
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14 at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14" &&
	test_repo 14 .git
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree ../14 &&
	test_repo 14 .git
'

test_expect_success '#14: GIT_DIR, core.worktree=../14 at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14" &&
	test_repo 14 "$here/14/.git"
'

test_expect_success '#14: GIT_DIR, core.worktree=../14(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree ../14 &&
	test_repo 14 "$here/14/.git"
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14 in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14" &&
	test_repo 14/sub/sub ../../.git
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree ../14 &&
	test_repo 14/sub/sub ../../.git
'

test_expect_success '#14: GIT_DIR, core.worktree=../14 in subdir' '
	cat >14/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: sub/
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14" &&
	test_repo 14/sub "$here/14/.git"
'

test_expect_success '#14: GIT_DIR, core.worktree=../14(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14
setup: cwd: $here/14
setup: prefix: sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree ../14 &&
	test_repo 14/sub/sub "$here/14/.git"
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14/wt at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14/wt" &&
	test_repo 14 .git
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14/wt(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree ../14/wt &&
	test_repo 14 .git
'

test_expect_success '#14: GIT_DIR, core.worktree=../14/wt(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree ../14/wt &&
	test_repo 14 "$here/14/.git"
'

test_expect_success '#14: GIT_DIR, core.worktree=../14/wt at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14/wt" &&
	test_repo 14 "$here/14/.git"
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14/wt in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14/wt" &&
	test_repo 14/sub/sub ../../.git
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=../14/wt(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree ../14/wt &&
	test_repo 14/sub/sub ../../.git
'

test_expect_success '#14: GIT_DIR, core.worktree=../14/wt(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree ../14/wt &&
	test_repo 14/sub/sub "$here/14/.git"
'

test_expect_success '#14: GIT_DIR, core.worktree=../14/wt in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here/14/wt
setup: cwd: $here/14/sub/sub
setup: prefix: (null)
EOF
	git config --file="$here/14.git/config" core.worktree "$here/14/wt" &&
	test_repo 14/sub/sub "$here/14/.git"
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=.. at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/
EOF
	git config --file="$here/14.git/config" core.worktree "$here" &&
	test_repo 14 .git
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=..(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/
EOF
	git config --file="$here/14.git/config" core.worktree .. &&
	test_repo 14 .git
'

test_expect_success '#14: GIT_DIR, core.worktree=..(rel) at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/
EOF
	git config --file="$here/14.git/config" core.worktree .. &&
	test_repo 14 "$here/14/.git"
'

test_expect_success '#14: GIT_DIR, core.worktree=.. at root' '
	cat >14/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/
EOF
	git config --file="$here/14.git/config" core.worktree "$here" &&
	test_repo 14 "$here/14/.git"
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=.. in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree "$here" &&
	test_repo 14/sub/sub ../../.git
'

test_expect_success '#14: GIT_DIR(rel), core.worktree=..(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree .. &&
	test_repo 14/sub/sub ../../.git
'

test_expect_success '#14: GIT_DIR, core.worktree=..(rel) in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree .. &&
	test_repo 14/sub/sub "$here/14/.git"
'

test_expect_success '#14: GIT_DIR, core.worktree=.. in subdir' '
	cat >14/sub/sub/expected <<EOF &&
setup: git_dir: $here/14.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 14/sub/sub/
EOF
	git config --file="$here/14.git/config" core.worktree "$here" &&
	test_repo 14/sub/sub "$here/14/.git"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 15 15/sub 15/sub/sub 15.wt 15.wt/sub 15/wt 15/wt/sub &&
	cd 15 &&
	git init &&
	git config core.worktree non-existent &&
	mv .git ../15.git &&
	echo gitdir: ../15.git >.git &&
	cd ..
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 .git "$here/15"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 .git .
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 "$here/15/.git" "$here/15"
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 "$here/15/.git" .
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: sub/sub/
EOF
	test_repo 15/sub/sub ../../.git "$here/15"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: sub/sub/
EOF
	test_repo 15/sub/sub ../../.git ../..
'

test_expect_success '#15: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >15/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: sub/
EOF
	test_repo 15/sub "$here/15/.git" "$here/15"
'

test_expect_success '#15: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15
setup: cwd: $here/15
setup: prefix: sub/sub/
EOF
	test_repo 15/sub/sub "$here/15/.git" ../..
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 .git "$here/15/wt"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 .git wt
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 "$here/15/.git" wt
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15
setup: prefix: (null)
EOF
	test_repo 15 "$here/15/.git" "$here/15/wt"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub ../../.git "$here/15/wt"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub ../../.git ../../wt
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub "$here/15/.git" ../../wt
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here/15/wt
setup: cwd: $here/15/sub/sub
setup: prefix: (null)
EOF
	test_repo 15/sub/sub "$here/15/.git" "$here/15/wt"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/
EOF
	test_repo 15 .git "$here"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/
EOF
	test_repo 15 .git ..
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/
EOF
	test_repo 15 "$here/15/.git" ..
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >15/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/
EOF
	test_repo 15 "$here/15/.git" "$here"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub ../../.git "$here"
'

test_expect_success '#15: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub ../../.git ../../..
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub "$here/15/.git" ../../../
'

test_expect_success '#15: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >15/sub/sub/expected <<EOF &&
setup: git_dir: $here/15.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 15/sub/sub/
EOF
	test_repo 15/sub/sub "$here/15/.git" "$here"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
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
setup: cwd: $here/16/.git
setup: prefix: (null)
EOF
	test_repo 16/.git
'

test_expect_success '#16.1: in .git/wt' '
	cat >16/.git/wt/expected <<EOF &&
setup: git_dir: $here/16/.git
setup: worktree: (null)
setup: cwd: $here/16/.git/wt
setup: prefix: (null)
EOF
	test_repo 16/.git/wt
'

test_expect_success '#16.1: in .git/wt/sub' '
	cat >16/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/16/.git
setup: worktree: (null)
setup: cwd: $here/16/.git/wt/sub
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
	git config --file="$here/16/.git/config" core.bare true
'

test_expect_success '#16.2: at .git' '
	cat >16/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/16/.git
setup: prefix: (null)
EOF
	test_repo 16/.git
'

test_expect_success '#16.2: in .git/wt' '
	cat >16/.git/wt/expected <<EOF &&
setup: git_dir: $here/16/.git
setup: worktree: (null)
setup: cwd: $here/16/.git/wt
setup: prefix: (null)
EOF
	test_repo 16/.git/wt
'

test_expect_success '#16.2: in .git/wt/sub' '
	cat >16/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/16/.git
setup: worktree: (null)
setup: cwd: $here/16/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 16/.git/wt/sub
'

test_expect_success '#16.2: at root' '
	cat >16/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $here/16
setup: prefix: (null)
EOF
	test_repo 16
'

test_expect_success '#16.2: in subdir' '
	cat >16/sub/expected <<EOF &&
setup: git_dir: $here/16/.git
setup: worktree: (null)
setup: cwd: $here/16/sub
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 17 17/sub &&
	cd 17 &&
	git init &&
	mkdir .git/wt .git/wt/sub &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	cd ..
'

test_expect_success '#17.1: at .git' '
	cat >17/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/17/.git
setup: prefix: (null)
EOF
	test_repo 17/.git
'

test_expect_success '#17.1: in .git/wt' '
	cat >17/.git/wt/expected <<EOF &&
setup: git_dir: $here/17/.git
setup: worktree: (null)
setup: cwd: $here/17/.git/wt
setup: prefix: (null)
EOF
	test_repo 17/.git/wt
'

test_expect_success '#17.1: in .git/wt/sub' '
	cat >17/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/17/.git
setup: worktree: (null)
setup: cwd: $here/17/.git/wt/sub
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
	git config --file="$here/17/.git/config" core.bare true
'

test_expect_success '#17.2: at .git' '
	cat >17/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/17/.git
setup: prefix: (null)
EOF
	test_repo 17/.git
'

test_expect_success '#17.2: in .git/wt' '
	cat >17/.git/wt/expected <<EOF &&
setup: git_dir: $here/17/.git
setup: worktree: (null)
setup: cwd: $here/17/.git/wt
setup: prefix: (null)
EOF
	test_repo 17/.git/wt
'

test_expect_success '#17.2: in .git/wt/sub' '
	cat >17/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/17/.git
setup: worktree: (null)
setup: cwd: $here/17/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 17/.git/wt/sub
'

test_expect_success '#17.2: at root' '
	cat >17/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $here/17
setup: prefix: (null)
EOF
	test_repo 17
'

test_expect_success '#17.2: in subdir' '
	cat >17/sub/expected <<EOF &&
setup: git_dir: $here/17/.git
setup: worktree: (null)
setup: cwd: $here/17/sub
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
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
setup: cwd: $here/18
setup: prefix: (null)
EOF
	 test_repo 18 .git
'

test_expect_success '#18: at root' '
	cat >18/expected <<EOF &&
setup: git_dir: $here/18/.git
setup: worktree: (null)
setup: cwd: $here/18
setup: prefix: (null)
EOF
	 test_repo 18 "$here/18/.git"
'

test_expect_success '#18: (rel) in subdir' '
	cat >18/sub/expected <<EOF &&
setup: git_dir: ../.git
setup: worktree: (null)
setup: cwd: $here/18/sub
setup: prefix: (null)
EOF
	test_repo 18/sub ../.git
'

test_expect_success '#18: in subdir' '
	cat >18/sub/expected <<EOF &&
setup: git_dir: $here/18/.git
setup: worktree: (null)
setup: cwd: $here/18/sub
setup: prefix: (null)
EOF
	test_repo 18/sub "$here/18/.git"
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
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 19 19/sub 19/sub/sub 19.wt 19.wt/sub 19/wt 19/wt/sub &&
	cd 19 &&
	git init &&
	git config core.bare true &&
	cd ..
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 .git "$here/19"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 .git .
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 "$here/19/.git" "$here/19"
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 "$here/19/.git" .
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: sub/sub/
EOF
	test_repo 19/sub/sub ../../.git "$here/19"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: sub/sub/
EOF
	test_repo 19/sub/sub ../../.git ../..
'

test_expect_success '#19: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >19/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: sub/
EOF
	test_repo 19/sub "$here/19/.git" "$here/19"
'

test_expect_success '#19: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19
setup: cwd: $here/19
setup: prefix: sub/sub/
EOF
	test_repo 19/sub/sub "$here/19/.git" ../..
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/19/wt
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 .git "$here/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/19/wt
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 .git wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19/wt
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 "$here/19/.git" wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19/wt
setup: cwd: $here/19
setup: prefix: (null)
EOF
	test_repo 19 "$here/19/.git" "$here/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/19/wt
setup: cwd: $here/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub ../../.git "$here/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/19/wt
setup: cwd: $here/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub ../../.git ../../wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19/wt
setup: cwd: $here/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub "$here/19/.git" ../../wt
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here/19/wt
setup: cwd: $here/19/sub/sub
setup: prefix: (null)
EOF
	test_repo 19/sub/sub "$here/19/.git" "$here/19/wt"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/
EOF
	test_repo 19 .git "$here"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/
EOF
	test_repo 19 .git ..
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/
EOF
	test_repo 19 "$here/19/.git" ..
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >19/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/
EOF
	test_repo 19 "$here/19/.git" "$here"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub ../../.git "$here"
'

test_expect_success '#19: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub ../../.git ../../..
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub "$here/19/.git" ../../../
'

test_expect_success '#19: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >19/sub/sub/expected <<EOF &&
setup: git_dir: $here/19/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 19/sub/sub/
EOF
	test_repo 19/sub/sub "$here/19/.git" "$here"
'

#
# case #20.1
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a directory
#  - cwd is inside .git
#
# Output:
#
# core.worktree is ignored -> #16.1

test_expect_success '#20.1: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 20 20/sub &&
	cd 20 &&
	git init &&
	git config core.worktree non-existent &&
	mkdir .git/wt .git/wt/sub &&
	cd ..
'

test_expect_success '#20.1: at .git' '
	cat >20/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/20/.git
setup: prefix: (null)
EOF
	test_repo 20/.git
'

test_expect_success '#20.1: in .git/wt' '
	cat >20/.git/wt/expected <<EOF &&
setup: git_dir: $here/20/.git
setup: worktree: (null)
setup: cwd: $here/20/.git/wt
setup: prefix: (null)
EOF
	test_repo 20/.git/wt
'

test_expect_success '#20.1: in .git/wt/sub' '
	cat >20/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/20/.git
setup: worktree: (null)
setup: cwd: $here/20/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 20/.git/wt/sub
'

#
# case #20.2
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
# core.worktree is ignored -> #16.2

test_expect_success '#20.2: setup' '
	git config --file="$here/20/.git/config" core.bare true
'

test_expect_success '#20.2: at .git' '
	cat >20/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/20/.git
setup: prefix: (null)
EOF
	test_repo 20/.git
'

test_expect_success '#20.2: in .git/wt' '
	cat >20/.git/wt/expected <<EOF &&
setup: git_dir: $here/20/.git
setup: worktree: (null)
setup: cwd: $here/20/.git/wt
setup: prefix: (null)
EOF
	test_repo 20/.git/wt
'

test_expect_success '#20.2: in .git/wt/sub' '
	cat >20/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/20/.git
setup: worktree: (null)
setup: cwd: $here/20/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 20/.git/wt/sub
'

test_expect_success '#20.2: at root' '
	cat >20/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $here/20
setup: prefix: (null)
EOF
	test_repo 20
'

test_expect_success '#20.2: in subdir' '
	cat >20/sub/expected <<EOF &&
setup: git_dir: $here/20/.git
setup: worktree: (null)
setup: cwd: $here/20/sub
setup: prefix: (null)
EOF
	test_repo 20/sub
'

#
# case #21.1
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a directory
#  - cwd is inside .git
#
# Output:
#
# GIT_WORK_TREE/core.worktree are ignored -> #20.1

test_expect_success '#21.1: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 21 21/sub &&
	cd 21 &&
	git init &&
	git config core.worktree non-existent &&
	GIT_WORK_TREE=non-existent-too &&
	export GIT_WORK_TREE &&
	mkdir .git/wt .git/wt/sub &&
	cd ..
'

test_expect_success '#21.1: at .git' '
	cat >21/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/21/.git
setup: prefix: (null)
EOF
	test_repo 21/.git
'

test_expect_success '#21.1: in .git/wt' '
	cat >21/.git/wt/expected <<EOF &&
setup: git_dir: $here/21/.git
setup: worktree: (null)
setup: cwd: $here/21/.git/wt
setup: prefix: (null)
EOF
	test_repo 21/.git/wt
'

test_expect_success '#21.1: in .git/wt/sub' '
	cat >21/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/21/.git
setup: worktree: (null)
setup: cwd: $here/21/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 21/.git/wt/sub
'

#
# case #21.2
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
# GIT_WORK_TREE/core.worktree are ignored -> #20.2

test_expect_success '#21.2: setup' '
	git config --file="$here/21/.git/config" core.bare true
'

test_expect_success '#21.2: at .git' '
	cat >21/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: (null)
setup: cwd: $here/21/.git
setup: prefix: (null)
EOF
	test_repo 21/.git
'

test_expect_success '#21.2: in .git/wt' '
	cat >21/.git/wt/expected <<EOF &&
setup: git_dir: $here/21/.git
setup: worktree: (null)
setup: cwd: $here/21/.git/wt
setup: prefix: (null)
EOF
	test_repo 21/.git/wt
'

test_expect_success '#21.2: in .git/wt/sub' '
	cat >21/.git/wt/sub/expected <<EOF &&
setup: git_dir: $here/21/.git
setup: worktree: (null)
setup: cwd: $here/21/.git/wt/sub
setup: prefix: (null)
EOF
	test_repo 21/.git/wt/sub
'

test_expect_success '#21.2: at root' '
	cat >21/expected <<EOF &&
setup: git_dir: .git
setup: worktree: (null)
setup: cwd: $here/21
setup: prefix: (null)
EOF
	test_repo 21
'

test_expect_success '#21.2: in subdir' '
	cat >21/sub/expected <<EOF &&
setup: git_dir: $here/21/.git
setup: worktree: (null)
setup: cwd: $here/21/sub
setup: prefix: (null)
EOF
	test_repo 21/sub
'

#
# case #22.1
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a directory
#  - cwd is inside .git
#
# Output:
#
# bare attribute is ignored
#
#  - worktree is at core.worktree
#  - cwd is at worktree root
#  - prefix is calculated
#  - git_dir is at $GIT_DIR
#  - cwd can be outside worktree

test_expect_success '#22.1: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 22 &&
	cd 22 &&
	git init &&
	mkdir .git/sub .git/wt .git/wt/sub &&
	cd ..
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=. at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git" &&
	test_repo 22/.git .
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=.(rel) at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree . &&
	test_repo 22/.git .
'

test_expect_success '#22.1: GIT_DIR, core.worktree=. at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git" &&
	test_repo 22/.git "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR, core.worktree=.(rel) at root' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree . &&
	test_repo 22/.git "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=. in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: sub/
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git" &&
	test_repo 22/.git/sub ..
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=.(rel) in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: sub/
EOF
	git config --file="$here/22/.git/config" core.worktree . &&
	test_repo 22/.git/sub/ ..
'

test_expect_success '#22.1: GIT_DIR, core.worktree=. in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: sub/
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git" &&
	test_repo 22/.git/sub "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR, core.worktree=.(rel) in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git
setup: cwd: $here/22/.git
setup: prefix: sub/
EOF
	git config --file="$here/22/.git/config" core.worktree . &&
	test_repo 22/.git/sub "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=wt at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git/wt" &&
	test_repo 22/.git .
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=wt(rel) at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: .
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree wt &&
	test_repo 22/.git .
'

test_expect_success '#22.1: GIT_DIR, core.worktree=wt(rel) at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree wt &&
	test_repo 22/.git "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR, core.worktree=wt at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git/wt" &&
	test_repo 22/.git "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=wt in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: ..
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git/sub
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git/wt" &&
	test_repo 22/.git/sub ..
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=wt(rel) in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: ..
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git/sub
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree wt &&
	test_repo 22/.git/sub ..
'

test_expect_success '#22.1: GIT_DIR, core.worktree=wt(rel) in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git/sub
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree wt &&
	test_repo 22/.git/sub "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR, core.worktree=wt in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22/.git/wt
setup: cwd: $here/22/.git/sub
setup: prefix: (null)
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22/.git/wt" &&
	test_repo 22/.git/sub "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=.. at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22" &&
	test_repo 22/.git .
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=..(rel) at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/
EOF
	git config --file="$here/22/.git/config" core.worktree .. &&
	test_repo 22/.git .
'

test_expect_success '#22.1: GIT_DIR, core.worktree=..(rel) at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/
EOF
	git config --file="$here/22/.git/config" core.worktree .. &&
	test_repo 22/.git "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR, core.worktree=.. at .git' '
	cat >22/.git/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22" &&
	test_repo 22/.git "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=.. in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/sub/
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22" &&
	test_repo 22/.git/sub ..
'

test_expect_success '#22.1: GIT_DIR(rel), core.worktree=..(rel) in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/sub/
EOF
	git config --file="$here/22/.git/config" core.worktree .. &&
	test_repo 22/.git/sub ..
'

test_expect_success '#22.1: GIT_DIR, core.worktree=..(rel) in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/sub/
EOF
	git config --file="$here/22/.git/config" core.worktree .. &&
	test_repo 22/.git/sub "$here/22/.git"
'

test_expect_success '#22.1: GIT_DIR, core.worktree=.. in .git/sub' '
	cat >22/.git/sub/expected <<EOF &&
setup: git_dir: $here/22/.git
setup: worktree: $here/22
setup: cwd: $here/22
setup: prefix: .git/sub/
EOF
	git config --file="$here/22/.git/config" core.worktree "$here/22" &&
	test_repo 22/.git/sub "$here/22/.git"
'

#
# case #22.2
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
# core.worktree and core.bare conflict, won't fly.

test_expect_success '#22.2: setup' '
	git config --file="$here/22/.git/config" core.bare true
'

test_expect_success '#22.2: at .git' '
	(
	cd 22/.git &&
	GIT_DIR=. &&
	export GIT_DIR &&
	test_must_fail git symbolic-ref HEAD 2>result &&
	grep "core.bare and core.worktree do not make sense" result
	)
'

test_expect_success '#22.2: at root' '
	(
	cd 22 &&
	GIT_DIR=.git &&
	export GIT_DIR &&
	test_must_fail git symbolic-ref HEAD 2>result &&
	grep "core.bare and core.worktree do not make sense" result
	)
'

#
# case #23
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a directory
#  - core.bare is set
#
# Output:
#
# core.worktree is overridden by GIT_WORK_TREE -> #19

test_expect_success '#23: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 23 23/sub 23/sub/sub 23.wt 23.wt/sub 23/wt 23/wt/sub &&
	cd 23 &&
	git init &&
	git config core.bare true &&
	git config core.worktree non-existent &&
	cd ..
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >23/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 .git "$here/23"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >23/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 .git .
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 "$here/23/.git" "$here/23"
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 "$here/23/.git" .
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: sub/sub/
EOF
	test_repo 23/sub/sub ../../.git "$here/23"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: sub/sub/
EOF
	test_repo 23/sub/sub ../../.git ../..
'

test_expect_success '#23: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >23/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: sub/
EOF
	test_repo 23/sub "$here/23/.git" "$here/23"
'

test_expect_success '#23: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23
setup: cwd: $here/23
setup: prefix: sub/sub/
EOF
	test_repo 23/sub/sub "$here/23/.git" ../..
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >23/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/23/wt
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 .git "$here/23/wt"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >23/expected <<EOF &&
setup: git_dir: .git
setup: worktree: $here/23/wt
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 .git wt
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23/wt
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 "$here/23/.git" wt
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23/wt
setup: cwd: $here/23
setup: prefix: (null)
EOF
	test_repo 23 "$here/23/.git" "$here/23/wt"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/23/wt
setup: cwd: $here/23/sub/sub
setup: prefix: (null)
EOF
	test_repo 23/sub/sub ../../.git "$here/23/wt"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: ../../.git
setup: worktree: $here/23/wt
setup: cwd: $here/23/sub/sub
setup: prefix: (null)
EOF
	test_repo 23/sub/sub ../../.git ../../wt
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23/wt
setup: cwd: $here/23/sub/sub
setup: prefix: (null)
EOF
	test_repo 23/sub/sub "$here/23/.git" ../../wt
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here/23/wt
setup: cwd: $here/23/sub/sub
setup: prefix: (null)
EOF
	test_repo 23/sub/sub "$here/23/.git" "$here/23/wt"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/
EOF
	test_repo 23 .git "$here"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/
EOF
	test_repo 23 .git ..
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/
EOF
	test_repo 23 "$here/23/.git" ..
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >23/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/
EOF
	test_repo 23 "$here/23/.git" "$here"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/sub/sub/
EOF
	test_repo 23/sub/sub ../../.git "$here"
'

test_expect_success '#23: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/sub/sub/
EOF
	test_repo 23/sub/sub ../../.git ../../..
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/sub/sub/
EOF
	test_repo 23/sub/sub "$here/23/.git" ../../../
'

test_expect_success '#23: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >23/sub/sub/expected <<EOF &&
setup: git_dir: $here/23/.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 23/sub/sub/
EOF
	test_repo 23/sub/sub "$here/23/.git" "$here"
'

#
# case #24
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# #16.2 except git_dir is set according to .git file

test_expect_success '#24: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 24 24/sub &&
	cd 24 &&
	git init &&
	git config core.bare true &&
	mv .git ../24.git &&
	echo gitdir: ../24.git >.git &&
	cd ..
'

test_expect_success '#24: at root' '
	cat >24/expected <<EOF &&
setup: git_dir: $here/24.git
setup: worktree: (null)
setup: cwd: $here/24
setup: prefix: (null)
EOF
	test_repo 24
'

test_expect_success '#24: in subdir' '
	cat >24/sub/expected <<EOF &&
setup: git_dir: $here/24.git
setup: worktree: (null)
setup: cwd: $here/24/sub
setup: prefix: (null)
EOF
	test_repo 24/sub
'

#
# case #25
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# #17.2 except git_dir is set according to .git file

test_expect_success '#25: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 25 25/sub &&
	cd 25 &&
	git init &&
	git config core.bare true &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	mv .git ../25.git &&
	echo gitdir: ../25.git >.git &&
	cd ..
'

test_expect_success '#25: at root' '
	cat >25/expected <<EOF &&
setup: git_dir: $here/25.git
setup: worktree: (null)
setup: cwd: $here/25
setup: prefix: (null)
EOF
	test_repo 25
'

test_expect_success '#25: in subdir' '
	cat >25/sub/expected <<EOF &&
setup: git_dir: $here/25.git
setup: worktree: (null)
setup: cwd: $here/25/sub
setup: prefix: (null)
EOF
	test_repo 25/sub
'

#
# case #26
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is not set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# #18 except git_dir is set according to .git file

test_expect_success '#26: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 26 26/sub &&
	cd 26 &&
	git init &&
	git config core.bare true &&
	mv .git ../26.git &&
	echo gitdir: ../26.git >.git &&
	cd ..
'

test_expect_success '#26: (rel) at root' '
	cat >26/expected <<EOF &&
setup: git_dir: $here/26.git
setup: worktree: (null)
setup: cwd: $here/26
setup: prefix: (null)
EOF
	 test_repo 26 .git
'

test_expect_success '#26: at root' '
	cat >26/expected <<EOF &&
setup: git_dir: $here/26.git
setup: worktree: (null)
setup: cwd: $here/26
setup: prefix: (null)
EOF
	 test_repo 26 "$here/26/.git"
'

test_expect_success '#26: (rel) in subdir' '
	cat >26/sub/expected <<EOF &&
setup: git_dir: $here/26.git
setup: worktree: (null)
setup: cwd: $here/26/sub
setup: prefix: (null)
EOF
	test_repo 26/sub ../.git
'

test_expect_success '#26: in subdir' '
	cat >26/sub/expected <<EOF &&
setup: git_dir: $here/26.git
setup: worktree: (null)
setup: cwd: $here/26/sub
setup: prefix: (null)
EOF
	test_repo 26/sub "$here/26/.git"
'

#
# case #27
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - .git is a file
#  - core.worktree is not set
#  - core.bare is set
#
# Output:
#
# #19 except git_dir is set according to .git file

test_expect_success '#27: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 27 27/sub 27/sub/sub 27.wt 27.wt/sub 27/wt 27/wt/sub &&
	cd 27 &&
	git init &&
	git config core.bare true &&
	mv .git ../27.git &&
	echo gitdir: ../27.git >.git &&
	cd ..
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 .git "$here/27"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 .git .
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 "$here/27/.git" "$here/27"
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 "$here/27/.git" .
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: sub/sub/
EOF
	test_repo 27/sub/sub ../../.git "$here/27"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: sub/sub/
EOF
	test_repo 27/sub/sub ../../.git ../..
'

test_expect_success '#27: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >27/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: sub/
EOF
	test_repo 27/sub "$here/27/.git" "$here/27"
'

test_expect_success '#27: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27
setup: cwd: $here/27
setup: prefix: sub/sub/
EOF
	test_repo 27/sub/sub "$here/27/.git" ../..
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 .git "$here/27/wt"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 .git wt
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 "$here/27/.git" wt
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27
setup: prefix: (null)
EOF
	test_repo 27 "$here/27/.git" "$here/27/wt"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27/sub/sub
setup: prefix: (null)
EOF
	test_repo 27/sub/sub ../../.git "$here/27/wt"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27/sub/sub
setup: prefix: (null)
EOF
	test_repo 27/sub/sub ../../.git ../../wt
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27/sub/sub
setup: prefix: (null)
EOF
	test_repo 27/sub/sub "$here/27/.git" ../../wt
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here/27/wt
setup: cwd: $here/27/sub/sub
setup: prefix: (null)
EOF
	test_repo 27/sub/sub "$here/27/.git" "$here/27/wt"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/
EOF
	test_repo 27 .git "$here"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/
EOF
	test_repo 27 .git ..
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/
EOF
	test_repo 27 "$here/27/.git" ..
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >27/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/
EOF
	test_repo 27 "$here/27/.git" "$here"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/sub/sub/
EOF
	test_repo 27/sub/sub ../../.git "$here"
'

test_expect_success '#27: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/sub/sub/
EOF
	test_repo 27/sub/sub ../../.git ../../..
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/sub/sub/
EOF
	test_repo 27/sub/sub "$here/27/.git" ../../../
'

test_expect_success '#27: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >27/sub/sub/expected <<EOF &&
setup: git_dir: $here/27.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 27/sub/sub/
EOF
	test_repo 27/sub/sub "$here/27/.git" "$here"
'

#
# case #28
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# core.worktree is ignored -> #24

test_expect_success '#28: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 28 28/sub &&
	cd 28 &&
	git init &&
	git config core.bare true &&
	git config core.worktree non-existent &&
	mv .git ../28.git &&
	echo gitdir: ../28.git >.git &&
	cd ..
'

test_expect_success '#28: at root' '
	cat >28/expected <<EOF &&
setup: git_dir: $here/28.git
setup: worktree: (null)
setup: cwd: $here/28
setup: prefix: (null)
EOF
	test_repo 28
'

test_expect_success '#28: in subdir' '
	cat >28/sub/expected <<EOF &&
setup: git_dir: $here/28.git
setup: worktree: (null)
setup: cwd: $here/28/sub
setup: prefix: (null)
EOF
	test_repo 28/sub
'

#
# case #29
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is not set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# GIT_WORK_TREE/core.worktree are ignored -> #28

test_expect_success '#29: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 29 29/sub &&
	cd 29 &&
	git init &&
	git config core.bare true &&
	GIT_WORK_TREE=non-existent &&
	export GIT_WORK_TREE &&
	mv .git ../29.git &&
	echo gitdir: ../29.git >.git &&
	cd ..
'

test_expect_success '#29: at root' '
	cat >29/expected <<EOF &&
setup: git_dir: $here/29.git
setup: worktree: (null)
setup: cwd: $here/29
setup: prefix: (null)
EOF
	test_repo 29
'

test_expect_success '#29: in subdir' '
	cat >29/sub/expected <<EOF &&
setup: git_dir: $here/29.git
setup: worktree: (null)
setup: cwd: $here/29/sub
setup: prefix: (null)
EOF
	test_repo 29/sub
'

#
# case #30
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is not set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# core.worktree and core.bare conflict, won't fly.

test_expect_success '#30: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 30 &&
	cd 30 &&
	git init &&
	git config core.bare true &&
	git config core.worktree non-existent &&
	mv .git ../30.git &&
	echo gitdir: ../30.git >.git &&
	cd ..
'

test_expect_success '#30: at root' '
	(
	cd 30 &&
	GIT_DIR=.git &&
	export GIT_DIR &&
	test_must_fail git symbolic-ref HEAD 2>result &&
	grep "core.bare and core.worktree do not make sense" result
	)
'

#
# case #31
#
############################################################
#
# Input:
#
#  - GIT_WORK_TREE is set
#  - GIT_DIR is set
#  - core.worktree is set
#  - .git is a file
#  - core.bare is set
#
# Output:
#
# #23 except git_dir is set according to .git file

test_expect_success '#31: setup' '
	sane_unset GIT_DIR GIT_WORK_TREE &&
	mkdir 31 31/sub 31/sub/sub 31.wt 31.wt/sub 31/wt 31/wt/sub &&
	cd 31 &&
	git init &&
	git config core.bare true &&
	git config core.worktree non-existent &&
	mv .git ../31.git &&
	echo gitdir: ../31.git >.git &&
	cd ..
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=root at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 .git "$here/31"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=root(rel) at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 .git .
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=root at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 "$here/31/.git" "$here/31"
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=root(rel) at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 "$here/31/.git" .
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORKTREE=root in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: sub/sub/
EOF
	test_repo 31/sub/sub ../../.git "$here/31"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORKTREE=root(rel) in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: sub/sub/
EOF
	test_repo 31/sub/sub ../../.git ../..
'

test_expect_success '#31: GIT_DIR, GIT_WORKTREE=root in subdir' '
	cat >31/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: sub/
EOF
	test_repo 31/sub "$here/31/.git" "$here/31"
'

test_expect_success '#31: GIT_DIR, GIT_WORKTREE=root(rel) in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31
setup: cwd: $here/31
setup: prefix: sub/sub/
EOF
	test_repo 31/sub/sub "$here/31/.git" ../..
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=wt at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 .git "$here/31/wt"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 .git wt
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=wt(rel) at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 "$here/31/.git" wt
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=wt at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31
setup: prefix: (null)
EOF
	test_repo 31 "$here/31/.git" "$here/31/wt"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=wt in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31/sub/sub
setup: prefix: (null)
EOF
	test_repo 31/sub/sub ../../.git "$here/31/wt"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=wt(rel) in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31/sub/sub
setup: prefix: (null)
EOF
	test_repo 31/sub/sub ../../.git ../../wt
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=wt(rel) in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31/sub/sub
setup: prefix: (null)
EOF
	test_repo 31/sub/sub "$here/31/.git" ../../wt
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=wt in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here/31/wt
setup: cwd: $here/31/sub/sub
setup: prefix: (null)
EOF
	test_repo 31/sub/sub "$here/31/.git" "$here/31/wt"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=.. at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/
EOF
	test_repo 31 .git "$here"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=..(rel) at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/
EOF
	test_repo 31 .git ..
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=..(rel) at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/
EOF
	test_repo 31 "$here/31/.git" ..
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=.. at root' '
	cat >31/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/
EOF
	test_repo 31 "$here/31/.git" "$here"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=.. in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/sub/sub/
EOF
	test_repo 31/sub/sub ../../.git "$here"
'

test_expect_success '#31: GIT_DIR(rel), GIT_WORK_TREE=..(rel) in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/sub/sub/
EOF
	test_repo 31/sub/sub ../../.git ../../..
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=..(rel) in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/sub/sub/
EOF
	test_repo 31/sub/sub "$here/31/.git" ../../../
'

test_expect_success '#31: GIT_DIR, GIT_WORK_TREE=.. in subdir' '
	cat >31/sub/sub/expected <<EOF &&
setup: git_dir: $here/31.git
setup: worktree: $here
setup: cwd: $here
setup: prefix: 31/sub/sub/
EOF
	test_repo 31/sub/sub "$here/31/.git" "$here"
'

test_done
