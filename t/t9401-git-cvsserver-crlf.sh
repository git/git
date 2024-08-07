#!/bin/sh
#
# Copyright (c) 2008 Matthew Ogilvie
# Parts adapted from other tests.
#

test_description='git-cvsserver -kb modes

tests -kb mode for binary files when accessing a git
repository using cvs CLI client via git-cvsserver server'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

marked_as () {
    foundEntry="$(grep "^/$2/" "$1/CVS/Entries")"
    if [ x"$foundEntry" = x"" ] ; then
       echo "NOT FOUND: $1 $2 1 $3" >> "${WORKDIR}/marked.log"
       return 1
    fi
    test x"$(grep "^/$2/" "$1/CVS/Entries" | cut -d/ -f5)" = x"$3"
    stat=$?
    echo "$1 $2 $stat '$3'" >> "${WORKDIR}/marked.log"
    return $stat
}

not_present() {
    foundEntry="$(grep "^/$2/" "$1/CVS/Entries")"
    if [ -r "$1/$2" ] ; then
        echo "Error: File still exists: $1 $2" >> "${WORKDIR}/marked.log"
        return 1;
    fi
    if [ x"$foundEntry" != x"" ] ; then
        echo "Error: should not have found: $1 $2" >> "${WORKDIR}/marked.log"
        return 1;
    else
        echo "Correctly not found: $1 $2" >> "${WORKDIR}/marked.log"
        return 0;
    fi
}

check_status_options() {
    (cd "$1" &&
    GIT_CONFIG="$git_config" cvs -Q status "$2" > "${WORKDIR}/status.out" 2>&1
    )
    if [ x"$?" != x"0" ] ; then
	echo "Error from cvs status: $1 $2" >> "${WORKDIR}/marked.log"
	return 1;
    fi
    got="$(sed -n -e 's/^[ 	]*Sticky Options:[ 	]*//p' "${WORKDIR}/status.out")"
    expect="$3"
    if [ x"$expect" = x"" ] ; then
	expect="(none)"
    fi
    test x"$got" = x"$expect"
    stat=$?
    echo "cvs status: $1 $2 $stat '$3' '$got'" >> "${WORKDIR}/marked.log"
    return $stat
}

cvs >/dev/null 2>&1
if test $? -ne 1
then
    skip_all='skipping git-cvsserver tests, cvs not found'
    test_done
fi
if ! test_have_prereq PERL
then
    skip_all='skipping git-cvsserver tests, perl not available'
    test_done
fi
perl -e 'use DBI; use DBD::SQLite' >/dev/null 2>&1 || {
    skip_all='skipping git-cvsserver tests, Perl SQLite interface unavailable'
    test_done
}

unset GIT_DIR GIT_CONFIG
WORKDIR=$PWD
SERVERDIR=$PWD/gitcvs.git
git_config="$SERVERDIR/config"
CVSROOT=":fork:$SERVERDIR"
CVSWORK="$PWD/cvswork"
CVS_SERVER=git-cvsserver
export CVSROOT CVS_SERVER

rm -rf "$CVSWORK" "$SERVERDIR"
test_expect_success 'setup' '
    git config push.default matching &&
    echo "Simple text file" >textfile.c &&
    echo "File with embedded NUL: Q <- there" | q_to_nul > binfile.bin &&
    mkdir subdir &&
    echo "Another text file" > subdir/file.h &&
    echo "Another binary: Q (this time CR)" | q_to_cr > subdir/withCr.bin &&
    echo "Mixed up NUL, but marked text: Q <- there" | q_to_nul > mixedUp.c &&
    echo "Unspecified" > subdir/unspecified.other &&
    echo "/*.bin -crlf" > .gitattributes &&
    echo "/*.c crlf" >> .gitattributes &&
    echo "subdir/*.bin -crlf" >> .gitattributes &&
    echo "subdir/*.c crlf" >> .gitattributes &&
    echo "subdir/file.h crlf" >> .gitattributes &&
    git add .gitattributes textfile.c binfile.bin mixedUp.c subdir/* &&
    git commit -q -m "First Commit" &&
    git clone -q --bare "$WORKDIR/.git" "$SERVERDIR" >/dev/null 2>&1 &&
    GIT_DIR="$SERVERDIR" git config --bool gitcvs.enabled true &&
    GIT_DIR="$SERVERDIR" git config gitcvs.logfile "$SERVERDIR/gitcvs.log"
'

test_expect_success 'cvs co (default crlf)' '
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork main >cvs.log 2>&1 &&
    test x"$(grep '/-k' cvswork/CVS/Entries cvswork/subdir/CVS/Entries)" = x""
'

rm -rf cvswork
test_expect_success 'cvs co (allbinary)' '
    GIT_DIR="$SERVERDIR" git config --bool gitcvs.allbinary true &&
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork main >cvs.log 2>&1 &&
    marked_as cvswork textfile.c -kb &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes -kb &&
    marked_as cvswork mixedUp.c -kb &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h -kb &&
    marked_as cvswork/subdir unspecified.other -kb
'

rm -rf cvswork cvs.log
test_expect_success 'cvs co (use attributes/allbinary)' '
    GIT_DIR="$SERVERDIR" git config --bool gitcvs.usecrlfattr true &&
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork main >cvs.log 2>&1 &&
    marked_as cvswork textfile.c "" &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes -kb &&
    marked_as cvswork mixedUp.c "" &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h "" &&
    marked_as cvswork/subdir unspecified.other -kb
'

rm -rf cvswork
test_expect_success 'cvs co (use attributes)' '
    GIT_DIR="$SERVERDIR" git config --bool gitcvs.allbinary false &&
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork main >cvs.log 2>&1 &&
    marked_as cvswork textfile.c "" &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes "" &&
    marked_as cvswork mixedUp.c "" &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h "" &&
    marked_as cvswork/subdir unspecified.other ""
'

test_expect_success 'adding files' '
    (cd cvswork &&
    (cd subdir &&
    echo "more text" > src.c &&
    GIT_CONFIG="$git_config" cvs -Q add src.c >cvs.log 2>&1 &&
    marked_as . src.c "" &&
    echo "pseudo-binary" > temp.bin
    ) &&
    GIT_CONFIG="$git_config" cvs -Q add subdir/temp.bin >cvs.log 2>&1 &&
    marked_as subdir temp.bin "-kb" &&
    cd subdir &&
    GIT_CONFIG="$git_config" cvs -Q ci -m "adding files" >cvs.log 2>&1 &&
    marked_as . temp.bin "-kb" &&
    marked_as . src.c ""
    )
'

test_expect_success 'updating' '
    git pull gitcvs.git &&
    echo "hi" >subdir/newfile.bin &&
    echo "junk" >subdir/file.h &&
    echo "hi" >subdir/newfile.c &&
    echo "hello" >>binfile.bin &&
    git add subdir/newfile.bin subdir/file.h subdir/newfile.c binfile.bin &&
    git commit -q -m "Add and change some files" &&
    git push gitcvs.git >/dev/null &&
    (cd cvswork &&
    GIT_CONFIG="$git_config" cvs -Q update
    ) &&
    marked_as cvswork textfile.c "" &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes "" &&
    marked_as cvswork mixedUp.c "" &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h "" &&
    marked_as cvswork/subdir unspecified.other "" &&
    marked_as cvswork/subdir newfile.bin -kb &&
    marked_as cvswork/subdir newfile.c "" &&
    echo "File with embedded NUL: Q <- there" | q_to_nul > tmpExpect1 &&
    echo "hello" >> tmpExpect1 &&
    cmp cvswork/binfile.bin tmpExpect1
'

rm -rf cvswork
test_expect_success 'cvs co (use attributes/guess)' '
    GIT_DIR="$SERVERDIR" git config gitcvs.allbinary guess &&
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork main >cvs.log 2>&1 &&
    marked_as cvswork textfile.c "" &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes "" &&
    marked_as cvswork mixedUp.c "" &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h "" &&
    marked_as cvswork/subdir unspecified.other "" &&
    marked_as cvswork/subdir newfile.bin -kb &&
    marked_as cvswork/subdir newfile.c ""
'

test_expect_success 'setup multi-line files' '
    ( echo "line 1" &&
      echo "line 2" &&
      echo "line 3" &&
      echo "line 4 with NUL: Q <-" ) | q_to_nul > multiline.c &&
    git add multiline.c &&
    ( echo "line 1" &&
      echo "line 2" &&
      echo "line 3" &&
      echo "line 4" ) | q_to_nul > multilineTxt.c &&
    git add multilineTxt.c &&
    git commit -q -m "multiline files" &&
    git push gitcvs.git >/dev/null
'

rm -rf cvswork
test_expect_success 'cvs co (guess)' '
    GIT_DIR="$SERVERDIR" git config --bool gitcvs.usecrlfattr false &&
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork main >cvs.log 2>&1 &&
    marked_as cvswork textfile.c "" &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes "" &&
    marked_as cvswork mixedUp.c -kb &&
    marked_as cvswork multiline.c -kb &&
    marked_as cvswork multilineTxt.c "" &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h "" &&
    marked_as cvswork/subdir unspecified.other "" &&
    marked_as cvswork/subdir newfile.bin "" &&
    marked_as cvswork/subdir newfile.c ""
'

test_expect_success 'cvs co another copy (guess)' '
    GIT_CONFIG="$git_config" cvs -Q co -d cvswork2 main >cvs.log 2>&1 &&
    marked_as cvswork2 textfile.c "" &&
    marked_as cvswork2 binfile.bin -kb &&
    marked_as cvswork2 .gitattributes "" &&
    marked_as cvswork2 mixedUp.c -kb &&
    marked_as cvswork2 multiline.c -kb &&
    marked_as cvswork2 multilineTxt.c "" &&
    marked_as cvswork2/subdir withCr.bin -kb &&
    marked_as cvswork2/subdir file.h "" &&
    marked_as cvswork2/subdir unspecified.other "" &&
    marked_as cvswork2/subdir newfile.bin "" &&
    marked_as cvswork2/subdir newfile.c ""
'

test_expect_success 'cvs status - sticky options' '
    check_status_options cvswork2 textfile.c "" &&
    check_status_options cvswork2 binfile.bin -kb &&
    check_status_options cvswork2 .gitattributes "" &&
    check_status_options cvswork2 mixedUp.c -kb &&
    check_status_options cvswork2 multiline.c -kb &&
    check_status_options cvswork2 multilineTxt.c "" &&
    check_status_options cvswork2/subdir withCr.bin -kb &&
    check_status_options cvswork2 subdir/withCr.bin -kb &&
    check_status_options cvswork2/subdir file.h "" &&
    check_status_options cvswork2 subdir/file.h "" &&
    check_status_options cvswork2/subdir unspecified.other "" &&
    check_status_options cvswork2/subdir newfile.bin "" &&
    check_status_options cvswork2/subdir newfile.c ""
'

test_expect_success 'add text (guess)' '
    (cd cvswork &&
    echo "simpleText" > simpleText.c &&
    GIT_CONFIG="$git_config" cvs -Q add simpleText.c
    ) &&
    marked_as cvswork simpleText.c ""
'

test_expect_success 'add bin (guess)' '
    (cd cvswork &&
    echo "simpleBin: NUL: Q <- there" | q_to_nul > simpleBin.bin &&
    GIT_CONFIG="$git_config" cvs -Q add simpleBin.bin
    ) &&
    marked_as cvswork simpleBin.bin -kb
'

test_expect_success 'remove files (guess)' '
    (cd cvswork &&
    GIT_CONFIG="$git_config" cvs -Q rm -f subdir/file.h &&
    (cd subdir &&
    GIT_CONFIG="$git_config" cvs -Q rm -f withCr.bin
    )) &&
    marked_as cvswork/subdir withCr.bin -kb &&
    marked_as cvswork/subdir file.h ""
'

test_expect_success 'cvs ci (guess)' '
    (cd cvswork &&
    GIT_CONFIG="$git_config" cvs -Q ci -m "add/rm files" >cvs.log 2>&1
    ) &&
    marked_as cvswork textfile.c "" &&
    marked_as cvswork binfile.bin -kb &&
    marked_as cvswork .gitattributes "" &&
    marked_as cvswork mixedUp.c -kb &&
    marked_as cvswork multiline.c -kb &&
    marked_as cvswork multilineTxt.c "" &&
    not_present cvswork/subdir withCr.bin &&
    not_present cvswork/subdir file.h &&
    marked_as cvswork/subdir unspecified.other "" &&
    marked_as cvswork/subdir newfile.bin "" &&
    marked_as cvswork/subdir newfile.c "" &&
    marked_as cvswork simpleBin.bin -kb &&
    marked_as cvswork simpleText.c ""
'

test_expect_success 'update subdir of other copy (guess)' '
    (cd cvswork2/subdir &&
    GIT_CONFIG="$git_config" cvs -Q update
    ) &&
    marked_as cvswork2 textfile.c "" &&
    marked_as cvswork2 binfile.bin -kb &&
    marked_as cvswork2 .gitattributes "" &&
    marked_as cvswork2 mixedUp.c -kb &&
    marked_as cvswork2 multiline.c -kb &&
    marked_as cvswork2 multilineTxt.c "" &&
    not_present cvswork2/subdir withCr.bin &&
    not_present cvswork2/subdir file.h &&
    marked_as cvswork2/subdir unspecified.other "" &&
    marked_as cvswork2/subdir newfile.bin "" &&
    marked_as cvswork2/subdir newfile.c "" &&
    not_present cvswork2 simpleBin.bin &&
    not_present cvswork2 simpleText.c
'

echo "starting update/merge" >> "${WORKDIR}/marked.log"
test_expect_success 'update/merge full other copy (guess)' '
    git pull gitcvs.git main &&
    sed "s/3/replaced_3/" < multilineTxt.c > ml.temp &&
    mv ml.temp multilineTxt.c &&
    git add multilineTxt.c &&
    git commit -q -m "modify multiline file" >> "${WORKDIR}/marked.log" &&
    git push gitcvs.git >/dev/null &&
    (cd cvswork2 &&
    sed "s/1/replaced_1/" < multilineTxt.c > ml.temp &&
    mv ml.temp multilineTxt.c &&
    GIT_CONFIG="$git_config" cvs update > cvs.log 2>&1
    ) &&
    marked_as cvswork2 textfile.c "" &&
    marked_as cvswork2 binfile.bin -kb &&
    marked_as cvswork2 .gitattributes "" &&
    marked_as cvswork2 mixedUp.c -kb &&
    marked_as cvswork2 multiline.c -kb &&
    marked_as cvswork2 multilineTxt.c "" &&
    not_present cvswork2/subdir withCr.bin &&
    not_present cvswork2/subdir file.h &&
    marked_as cvswork2/subdir unspecified.other "" &&
    marked_as cvswork2/subdir newfile.bin "" &&
    marked_as cvswork2/subdir newfile.c "" &&
    marked_as cvswork2 simpleBin.bin -kb &&
    marked_as cvswork2 simpleText.c "" &&
    echo "line replaced_1" > tmpExpect2 &&
    echo "line 2" >> tmpExpect2 &&
    echo "line replaced_3" >> tmpExpect2 &&
    echo "line 4" | q_to_nul >> tmpExpect2 &&
    cmp cvswork2/multilineTxt.c tmpExpect2
'

test_done
