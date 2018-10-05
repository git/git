#!/bin/sh

# Structure of the test cvs repository
#
# Message   File:Content         Commit Time
# Rev 1     a: 1.1               2009-02-21 19:11:43 +0100
# Rev 2     a: 1.2    b: 1.1     2009-02-21 19:11:14 +0100
# Rev 3               b: 1.2     2009-02-21 19:11:43 +0100
#
# As you can see the commit of Rev 3 has the same time as
# Rev 1 this leads to a broken import because of a cvsps
# bug.

test_description='git cvsimport testing for correct patchset estimation'
. ./lib-cvs.sh

setup_cvs_test_repository t9603

test_expect_failure PERL 'import with criss cross times on revisions' '

    git cvsimport -p"-x" -C module-git module &&
    (cd module-git &&
        git log --pretty=format:%s > ../actual-master &&
        git log A~2..A --pretty="format:%s %ad" -- > ../actual-A &&
        echo "" >> ../actual-master &&
	echo "" >> ../actual-A
    ) &&
    echo "Rev 4
Rev 3
Rev 2
Rev 1" > expect-master &&
    test_cmp expect-master actual-master &&

    echo "Rev 5 Branch A Wed Mar 11 19:09:10 2009 +0000
Rev 4 Branch A Wed Mar 11 19:03:52 2009 +0000" > expect-A &&
    test_cmp expect-A actual-A
'

test_done
