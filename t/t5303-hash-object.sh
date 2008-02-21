#!/bin/sh

test_description=git-hash-object

. ./test-lib.sh

test_expect_success \
    'git hash-object -w --stdin saves the object' \
    'obname=$(echo foo | git hash-object -w --stdin) &&
    obpath=$(echo $obname | sed -e "s/\(..\)/\1\//") &&
    test -r .git/objects/"$obpath" &&
    rm -f .git/objects/"$obpath"'
    
test_expect_success \
    'git hash-object --stdin -w saves the object' \
    'obname=$(echo foo | git hash-object --stdin -w) &&
    obpath=$(echo $obname | sed -e "s/\(..\)/\1\//") &&
    test -r .git/objects/"$obpath" &&
    rm -f .git/objects/"$obpath"'    

test_expect_success \
    'git hash-object --stdin file1 <file0 first operates on file0, then file1' \
    'echo foo > file1 &&
    obname0=$(echo bar | git hash-object --stdin) &&
    obname1=$(git hash-object file1) &&
    obname0new=$(echo bar | git hash-object --stdin file1 | sed -n -e 1p) &&
    obname1new=$(echo bar | git hash-object --stdin file1 | sed -n -e 2p) &&
    test "$obname0" = "$obname0new" &&
    test "$obname1" = "$obname1new"'

test_expect_success \
    'git hash-object refuses multiple --stdin arguments' \
    '! git hash-object --stdin --stdin < file1'

test_done
