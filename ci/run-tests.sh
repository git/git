#!/bin/sh
#
# Test Git
#

. ${0%/*}/lib-travisci.sh

ln -s "$cache_dir/.prove" t/.prove

make --quiet test

check_unignored_build_artifacts

save_good_tree
