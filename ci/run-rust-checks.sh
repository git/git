#!/bin/sh

. ${0%/*}/lib.sh

set +x

if ! group "Check Rust formatting" cargo fmt --all --check
then
	RET=1
fi

exit $RET
