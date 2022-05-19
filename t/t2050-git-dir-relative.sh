#!/bin/sh

test_description='check problems with relative BUT_DIR

This test creates a working tree state with a file and subdir:

  top (cummitted several times)
  subdir (a subdirectory)

It creates a commit-hook and tests it, then moves .but
into the subdir while keeping the worktree location,
and tries cummits from the top and the subdir, checking
that the commit-hook still gets called.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

CUMMIT_FILE="$(pwd)/output"
export CUMMIT_FILE

test_expect_success 'Setting up post-commit hook' '
mkdir -p .but/hooks &&
echo >.but/hooks/post-cummit "#!/bin/sh
touch \"\${CUMMIT_FILE}\"
echo Post commit hook was called." &&
chmod +x .but/hooks/post-cummit'

test_expect_success 'post-commit hook used ordinarily' '
echo initial >top &&
but add top &&
but cummit -m initial &&
test -r "${CUMMIT_FILE}"
'

rm -rf "${CUMMIT_FILE}"
mkdir subdir
mv .but subdir

test_expect_success 'post-commit-hook created and used from top dir' '
echo changed >top &&
but --but-dir subdir/.but add top &&
but --but-dir subdir/.but cummit -m topcummit &&
test -r "${CUMMIT_FILE}"
'

rm -rf "${CUMMIT_FILE}"

test_expect_success 'post-commit-hook from sub dir' '
echo changed again >top &&
cd subdir &&
but --but-dir .but --work-tree .. add ../top &&
but --but-dir .but --work-tree .. cummit -m subcummit &&
test -r "${CUMMIT_FILE}"
'

test_done
