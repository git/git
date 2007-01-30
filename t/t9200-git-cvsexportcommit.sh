#!/bin/sh
#
# Copyright (c) Robin Rosenberg
#
test_description='CVS export comit. '

. ./test-lib.sh

cvs >/dev/null 2>&1
if test $? -ne 1
then
    test_expect_success 'skipping git-cvsexportcommit tests, cvs not found' :
    test_done
    exit
fi

CVSROOT=$(pwd)/cvsroot
CVSWORK=$(pwd)/cvswork
GIT_DIR=$(pwd)/.git
export CVSROOT CVSWORK GIT_DIR

rm -rf "$CVSROOT" "$CVSWORK"
mkdir "$CVSROOT" &&
cvs init &&
cvs -Q co -d "$CVSWORK" . &&
echo >empty &&
git add empty &&
git commit -q -a -m "Initial" 2>/dev/null ||
exit 1

test_expect_success \
    'New file' \
    'mkdir A B C D E F &&
     echo hello1 >A/newfile1.txt &&
     echo hello2 >B/newfile2.txt &&
     cp ../test9200a.png C/newfile3.png &&
     cp ../test9200a.png D/newfile4.png &&
     git add A/newfile1.txt &&
     git add B/newfile2.txt &&
     git add C/newfile3.png &&
     git add D/newfile4.png &&
     git commit -a -m "Test: New file" &&
     id=$(git rev-list --max-count=1 HEAD) &&
     (cd "$CVSWORK" &&
     git cvsexportcommit -c $id &&
     test "$(echo $(sort A/CVS/Entries|cut -d/ -f2,3,5))" = "newfile1.txt/1.1/" &&
     test "$(echo $(sort B/CVS/Entries|cut -d/ -f2,3,5))" = "newfile2.txt/1.1/" &&
     test "$(echo $(sort C/CVS/Entries|cut -d/ -f2,3,5))" = "newfile3.png/1.1/-kb" &&
     test "$(echo $(sort D/CVS/Entries|cut -d/ -f2,3,5))" = "newfile4.png/1.1/-kb" &&
     diff A/newfile1.txt ../A/newfile1.txt &&
     diff B/newfile2.txt ../B/newfile2.txt &&
     diff C/newfile3.png ../C/newfile3.png &&
     diff D/newfile4.png ../D/newfile4.png
     )'

test_expect_success \
    'Remove two files, add two and update two' \
    'echo Hello1 >>A/newfile1.txt &&
     rm -f B/newfile2.txt &&
     rm -f C/newfile3.png &&
     echo Hello5  >E/newfile5.txt &&
     cp ../test9200b.png D/newfile4.png &&
     cp ../test9200a.png F/newfile6.png &&
     git add E/newfile5.txt &&
     git add F/newfile6.png &&
     git commit -a -m "Test: Remove, add and update" &&
     id=$(git rev-list --max-count=1 HEAD) &&
     (cd "$CVSWORK" &&
     git cvsexportcommit -c $id &&
     test "$(echo $(sort A/CVS/Entries|cut -d/ -f2,3,5))" = "newfile1.txt/1.2/" &&
     test "$(echo $(sort B/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort C/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort D/CVS/Entries|cut -d/ -f2,3,5))" = "newfile4.png/1.2/-kb" &&
     test "$(echo $(sort E/CVS/Entries|cut -d/ -f2,3,5))" = "newfile5.txt/1.1/" &&
     test "$(echo $(sort F/CVS/Entries|cut -d/ -f2,3,5))" = "newfile6.png/1.1/-kb" &&
     diff A/newfile1.txt ../A/newfile1.txt &&
     diff D/newfile4.png ../D/newfile4.png &&
     diff E/newfile5.txt ../E/newfile5.txt &&
     diff F/newfile6.png ../F/newfile6.png
     )'

# Should fail (but only on the git-cvsexportcommit stage)
test_expect_success \
    'Fail to change binary more than one generation old' \
    'cat F/newfile6.png >>D/newfile4.png &&
     git commit -a -m "generatiion 1" &&
     cat F/newfile6.png >>D/newfile4.png &&
     git commit -a -m "generation 2" &&
     id=$(git rev-list --max-count=1 HEAD) &&
     (cd "$CVSWORK" &&
     ! git cvsexportcommit -c $id
     )'

#test_expect_success \
#    'Fail to remove binary file more than one generation old' \
#    'git reset --hard HEAD^ &&
#     cat F/newfile6.png >>D/newfile4.png &&
#     git commit -a -m "generation 2 (again)" &&
#     rm -f D/newfile4.png &&
#     git commit -a -m "generation 3" &&
#     id=$(git rev-list --max-count=1 HEAD) &&
#     (cd "$CVSWORK" &&
#     ! git cvsexportcommit -c $id
#     )'

# We reuse the state from two tests back here

# This test is here because a patch for only binary files will
# fail with gnu patch, so cvsexportcommit must handle that.
test_expect_success \
    'Remove only binary files' \
    'git reset --hard HEAD^^ &&
     rm -f D/newfile4.png &&
     git commit -a -m "test: remove only a binary file" &&
     id=$(git rev-list --max-count=1 HEAD) &&
     (cd "$CVSWORK" &&
     git cvsexportcommit -c $id &&
     test "$(echo $(sort A/CVS/Entries|cut -d/ -f2,3,5))" = "newfile1.txt/1.2/" &&
     test "$(echo $(sort B/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort C/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort D/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort E/CVS/Entries|cut -d/ -f2,3,5))" = "newfile5.txt/1.1/" &&
     test "$(echo $(sort F/CVS/Entries|cut -d/ -f2,3,5))" = "newfile6.png/1.1/-kb" &&
     diff A/newfile1.txt ../A/newfile1.txt &&
     diff E/newfile5.txt ../E/newfile5.txt &&
     diff F/newfile6.png ../F/newfile6.png
     )'

test_expect_success \
    'Remove only a text file' \
    'rm -f A/newfile1.txt &&
     git commit -a -m "test: remove only a binary file" &&
     id=$(git rev-list --max-count=1 HEAD) &&
     (cd "$CVSWORK" &&
     git cvsexportcommit -c $id &&
     test "$(echo $(sort A/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort B/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort C/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort D/CVS/Entries|cut -d/ -f2,3,5))" = "" &&
     test "$(echo $(sort E/CVS/Entries|cut -d/ -f2,3,5))" = "newfile5.txt/1.1/" &&
     test "$(echo $(sort F/CVS/Entries|cut -d/ -f2,3,5))" = "newfile6.png/1.1/-kb" &&
     diff E/newfile5.txt ../E/newfile5.txt &&
     diff F/newfile6.png ../F/newfile6.png
     )'

test_expect_success \
     'New file with spaces in file name' \
     'mkdir "G g" &&
      echo ok then >"G g/with spaces.txt" &&
      git add "G g/with spaces.txt" && \
      cp ../test9200a.png "G g/with spaces.png" && \
      git add "G g/with spaces.png" &&
      git commit -a -m "With spaces" &&
      id=$(git rev-list --max-count=1 HEAD) &&
      (cd "$CVSWORK" &&
      git-cvsexportcommit -c $id &&
      test "$(echo $(sort "G g/CVS/Entries"|cut -d/ -f2,3,5))" = "with spaces.png/1.1/-kb with spaces.txt/1.1/"
      )'

test_expect_success \
     'Update file with spaces in file name' \
     'echo Ok then >>"G g/with spaces.txt" &&
      cat ../test9200a.png >>"G g/with spaces.png" && \
      git add "G g/with spaces.png" &&
      git commit -a -m "Update with spaces" &&
      id=$(git rev-list --max-count=1 HEAD) &&
      (cd "$CVSWORK" &&
      git-cvsexportcommit -c $id
      test "$(echo $(sort "G g/CVS/Entries"|cut -d/ -f2,3,5))" = "with spaces.png/1.2/-kb with spaces.txt/1.2/"
      )'

# This test contains ISO-8859-1 characters
test_expect_success \
     'File with non-ascii file name' \
     'mkdir -p Å/goo/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/å/ä/ö &&
      echo Foo >Å/goo/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/å/ä/ö/gårdetsågårdet.txt &&
      git add Å/goo/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/å/ä/ö/gårdetsågårdet.txt &&
      cp ../test9200a.png Å/goo/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/å/ä/ö/gårdetsågårdet.png &&
      git add Å/goo/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/å/ä/ö/gårdetsågårdet.png &&
      git commit -a -m "Går det så går det" && \
      id=$(git rev-list --max-count=1 HEAD) &&
      (cd "$CVSWORK" &&
      git-cvsexportcommit -v -c $id &&
      test "$(echo $(sort Å/goo/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/å/ä/ö/CVS/Entries|cut -d/ -f2,3,5))" = "gårdetsågårdet.png/1.1/-kb gårdetsågårdet.txt/1.1/"
      )'

test_expect_success \
     'Mismatching patch should fail' \
     'date >>"E/newfile5.txt" &&
      git add "E/newfile5.txt" &&
      git commit -a -m "Update one" &&
      date >>"E/newfile5.txt" &&
      git add "E/newfile5.txt" &&
      git commit -a -m "Update two" &&
      id=$(git rev-list --max-count=1 HEAD) &&
      (cd "$CVSWORK" &&
      ! git-cvsexportcommit -c $id
      )'

test_expect_success \
     'Retain execute bit' \
     'mkdir G &&
      echo executeon >G/on &&
      chmod +x G/on &&
      echo executeoff >G/off &&
      git add G/on &&
      git add G/off &&
      git commit -a -m "Execute test" &&
      (cd "$CVSWORK" &&
      git-cvsexportcommit -c HEAD
      test -x G/on &&
      ! test -x G/off
      )'

test_done
