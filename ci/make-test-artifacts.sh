#!/bin/sh
#
# Build Git and store artifacts for testing
#

mkdir -p "$1" # in case ci/lib.sh decides to quit early

. ${0%/*}/lib.sh

if [ -z "$CARGO_HOME" ]; then
  echo >&2 "::error:: CARGO_HOME is not set"
  exit 1
fi

export PATH="$CARGO_HOME/bin:$PATH"

group Build make artifacts-tar ARTIFACTS_DIRECTORY="$1"

check_unignored_build_artifacts
