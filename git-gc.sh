#!/bin/sh
#
# Copyright (c) 2006, Shawn O. Pearce
#
# Cleanup unreachable files and optimize the repository.

USAGE=''
SUBDIRECTORY_OK=Yes
. git-sh-setup

git-pack-refs --prune &&
git-reflog expire --all &&
git-repack -a -d -l &&
git-prune &&
git-rerere gc || exit
