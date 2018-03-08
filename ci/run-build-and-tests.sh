#!/bin/sh
#
# Build and test Git
#

. ${0%/*}/lib-travisci.sh

ln -s "$cache_dir/.prove" t/.prove

make --jobs=2
make --quiet test
if test "$jobname" = "linux-gcc"
then
	GIT_TEST_SPLIT_INDEX=YesPlease make --quiet test
fi

check_unignored_build_artifacts

save_good_tree
