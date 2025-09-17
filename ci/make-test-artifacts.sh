#!/bin/sh
#
# Build Git and store artifacts for testing
#

mkdir -p "$1" # in case ci/lib.sh decides to quit early

. ${0%/*}/lib.sh

## ensure rustup is in the PATH variable
if [ "$CARGO_HOME" = "" ]; then
  echo >&2 "::error:: CARGO_HOME is not set"
  exit 2
fi
export PATH="$CARGO_HOME/bin:$PATH"

rustc -vV

group Build make artifacts-tar ARTIFACTS_DIRECTORY="$1"

check_unignored_build_artifacts
