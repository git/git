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
  diff -u expect actual
'

test_done
