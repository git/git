#!/bin/sh

test_description='read-tree -u --reset'

. ./test-lib.sh

# two-tree test

test_expect_success 'setup' '
  git init &&
  mkdir df &&
  echo content >df/file &&
  git add df/file &&
  git commit -m one &&
  git ls-files >expect &&
  rm -rf df &&
  echo content >df &&
  git add df &&
  echo content >new &&
  git add new &&
  git commit -m two
'

test_expect_success 'reset should work' '
  git read-tree -u --reset HEAD^ &&
  git ls-files >actual &&
  test_cmp expect actual
'

test_expect_success 'reset should remove remnants from a failed merge' '
  git read-tree --reset -u HEAD &&
  git ls-files -s >expect &&
  sha1=$(git rev-parse :new) &&
  (
	echo "100644 $sha1 1	old"
	echo "100644 $sha1 3	old"
  ) | git update-index --index-info &&
  >old &&
  git ls-files -s &&
  git read-tree --reset -u HEAD &&
  git ls-files -s >actual &&
  ! test -f old
'

test_expect_success 'Porcelain reset should remove remnants too' '
  git read-tree --reset -u HEAD &&
  git ls-files -s >expect &&
  sha1=$(git rev-parse :new) &&
  (
	echo "100644 $sha1 1	old"
	echo "100644 $sha1 3	old"
  ) | git update-index --index-info &&
  >old &&
  git ls-files -s &&
  git reset --hard &&
  git ls-files -s >actual &&
  ! test -f old
'

test_expect_success 'Porcelain checkout -f should remove remnants too' '
  git read-tree --reset -u HEAD &&
  git ls-files -s >expect &&
  sha1=$(git rev-parse :new) &&
  (
	echo "100644 $sha1 1	old"
	echo "100644 $sha1 3	old"
  ) | git update-index --index-info &&
  >old &&
  git ls-files -s &&
  git checkout -f &&
  git ls-files -s >actual &&
  ! test -f old
'

test_expect_success 'Porcelain checkout -f HEAD should remove remnants too' '
  git read-tree --reset -u HEAD &&
  git ls-files -s >expect &&
  sha1=$(git rev-parse :new) &&
  (
	echo "100644 $sha1 1	old"
	echo "100644 $sha1 3	old"
  ) | git update-index --index-info &&
  >old &&
  git ls-files -s &&
  git checkout -f HEAD &&
  git ls-files -s >actual &&
  ! test -f old
'

test_done
