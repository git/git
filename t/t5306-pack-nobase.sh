#!/bin/sh
#
# Copyright (c) 2008 Google Inc.
#

test_description='but-pack-object with missing base

'
. ./test-lib.sh

# Create A-B chain
#
test_expect_success \
    'setup base' \
    'test_write_lines a b c d e f g h i >text &&
     echo side >side &&
     but update-index --add text side &&
     A=$(echo A | but cummit-tree $(but write-tree)) &&

     echo m >>text &&
     but update-index text &&
     B=$(echo B | but cummit-tree $(but write-tree) -p $A) &&
     but update-ref HEAD $B
    '

# Create repository with C whose parent is B.
# Repository contains C, C^{tree}, C:text, B, B^{tree}.
# Repository is missing B:text (best delta base for C:text).
# Repository is missing A (parent of B).
# Repository is missing A:side.
#
test_expect_success \
    'setup patch_clone' \
    'base_objects=$(pwd)/.but/objects &&
     (mkdir patch_clone &&
      cd patch_clone &&
      but init &&
      echo "$base_objects" >.but/objects/info/alternates &&
      echo q >>text &&
      but read-tree $B &&
      but update-index text &&
      but update-ref HEAD $(echo C | but cummit-tree $(but write-tree) -p $B) &&
      rm .but/objects/info/alternates &&

      but --but-dir=../.but cat-file cummit $B |
      but hash-object -t cummit -w --stdin &&

      but --but-dir=../.but cat-file tree "$B^{tree}" |
      but hash-object -t tree -w --stdin
     ) &&
     C=$(but --but-dir=patch_clone/.but rev-parse HEAD)
    '

# Clone patch_clone indirectly by cloning base and fetching.
#
test_expect_success \
    'indirectly clone patch_clone' \
    '(mkdir user_clone &&
      cd user_clone &&
      but init &&
      but pull ../.but &&
      test $(but rev-parse HEAD) = $B &&

      but pull ../patch_clone/.but &&
      test $(but rev-parse HEAD) = $C
     )
    '

# Cloning the patch_clone directly should fail.
#
test_expect_success \
    'clone of patch_clone is incomplete' \
    '(mkdir user_direct &&
      cd user_direct &&
      but init &&
      test_must_fail but fetch ../patch_clone/.but
     )
    '

test_done
