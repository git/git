#!/bin/sh
#
# Test Git
#

. ${0%/*}/lib-travisci.sh

mkdir -p $HOME/travis-cache
ln -s $HOME/travis-cache/.prove t/.prove
make --quiet test
