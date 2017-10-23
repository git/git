#!/bin/sh
#
# Perform various static code analysis checks
#

. ${0%/*}/lib-travisci.sh

make coccicheck
