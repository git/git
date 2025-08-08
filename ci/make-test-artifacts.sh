#!/bin/sh
#
# Build Git and store artifacts for testing
#

mkdir -p "$1" # in case ci/lib.sh decides to quit early

. ${0%/*}/lib.sh

## install rust per user rather than system wide
. ${0%/*}/install-rust.sh

group Build make artifacts-tar ARTIFACTS_DIRECTORY="$1"

if [ -d "$CARGO_HOME" ]; then
  rm -rf $CARGO_HOME
fi

check_unignored_build_artifacts
