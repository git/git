#!/bin/sh

. git-sh-setup

echo $(find "$GIT_DIR/objects"/?? -type f -print | wc -l) objects, \
$({
    echo 0
    # "no-such" is to help Darwin folks by not using xargs -r.
    find "$GIT_DIR/objects"/?? -type f -print 2>/dev/null |
    xargs du -k "$GIT_DIR/objects/no-such" 2>/dev/null |
    sed -e 's/[ 	].*/ +/'
    echo p
} | dc) kilobytes
