#!/bin/sh

echo >&2 "fatal: but was built without support for $(basename $0) (@@REASON@@)."
exit 128
