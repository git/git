#!/bin/sh
#
# Test Git
#

. ${0%/*}/lib-travisci.sh

ln -s $HOME/travis-cache/.prove t/.prove
make --quiet test
if test "$jobname" = "linux-gcc"
then
	GIT_TEST_SPLIT_INDEX=YesPlease make --quiet test
fi

check_unignored_build_artifacts

save_good_tree
