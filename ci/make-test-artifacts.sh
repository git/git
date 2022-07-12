#!/bin/sh
#
# Build Git and store artifacts for testing
#

mkdir -p "$1" # in case ci/lib.sh decides to quit early

. ${0%/*}/lib.sh

group Build make artifacts-tar ARTIFACTS_DIRECTORY="$1"

check_unignored_build_artifacts
