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
	if test -n "$1"; then cd "$1"; fi &&
	if test -f trace; then rm trace; fi &&
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

test_done
