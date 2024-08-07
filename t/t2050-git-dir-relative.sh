#!/bin/sh

test_description='check problems with relative GIT_DIR

This test creates a working tree state with a file and subdir:

  top (committed several times)
  subdir (a subdirectory)

It creates a commit-hook and tests it, then moves .git
into the subdir while keeping the worktree location,
and tries commits from the top and the subdir, checking
that the commit-hook still gets called.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

COMMIT_FILE="$(pwd)/output"
export COMMIT_FILE

test_expect_success 'Setting up post-commit hook' '
mkdir -p .git/hooks &&
echo >.git/hooks/post-commit "#!/bin/sh
touch \"\${COMMIT_FILE}\"
echo Post commit hook was called." &&
chmod +x .git/hooks/post-commit'

test_expect_success 'post-commit hook used ordinarily' '
echo initial >top &&
git add top &&
git commit -m initial &&
test -r "${COMMIT_FILE}"
'

rm -rf "${COMMIT_FILE}"
mkdir subdir
mv .git subdir

test_expect_success 'post-commit-hook created and used from top dir' '
echo changed >top &&
git --git-dir subdir/.git add top &&
git --git-dir subdir/.git commit -m topcommit &&
test -r "${COMMIT_FILE}"
'

rm -rf "${COMMIT_FILE}"

test_expect_success 'post-commit-hook from sub dir' '
echo changed again >top &&
cd subdir &&
git --git-dir .git --work-tree .. add ../top &&
git --git-dir .git --work-tree .. commit -m subcommit &&
test -r "${COMMIT_FILE}"
'

test_done
