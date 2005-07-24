#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-ls-files --others --exclude

This test runs git-ls-files --others and tests --exclude patterns.
'

. ./test-lib.sh

rm -fr one three
for dir in . one one/two three
do
  mkdir -p $dir &&
  for i in 1 2 3 4 5
  do
    >$dir/a.$i
  done
done

cat >expect <<EOF
a.2
a.4
a.5
one/a.3
one/a.4
one/a.5
one/two/a.3
one/two/a.5
three/a.2
three/a.3
three/a.4
three/a.5
EOF

echo '.gitignore
output
expect
.gitignore
' >.git/ignore

echo '*.1
/*.3' >.gitignore
echo '*.2
two/*.4' >one/.gitignore

test_expect_success \
    'git-ls-files --others --exclude.' \
    'git-ls-files --others \
       --exclude-per-directory=.gitignore \
       --exclude-from=.git/ignore \
       >output &&
     diff -u expect output'
