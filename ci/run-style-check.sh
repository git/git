#!/bin/sh
#
# Perform style check
#

baseCommit=$1

git clang-format --style=file:.clang-format \
	--diff --extensions c,h "$baseCommit"
