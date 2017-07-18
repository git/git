#!/bin/sh

# git executable wrapper script for Git-Mediawiki to run tests without
# installing all the scripts and perl packages.

GIT_ROOT_DIR=../../..
GIT_EXEC_PATH=$(cd "$(dirname "$0")" && cd ${GIT_ROOT_DIR} && pwd)

GITPERLLIB="$GIT_EXEC_PATH"'/contrib/mw-to-git'"${GITPERLLIB:+:$GITPERLLIB}"
PATH="$GIT_EXEC_PATH"'/contrib/mw-to-git:'"$PATH"

export GITPERLLIB PATH

exec "${GIT_EXEC_PATH}/bin-wrappers/git" "$@"
