#!/bin/sh
#
# Build and test Git
#

. ${0%/*}/lib-travisci.sh

ln -s $HOME/travis-cache/.prove t/.prove

make --jobs=2
make --quiet test

check_unignored_build_artifacts

save_good_tree
