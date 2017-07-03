#!/bin/sh

test_description='git merge --signoff

This test runs git merge --signoff and makes sure that it works.
'

. ./test-lib.sh

# Setup test files
test_setup () {
  # Expected commit message after merge --signoff
  printf "Merge branch 'master' into other-branch\n\n" >expected-signed &&
  printf "Signed-off-by: $(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/")\n" >>expected-signed &&

  # Expected commit message after merge without --signoff (or with --no-signoff)
  echo "Merge branch 'master' into other-branch" >expected-unsigned &&

  # Initial commit and feature branch to merge master into it.
  git commit --allow-empty -m "Initial empty commit" &&
  git checkout -b other-branch &&
  test_commit other-branch file1 1
}

# Create fake editor that just copies file
create_fake_editor () {
  echo 'cp "$1" "$1.saved"' | write_script save-editor
}

test_expect_success 'setup' '
  test_setup && create_fake_editor
'

test_expect_success 'git merge --signoff adds a sign-off line' '
  git checkout master &&
  test_commit master-branch-2 file2 2 &&
  git checkout other-branch &&
  git merge master --signoff --no-edit &&
  git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
  test_cmp expected-signed actual
'

test_expect_success 'git merge does not add a sign-off line' '
  git checkout master &&
  test_commit master-branch-3 file3 3 &&
  git checkout other-branch &&
  git merge master --no-edit &&
  git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
  test_cmp expected-unsigned actual
'

test_expect_success 'git merge --no-signoff flag cancels --signoff flag' '
  git checkout master &&
  test_commit master-branch-4 file4 4 &&
  git checkout other-branch &&
  git merge master --no-edit --signoff --no-signoff &&
  git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
  test_cmp expected-unsigned actual
'

test_expect_success 'git merge --signoff adds S-o-b line in commit message editor' '
  git checkout master &&
  test_commit master-branch-5 file5 5 &&
  git checkout other-branch &&
  GIT_EDITOR=./save-editor git merge master -m "My Message" --edit --signoff &&
  test_i18ngrep "^My Message" .git/MERGE_MSG.saved &&
  test_i18ngrep "^Signed-off-by: " .git/MERGE_MSG.saved
'

test_expect_success 'git merge --no-signoff does not add S-o-b line in commit message editor' '
  git checkout master &&
  test_commit master-branch-6 file6 6 &&
  git checkout other-branch &&
  GIT_EDITOR=./save-editor git merge master -m "My Message" --edit --no-signoff &&
  test_i18ngrep "^My Message" .git/MERGE_MSG.saved &&
  test_i18ngrep ! "^Signed-off-by: " .git/MERGE_MSG.saved
'

test_expect_success 'git merge --no-signoff cancels --signoff flag in commit message editor' '
  git checkout master &&
  test_commit master-branch-7 file7 7 &&
  git checkout other-branch &&
  GIT_EDITOR=./save-editor git merge master -m "My Message" --edit --signoff --no-signoff &&
  test_i18ngrep "^My Message" .git/MERGE_MSG.saved &&
  test_i18ngrep ! "^Signed-off-by: " .git/MERGE_MSG.saved
'

test_done
