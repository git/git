#!/bin/sh

test_description='run clar unit tests'
test_external=t
. ./test-lib.sh

# We store the binary in different locations for make vs meson. Yuck.
if test -d "$GIT_BUILD_DIR/t/unit-tests"
then
	UNIT_TESTS="$GIT_BUILD_DIR/t/unit-tests/bin/unit-tests"
else
	UNIT_TESTS="$GIT_BUILD_DIR/t/unit-tests"
fi

# Pass along any options that are meaningful to the unit-test binary here.
"$UNIT_TESTS" \
	${immediate:+-i} \
	|| error "unit-test binary failed"

test_done
