#!/bin/sh

test_description='test sha1 collision detection'

. ./test-lib.sh
TEST_DATA="$TEST_DIRECTORY/t0013"

test_lazy_prereq SHA1_IS_SHA1DC 'test-tool sha1-is-sha1dc'

if ! test_have_prereq SHA1_IS_SHA1DC
then
	skip_all='skipping sha1 collision tests, not using sha1collisiondetection'
	test_done
fi

test_lazy_prereq SHA1DC_RS '
	test rust = "$(GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=rust${SQ}" \
		test-tool sha1-is-sha1dc --backend)"
'

test_lazy_prereq SHA1DC_ACCEL '
	test -n "$(test-tool sha1dc-backends)"
'

test_expect_success 'original C SHA1DC remains the default' '
	test c = "$(test-tool sha1-is-sha1dc --backend)"
'

test_expect_success 'test-sha1 detects shattered pdf' '
	test_must_fail test-tool sha1 <"$TEST_DATA/shattered-1.pdf" 2>err &&
	test_grep collision err &&
	test_grep 38762cf7f55934b34d179ae6a4c80cadccbb7f0a err &&
	if test_have_prereq SHA1DC_RS
	then
		test_must_fail env \
			GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=rust${SQ}" \
			test-tool sha1 <"$TEST_DATA/shattered-1.pdf" 2>err &&
		test_grep collision err &&
		test_grep 38762cf7f55934b34d179ae6a4c80cadccbb7f0a err
	fi
'

test_expect_success SHA1DC_RS 'select SHA1DC backend via config' '
	test_config core.sha1dcBackend rust &&
	test rust = "$(test-tool sha1-is-sha1dc --backend)" &&
	test_config core.sha1dcBackend c &&
	test c = "$(test-tool sha1-is-sha1dc --backend)"
'

test_expect_success !SHA1DC_RS 'reject uncompiled Rust SHA1DC backend' '
	test_must_fail env \
		GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=rust${SQ}" \
		test-tool sha1-is-sha1dc --backend 2>err &&
	test_grep "unavailable or invalid SHA-1 backend" err
'

test_expect_success SHA1DC_ACCEL 'select accelerated C via config' '
	test_config core.sha1dcBackend accel &&
	test accel = "$(test-tool sha1-is-sha1dc --backend)"
'

test_expect_success 'reject invalid SHA1DC backend' '
	test_must_fail env \
		GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=invalid${SQ}" \
		test-tool sha1-is-sha1dc --backend 2>err &&
	test_grep "unavailable or invalid SHA-1 backend" err
'

test_expect_success 'test-sha1 detects SHA-mbles chosen-prefix collision' '
	test_must_fail test-tool sha1 <"$TEST_DATA/sha-mbles-1.bin" 2>err &&
	test_grep collision err &&
	test_grep 8ac60ba76f1999a1ab70223f225aefdc78d4ddc0 err
'

# Each implementation of the detection this build and CPU can use.
for backend in $(test-tool sha1dc-backends)
do
	test_expect_success "$backend: detects collisions" '
		test_must_fail env \
			GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=c${SQ}" \
			GIT_TEST_SHA1DC_BACKEND=$backend \
			test-tool sha1 <"$TEST_DATA/shattered-1.pdf" 2>err &&
		test_grep 38762cf7f55934b34d179ae6a4c80cadccbb7f0a err &&
		test_must_fail env \
			GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=c${SQ}" \
			GIT_TEST_SHA1DC_BACKEND=$backend \
			test-tool sha1 <"$TEST_DATA/sha-mbles-1.bin" 2>err &&
		test_grep 8ac60ba76f1999a1ab70223f225aefdc78d4ddc0 err
	'

	# In every combination of settings, and with the input split around
	# each block, it must find what sha1dc/ finds and give its digest.
	test_expect_success "$backend: agrees with sha1dc/ on collisions" '
		test_copy_bytes 320 <"$TEST_DATA/shattered-1.pdf" >shattered &&
		for f in shattered "$TEST_DATA/sha-mbles-1.bin"
		do
			GIT_TEST_SHA1DC_BACKEND=$backend \
				test-tool sha1dc-compare "$f" >out &&
			test_grep ! "detect=1 .* collision=0" out &&
			test_grep ! "detect=0 .* collision=1" out || return 1
		done &&

		# A reduced-round collision counts only when asked for.
		GIT_TEST_SHA1DC_BACKEND=$backend test-tool sha1dc-compare \
			"$TEST_DATA/sha1-reduced-round.bin" >out &&
		grep "collision=1" out >actual &&
		grep "detect=1 .* reduced=1 collision=1" out >expect &&
		test_line_count = 4 expect &&
		test_cmp expect actual
	'

	test_expect_success "$backend: hashes like the others" '
		test-tool genrandom "$backend" 100000 >data &&
		GIT_CONFIG_PARAMETERS="${SQ}core.sha1dcBackend=c${SQ}" \
			GIT_TEST_SHA1DC_BACKEND=$backend \
			test-tool sha1 <data >actual &&
		test-tool sha1 <data >expect &&
		test_cmp expect actual
	'
done

test_done
