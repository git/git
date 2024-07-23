#!/bin/sh
#
# Perform style check
#

baseCommit=$1

git clang-format --style file --diff --extensions c,h "$baseCommit"
