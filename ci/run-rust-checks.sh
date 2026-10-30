#!/bin/sh

. ${0%/*}/lib.sh

set +x

if ! group "Check Rust formatting" cargo fmt --manifest-path rust/Cargo.toml --all --check
then
	RET=1
fi

if ! group "Check for common Rust mistakes" cargo clippy --manifest-path rust/Cargo.toml --all-targets --all-features -- -Dwarnings
then
	RET=1
fi

if ! group "Check for minimum required Rust version" cargo msrv --path rust verify
then
	RET=1
fi

exit $RET
