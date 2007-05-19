#!/bin/sh
#
# Copyright (c) 2007 Frank Lichtenheld
#

test_description='git-cvsserver access

tests read access to a git repository with the
cvs CLI client via git-cvsserver server'

. ./test-lib.sh

cvs >/dev/null 2>&1
if test $? -ne 1
then
    test_expect_success 'skipping git-cvsserver tests, cvs not found' :
    test_done
    exit
fi
perl -e 'use DBI; use DBD::SQLite' >/dev/null 2>&1 || {
    test_expect_success 'skipping git-cvsserver tests, Perl SQLite interface unavailable' :
    test_done
    exit
}

unset GIT_DIR GIT_CONFIG
WORKDIR=$(pwd)
SERVERDIR=$(pwd)/gitcvs.git
CVSROOT=":fork:$SERVERDIR"
CVSWORK=$(pwd)/cvswork
CVS_SERVER=git-cvsserver
export CVSROOT CVS_SERVER

rm -rf "$CVSWORK" "$SERVERDIR"
echo >empty &&
  git add empty &&
  git commit -q -m "First Commit" &&
  git clone -q --local --bare "$WORKDIR/.git" "$SERVERDIR" >/dev/null 2>&1 &&
  GIT_DIR="$SERVERDIR" git config --bool gitcvs.enabled true &&
  GIT_DIR="$SERVERDIR" git config --bool gitcvs.logfile "$SERVERDIR/gitcvs.log" ||
  exit 1

# note that cvs doesn't accept absolute pathnames
# as argument to co -d
test_expect_success 'basic checkout' \
  'cvs -Q co -d cvswork master &&
   test "$(echo $(grep -v ^D cvswork/CVS/Entries|cut -d/ -f2,3,5))" = "empty/1.1/"'

test_expect_success 'cvs update (create new file)' \
  'echo testfile1 >testfile1 &&
   git add testfile1 &&
   git commit -q -m "Add testfile1" &&
   git push gitcvs.git >/dev/null &&
   cd cvswork &&
   cvs -Q update &&
   test "$(echo $(grep testfile1 CVS/Entries|cut -d/ -f2,3,5))" = "testfile1/1.1/" &&
   diff -q testfile1 ../testfile1'

cd "$WORKDIR"
test_expect_success 'cvs update (update existing file)' \
  'echo line 2 >>testfile1 &&
   git add testfile1 &&
   git commit -q -m "Append to testfile1" &&
   git push gitcvs.git >/dev/null &&
   cd cvswork &&
   cvs -Q update &&
   test "$(echo $(grep testfile1 CVS/Entries|cut -d/ -f2,3,5))" = "testfile1/1.2/" &&
   diff -q testfile1 ../testfile1'

cd "$WORKDIR"
#TODO: cvsserver doesn't support update w/o -d
test_expect_failure "cvs update w/o -d doesn't create subdir (TODO)" \
  'mkdir test &&
   echo >test/empty &&
   git add test &&
   git commit -q -m "Single Subdirectory" &&
   git push gitcvs.git >/dev/null &&
   cd cvswork &&
   cvs -Q update &&
   test ! -d test'

cd "$WORKDIR"
test_expect_success 'cvs update (subdirectories)' \
  '(for dir in A A/B A/B/C A/D E; do
      mkdir $dir &&
      echo "test file in $dir" >"$dir/file_in_$(echo $dir|sed -e "s#/# #g")"  &&
      git add $dir;
   done) &&
   git commit -q -m "deep sub directory structure" &&
   git push gitcvs.git >/dev/null &&
   cd cvswork &&
   cvs -Q update -d &&
   (for dir in A A/B A/B/C A/D E; do
      filename="file_in_$(echo $dir|sed -e "s#/# #g")" &&
      if test "$(echo $(grep -v ^D $dir/CVS/Entries|cut -d/ -f2,3,5))" = "$filename/1.1/" &&
           diff -q "$dir/$filename" "../$dir/$filename"; then
        :
      else
        echo >failure
      fi
    done) &&
   test ! -f failure'

cd "$WORKDIR"
test_expect_success 'cvs update (delete file)' \
  'git rm testfile1 &&
   git commit -q -m "Remove testfile1" &&
   git push gitcvs.git >/dev/null &&
   cd cvswork &&
   cvs -Q update &&
   test -z "$(grep testfile1 CVS/Entries)" &&
   test ! -f testfile1'

cd "$WORKDIR"
test_expect_success 'cvs update (re-add deleted file)' \
  'echo readded testfile >testfile1 &&
   git add testfile1 &&
   git commit -q -m "Re-Add testfile1" &&
   git push gitcvs.git >/dev/null &&
   cd cvswork &&
   cvs -Q update &&
   test "$(echo $(grep testfile1 CVS/Entries|cut -d/ -f2,3,5))" = "testfile1/1.4/" &&
   diff -q testfile1 ../testfile1'

test_done
