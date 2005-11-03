#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Pretend we resolved the heads, but declare our tree trumps everybody else.
#

# We need to exit with 2 if the index does not match our HEAD tree,
# because the current index is what we will be committing as the
# merge result.

test "$(git-diff-index --cached --name-status HEAD)" = "" || exit 2

exit 0
