
The source code in this directory comes from https://github.com/google/reftable.

The VERSION file keeps track of the current version of the reftable library.

To update the library, do:

   ((cd reftable-repo && git fetch origin && git checkout origin/master ) ||
    git clone https://github.com/google/reftable reftable-repo) && \
   cp reftable-repo/c/*.[ch] reftable/ && \
   cp reftable-repo/LICENSE reftable/ &&
   git --git-dir reftable-repo/.git show --no-patch origin/master \
    > reftable/VERSION && \
   echo '/* empty */' > reftable/config.h
   rm reftable/*_test.c reftable/test_framework.*
   git add reftable/*.[ch]

Bugfixes should be accompanied by a test and applied to upstream project at
https://github.com/google/reftable.
