#!/bin/sh

set -eux

((cd reftable-repo && git fetch origin && git checkout origin/master ) ||
git clone https://github.com/google/reftable reftable-repo) && \
cp reftable-repo/c/*.[ch] reftable/ && \
cp reftable-repo/c/include/*.[ch] reftable/ && \
cp reftable-repo/LICENSE reftable/ &&
git --git-dir reftable-repo/.git show --no-patch origin/master \
> reftable/VERSION && \
sed -i~ 's|if REFTABLE_IN_GITCORE|if 1 /* REFTABLE_IN_GITCORE */|' reftable/system.h
rm reftable/*_test.c reftable/test_framework.* reftable/compat.*
git add reftable/*.[ch]
