#!/bin/sh
#
# Test Git
#

. ${0%/*}/lib-travisci.sh

mkdir -p $HOME/travis-cache
ln -s $HOME/travis-cache/.prove t/.prove
make --quiet test
if test "$jobname" = "linux-gcc"
then
	GIT_TEST_SPLIT_INDEX=YesPlease make --quiet test
fi
