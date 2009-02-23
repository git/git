#!/bin/sh

. ./test-lib.sh

unset CVS_SERVER
# for clean cvsps cache
HOME=$(pwd)
export HOME

if ! type cvs >/dev/null 2>&1
then
	say 'skipping cvsimport tests, cvs not found'
	test_done
	exit
fi

cvsps_version=`cvsps -h 2>&1 | sed -ne 's/cvsps version //p'`
case "$cvsps_version" in
2.1 | 2.2*)
	;;
'')
	say 'skipping cvsimport tests, cvsps not found'
	test_done
	exit
	;;
*)
	say 'skipping cvsimport tests, unsupported cvsps version'
	test_done
	exit
	;;
esac
