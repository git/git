#!/bin/sh

test_description='check problems with relative GIT_DIR

This test creates a working tree state with a file and subdir:

  top (cummitted several times)
  subdir (a subdirectory)

It creates a commit-hook and tests it, then moves .git
into the subdir while keeping the worktree location,
and tries cummits from the top and the subdir, checking
that the commit-hook still gets called.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

cummit_FILE="$(pwd)/output"
export cummit_FILE

test_expect_success 'Setting up post-commit hook' '
mkdir -p .git/hooks &&
echo >.git/hooks/post-cummit "#!/bin/sh
touch \"\${cummit_FILE}\"
echo Post commit hook was called." &&
chmod +x .git/hooks/post-cummit'

test_expect_success 'post-commit hook used ordinarily' '
echo initial >top &&
git add top &&
git cummit -m initial &&
test -r "${cummit_FILE}"
'

rm -rf "${cummit_FILE}"
mkdir subdir
mv .git subdir

test_expect_success 'post-commit-hook created and used from top dir' '
echo changed >top &&
git --git-dir subdir/.git add top &&
git --git-dir subdir/.git cummit -m topcummit &&
test -r "${cummit_FILE}"
'

rm -rf "${cummit_FILE}"

test_expect_success 'post-commit-hook from sub dir' '
echo changed again >top &&
cd subdir &&
git --git-dir .git --work-tree .. add ../top &&
git --git-dir .git --work-tree .. cummit -m subcummit &&
test -r "${cummit_FILE}"
'

test_done
