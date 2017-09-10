#!/bin/sh
#
# Build Git
#

. ${0%/*}/lib-travisci.sh

make --jobs=2
