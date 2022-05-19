#!/bin/sh
#
# Copyright (c) 2007 Frank Lichtenheld
#

test_description='but-cvsserver access

tests read access to a but repository with the
cvs CLI client via but-cvsserver server'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if ! test_have_prereq PERL; then
	skip_all='skipping but cvsserver tests, perl not available'
	test_done
fi
cvs >/dev/null 2>&1
if test $? -ne 1
then
    skip_all='skipping but-cvsserver tests, cvs not found'
    test_done
fi
perl -e 'use DBI; use DBD::SQLite' >/dev/null 2>&1 || {
    skip_all='skipping but-cvsserver tests, Perl SQLite interface unavailable'
    test_done
}

WORKDIR=$PWD
SERVERDIR=$PWD/butcvs.but
but_config="$SERVERDIR/config"
CVSROOT=":fork:$SERVERDIR"
CVSWORK="$PWD/cvswork"
CVS_SERVER=but-cvsserver
export CVSROOT CVS_SERVER

if perl -e 'exit(1) if not defined crypt("", "cv")'
then
	PWDHASH='lac2ItudM3.KM'
else
	PWDHASH='$2b$10$t8fGvE/a9eLmfOLzsZme2uOa2QtoMYwIxq9wZA6aBKtF1Yb7FJIzi'
fi

rm -rf "$CVSWORK" "$SERVERDIR"
test_expect_success 'setup' '
  but config push.default matching &&
  echo >empty &&
  but add empty &&
  but cummit -q -m "First cummit" &&
  mkdir secondroot &&
  ( cd secondroot &&
  but init &&
  touch secondrootfile &&
  but add secondrootfile &&
  but cummit -m "second root") &&
  but fetch secondroot main &&
  but merge --allow-unrelated-histories FETCH_HEAD &&
  but clone -q --bare "$WORKDIR/.but" "$SERVERDIR" >/dev/null 2>&1 &&
  GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled true &&
  GIT_DIR="$SERVERDIR" but config butcvs.logfile "$SERVERDIR/butcvs.log" &&
  GIT_DIR="$SERVERDIR" but config butcvs.authdb "$SERVERDIR/auth.db" &&
  echo "cvsuser:$PWDHASH" >"$SERVERDIR/auth.db"
'

# note that cvs doesn't accept absolute pathnames
# as argument to co -d
test_expect_success 'basic checkout' \
  'GIT_CONFIG="$but_config" cvs -Q co -d cvswork main &&
   test "$(echo $(grep -v ^D cvswork/CVS/Entries|cut -d/ -f2,3,5 | head -n 1))" = "empty/1.1/" &&
   test "$(echo $(grep -v ^D cvswork/CVS/Entries|cut -d/ -f2,3,5 | sed -ne \$p))" = "secondrootfile/1.1/"'

#------------------------
# PSERVER AUTHENTICATION
#------------------------

cat >request-anonymous  <<EOF
BEGIN AUTH REQUEST
$SERVERDIR
anonymous

END AUTH REQUEST
EOF

cat >request-but  <<EOF
BEGIN AUTH REQUEST
$SERVERDIR
but

END AUTH REQUEST
EOF

cat >login-anonymous <<EOF
BEGIN VERIFICATION REQUEST
$SERVERDIR
anonymous

END VERIFICATION REQUEST
EOF

cat >login-but <<EOF
BEGIN VERIFICATION REQUEST
$SERVERDIR
but

END VERIFICATION REQUEST
EOF

cat >login-but-ok <<EOF
BEGIN VERIFICATION REQUEST
$SERVERDIR
cvsuser
Ah<Z:yZZ30 e
END VERIFICATION REQUEST
EOF

test_expect_success 'pserver authentication' \
  'cat request-anonymous | but-cvsserver pserver >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'pserver authentication failure (non-anonymous user)' \
  'if cat request-but | but-cvsserver pserver >log 2>&1
   then
       false
   else
       true
   fi &&
   sed -ne \$p log | grep "^I HATE YOU\$"'

test_expect_success 'pserver authentication success (non-anonymous user with password)' \
  'cat login-but-ok | but-cvsserver pserver >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'pserver authentication (login)' \
  'cat login-anonymous | but-cvsserver pserver >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'pserver authentication failure (login/non-anonymous user)' \
  'if cat login-but | but-cvsserver pserver >log 2>&1
   then
       false
   else
       true
   fi &&
   sed -ne \$p log | grep "^I HATE YOU\$"'


# misuse pserver authentication for testing of req_Root

cat >request-relative  <<EOF
BEGIN AUTH REQUEST
butcvs.but
anonymous

END AUTH REQUEST
EOF

cat >request-conflict  <<EOF
BEGIN AUTH REQUEST
$SERVERDIR
anonymous

END AUTH REQUEST
Root $WORKDIR
EOF

test_expect_success 'req_Root failure (relative pathname)' \
  'if cat request-relative | but-cvsserver pserver >log 2>&1
   then
       echo unexpected success
       false
   else
       true
   fi &&
   tail log | grep "^error 1 Root must be an absolute pathname$"'

test_expect_success 'req_Root failure (conflicting roots)' \
  'cat request-conflict | but-cvsserver pserver >log 2>&1 &&
   tail log | grep "^error 1 Conflicting roots specified$"'

test_expect_success 'req_Root (strict paths)' \
  'cat request-anonymous | but-cvsserver --strict-paths pserver "$SERVERDIR" >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'req_Root failure (strict-paths)' '
    ! cat request-anonymous |
    but-cvsserver --strict-paths pserver "$WORKDIR" >log 2>&1
'

test_expect_success 'req_Root (w/o strict-paths)' \
  'cat request-anonymous | but-cvsserver pserver "$WORKDIR/" >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'req_Root failure (w/o strict-paths)' '
    ! cat request-anonymous |
    but-cvsserver pserver "$WORKDIR/butcvs" >log 2>&1
'

cat >request-base  <<EOF
BEGIN AUTH REQUEST
/butcvs.but
anonymous

END AUTH REQUEST
Root /butcvs.but
EOF

test_expect_success 'req_Root (base-path)' \
  'cat request-base | but-cvsserver --strict-paths --base-path "$WORKDIR/" pserver "$SERVERDIR" >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'req_Root failure (base-path)' '
    ! cat request-anonymous |
    but-cvsserver --strict-paths --base-path "$WORKDIR" pserver "$SERVERDIR" >log 2>&1
'

GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled false || exit 1

test_expect_success 'req_Root (export-all)' \
  'cat request-anonymous | but-cvsserver --export-all pserver "$WORKDIR" >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

test_expect_success 'req_Root failure (export-all w/o whitelist)' \
  '! (cat request-anonymous | but-cvsserver --export-all pserver >log 2>&1 || false)'

test_expect_success 'req_Root (everything together)' \
  'cat request-base | but-cvsserver --export-all --strict-paths --base-path "$WORKDIR/" pserver "$SERVERDIR" >log 2>&1 &&
   sed -ne \$p log | grep "^I LOVE YOU\$"'

GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled true || exit 1

#--------------
# CONFIG TESTS
#--------------

test_expect_success 'butcvs.enabled = false' \
  'GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled false &&
   if GIT_CONFIG="$but_config" cvs -Q co -d cvswork2 main >cvs.log 2>&1
   then
     echo unexpected cvs success
     false
   else
     true
   fi &&
   grep "GITCVS emulation disabled" cvs.log &&
   test ! -d cvswork2'

rm -fr cvswork2
test_expect_success 'butcvs.ext.enabled = true' \
  'GIT_DIR="$SERVERDIR" but config --bool butcvs.ext.enabled true &&
   GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled false &&
   GIT_CONFIG="$but_config" cvs -Q co -d cvswork2 main >cvs.log 2>&1 &&
   test_cmp cvswork cvswork2'

rm -fr cvswork2
test_expect_success 'butcvs.ext.enabled = false' \
  'GIT_DIR="$SERVERDIR" but config --bool butcvs.ext.enabled false &&
   GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled true &&
   if GIT_CONFIG="$but_config" cvs -Q co -d cvswork2 main >cvs.log 2>&1
   then
     echo unexpected cvs success
     false
   else
     true
   fi &&
   grep "GITCVS emulation disabled" cvs.log &&
   test ! -d cvswork2'

rm -fr cvswork2
test_expect_success 'butcvs.dbname' \
  'GIT_DIR="$SERVERDIR" but config --bool butcvs.ext.enabled true &&
   GIT_DIR="$SERVERDIR" but config butcvs.dbname %Gbutcvs.%a.%m.sqlite &&
   GIT_CONFIG="$but_config" cvs -Q co -d cvswork2 main >cvs.log 2>&1 &&
   test_cmp cvswork cvswork2 &&
   test -f "$SERVERDIR/butcvs.ext.main.sqlite" &&
   cmp "$SERVERDIR/butcvs.main.sqlite" "$SERVERDIR/butcvs.ext.main.sqlite"'

rm -fr cvswork2
test_expect_success 'butcvs.ext.dbname' \
  'GIT_DIR="$SERVERDIR" but config --bool butcvs.ext.enabled true &&
   GIT_DIR="$SERVERDIR" but config butcvs.ext.dbname %Gbutcvs1.%a.%m.sqlite &&
   GIT_DIR="$SERVERDIR" but config butcvs.dbname %Gbutcvs2.%a.%m.sqlite &&
   GIT_CONFIG="$but_config" cvs -Q co -d cvswork2 main >cvs.log 2>&1 &&
   test_cmp cvswork cvswork2 &&
   test -f "$SERVERDIR/butcvs1.ext.main.sqlite" &&
   test ! -f "$SERVERDIR/butcvs2.ext.main.sqlite" &&
   cmp "$SERVERDIR/butcvs.main.sqlite" "$SERVERDIR/butcvs1.ext.main.sqlite"'


#------------
# CVS UPDATE
#------------

rm -fr "$SERVERDIR"
cd "$WORKDIR" &&
but clone -q --bare "$WORKDIR/.but" "$SERVERDIR" >/dev/null 2>&1 &&
GIT_DIR="$SERVERDIR" but config --bool butcvs.enabled true &&
GIT_DIR="$SERVERDIR" but config butcvs.logfile "$SERVERDIR/butcvs.log" ||
exit 1

test_expect_success 'cvs update (create new file)' \
  'echo testfile1 >testfile1 &&
   but add testfile1 &&
   but cummit -q -m "Add testfile1" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test "$(echo $(grep testfile1 CVS/Entries|cut -d/ -f2,3,5))" = "testfile1/1.1/" &&
   test_cmp testfile1 ../testfile1'

cd "$WORKDIR"
test_expect_success 'cvs update (update existing file)' \
  'echo line 2 >>testfile1 &&
   but add testfile1 &&
   but cummit -q -m "Append to testfile1" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test "$(echo $(grep testfile1 CVS/Entries|cut -d/ -f2,3,5))" = "testfile1/1.2/" &&
   test_cmp testfile1 ../testfile1'

cd "$WORKDIR"
#TODO: cvsserver doesn't support update w/o -d
test_expect_failure "cvs update w/o -d doesn't create subdir (TODO)" '
   mkdir test &&
   echo >test/empty &&
   but add test &&
   but cummit -q -m "Single Subdirectory" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test ! -d test
'

cd "$WORKDIR"
test_expect_success 'cvs update (subdirectories)' \
  '(for dir in A A/B A/B/C A/D E; do
      mkdir $dir &&
      echo "test file in $dir" >"$dir/file_in_$(echo $dir|sed -e "s#/# #g")"  &&
      but add $dir || exit 1
   done) &&
   but cummit -q -m "deep sub directory structure" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update -d &&
   (for dir in A A/B A/B/C A/D E; do
      filename="file_in_$(echo $dir|sed -e "s#/# #g")" &&
      if test "$(echo $(grep -v ^D $dir/CVS/Entries|cut -d/ -f2,3,5))" = "$filename/1.1/" &&
	test_cmp "$dir/$filename" "../$dir/$filename"; then
        :
      else
	exit 1
      fi
    done)'

cd "$WORKDIR"
test_expect_success 'cvs update (delete file)' \
  'but rm testfile1 &&
   but cummit -q -m "Remove testfile1" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test -z "$(grep testfile1 CVS/Entries)" &&
   test ! -f testfile1'

cd "$WORKDIR"
test_expect_success 'cvs update (re-add deleted file)' \
  'echo readded testfile >testfile1 &&
   but add testfile1 &&
   but cummit -q -m "Re-Add testfile1" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test "$(echo $(grep testfile1 CVS/Entries|cut -d/ -f2,3,5))" = "testfile1/1.4/" &&
   test_cmp testfile1 ../testfile1'

cd "$WORKDIR"
test_expect_success 'cvs update (merge)' \
  'echo Line 0 >expected &&
   for i in 1 2 3 4 5 6 7
   do
     echo Line $i >>merge &&
     echo Line $i >>expected || return 1
   done &&
   echo Line 8 >>expected &&
   but add merge &&
   but cummit -q -m "Merge test (pre-merge)" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test "$(echo $(grep merge CVS/Entries|cut -d/ -f2,3,5))" = "merge/1.1/" &&
   test_cmp merge ../merge &&
   ( echo Line 0 && cat merge ) >merge.tmp &&
   mv merge.tmp merge &&
   cd "$WORKDIR" &&
   echo Line 8 >>merge &&
   but add merge &&
   but cummit -q -m "Merge test (merge)" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   sleep 1 && touch merge &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test_cmp merge ../expected'

cd "$WORKDIR"

cat >expected.C <<EOF
<<<<<<< merge.mine
Line 0
=======
LINE 0
>>>>>>> merge.1.3
EOF

for i in 1 2 3 4 5 6 7 8
do
  echo Line $i >>expected.C
done

test_expect_success 'cvs update (conflict merge)' \
  '( echo LINE 0 && cat merge ) >merge.tmp &&
   mv merge.tmp merge &&
   but add merge &&
   but cummit -q -m "Merge test (conflict)" &&
   but push butcvs.but >/dev/null &&
   cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update &&
   test_cmp merge ../expected.C'

cd "$WORKDIR"
test_expect_success 'cvs update (-C)' \
  'cd cvswork &&
   GIT_CONFIG="$but_config" cvs -Q update -C &&
   test_cmp merge ../merge'

cd "$WORKDIR"
test_expect_success 'cvs update (merge no-op)' \
   'echo Line 9 >>merge &&
    cp merge cvswork/merge &&
    but add merge &&
    but cummit -q -m "Merge test (no-op)" &&
    but push butcvs.but >/dev/null &&
    cd cvswork &&
    sleep 1 && touch merge &&
    GIT_CONFIG="$but_config" cvs -Q update &&
    test_cmp merge ../merge'

cd "$WORKDIR"
test_expect_success 'cvs update (-p)' '
    touch really-empty &&
    echo Line 1 > no-lf &&
    printf "Line 2" >> no-lf &&
    but add really-empty no-lf &&
    but cummit -q -m "Update -p test" &&
    but push butcvs.but >/dev/null &&
    cd cvswork &&
    GIT_CONFIG="$but_config" cvs update &&
    for i in merge no-lf empty really-empty; do
	GIT_CONFIG="$but_config" cvs update -p "$i" >$i.out &&
	test_cmp $i.out ../$i || return 1
    done
'

cd "$WORKDIR"
test_expect_success 'cvs update (module list supports packed refs)' '
    GIT_DIR="$SERVERDIR" but pack-refs --all &&
    GIT_CONFIG="$but_config" cvs -n up -d 2> out &&
    grep "cvs update: New directory \`main'\''" < out
'

#------------
# CVS STATUS
#------------

cd "$WORKDIR"
test_expect_success 'cvs status' '
    mkdir status.dir &&
    echo Line > status.dir/status.file &&
    echo Line > status.file &&
    but add status.dir status.file &&
    but cummit -q -m "Status test" &&
    but push butcvs.but >/dev/null &&
    cd cvswork &&
    GIT_CONFIG="$but_config" cvs update &&
    GIT_CONFIG="$but_config" cvs status | grep "^File: status.file" >../out &&
    test_line_count = 2 ../out
'

cd "$WORKDIR"
test_expect_success 'cvs status (nonrecursive)' '
    cd cvswork &&
    GIT_CONFIG="$but_config" cvs status -l | grep "^File: status.file" >../out &&
    test_line_count = 1 ../out
'

cd "$WORKDIR"
test_expect_success 'cvs status (no subdirs in header)' '
    cd cvswork &&
    GIT_CONFIG="$but_config" cvs status | grep ^File: >../out &&
    ! grep / <../out
'

#------------
# CVS CHECKOUT
#------------

cd "$WORKDIR"
test_expect_success 'cvs co -c (shows module database)' '
    GIT_CONFIG="$but_config" cvs co -c > out &&
    grep "^main[	 ][ 	]*main$" <out &&
    ! grep -v "^main[	 ][ 	]*main$" <out
'

#------------
# CVS LOG
#------------

# Known issues with but-cvsserver current log output:
#  - Hard coded "lines: +2 -3" placeholder, instead of real numbers.
#  - CVS normally does not internally add a blank first line
#    or a last line with nothing but a space to log messages.
#  - The latest cvs 1.12.x server sends +0000 timezone (with some hidden "MT"
#    tagging in the protocol), and if cvs 1.12.x client sees the MT tags,
#    it converts to local time zone.  but-cvsserver doesn't do the +0000
#    or the MT tags...
#  - The latest 1.12.x releases add a "cummitid:" field on to the end of the
#    "date:" line (after "lines:").  Maybe we could stick but's cummit id
#    in it?  Or does CVS expect a certain number of bits (too few for
#    a full sha1)?
#
# Given the above, expect the following test to break if but-cvsserver's
# log output is improved.  The test is just to ensure it doesn't
# accidentally get worse.

sed -e 's/^x//' -e 's/SP$/ /' > "$WORKDIR/expect" <<EOF
x
xRCS file: $WORKDIR/butcvs.but/main/merge,v
xWorking file: merge
xhead: 1.4
xbranch:
xlocks: strict
xaccess list:
xsymbolic names:
xkeyword substitution: kv
xtotal revisions: 4;	selected revisions: 4
xdescription:
x----------------------------
xrevision 1.4
xdate: __DATE__;  author: author;  state: Exp;  lines: +2 -3
x
xMerge test (no-op)
xSP
x----------------------------
xrevision 1.3
xdate: __DATE__;  author: author;  state: Exp;  lines: +2 -3
x
xMerge test (conflict)
xSP
x----------------------------
xrevision 1.2
xdate: __DATE__;  author: author;  state: Exp;  lines: +2 -3
x
xMerge test (merge)
xSP
x----------------------------
xrevision 1.1
xdate: __DATE__;  author: author;  state: Exp;  lines: +2 -3
x
xMerge test (pre-merge)
xSP
x=============================================================================
EOF
expectStat="$?"

cd "$WORKDIR"
test_expect_success 'cvs log' '
    cd cvswork &&
    test x"$expectStat" = x"0" &&
    GIT_CONFIG="$but_config" cvs log merge >../out &&
    sed -e "s%2[0-9][0-9][0-9]/[01][0-9]/[0-3][0-9] [0-2][0-9]:[0-5][0-9]:[0-5][0-9]%__DATE__%" ../out > ../actual &&
    test_cmp ../expect ../actual
'

#------------
# CVS ANNOTATE
#------------

cd "$WORKDIR"
test_expect_success 'cvs annotate' '
    cd cvswork &&
    GIT_CONFIG="$but_config" cvs annotate merge >../out &&
    sed -e "s/ .*//" ../out >../actual &&
    printf "1.%d\n" 3 1 1 1 1 1 1 1 2 4 >../expect &&
    test_cmp ../expect ../actual
'

#------------
# running via but-shell
#------------

cd "$WORKDIR"

test_expect_success 'create remote-cvs helper' '
	write_script remote-cvs <<-\EOF
	exec but shell -c "cvs server"
	EOF
'

test_expect_success 'cvs server does not run with vanilla but-shell' '
	(
		cd cvswork &&
		CVS_SERVER=$WORKDIR/remote-cvs &&
		export CVS_SERVER &&
		! cvs log merge
	)
'

test_expect_success 'configure but shell to run cvs server' '
	mkdir "$HOME"/but-shell-commands &&

	write_script "$HOME"/but-shell-commands/cvs <<-\EOF &&
	if ! test $# = 1 && test "$1" = "server"
	then
		echo >&2 "but-cvsserver only handles \"server\""
		exit 1
	fi
	exec but cvsserver server
	EOF

	# Should not be used, but part of the recommended setup
	write_script "$HOME"/but-shell-commands/no-interactive-login <<-\EOF
	echo Interactive login forbidden
	EOF
'

test_expect_success 'cvs server can run with recommended config' '
	(
		cd cvswork &&
		CVS_SERVER=$WORKDIR/remote-cvs &&
		export CVS_SERVER &&
		cvs log merge
	)
'

test_done
