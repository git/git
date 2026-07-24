#!/bin/sh

test_description='test index-pack handling of delta cycles in packfiles'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

# Two similar-ish objects that we have computed deltas between.
A=$(test_oid packlib_7_0)
B=$(test_oid packlib_7_76)

# Copy the entries from a complete pack without its header or trailer.
pack_entries () {
	entry_size=$(wc -c <"$1") &&
	dd if="$1" bs=1 skip=12 \
		count=$((entry_size - 12 - $(test_oid rawsz))) 2>/dev/null
}

# B as an OFS_DELTA against A at the given one-byte distance.
pack_obj_b_ofs_a () {
	pack_obj "$B" "$A" >b-ref.tmp &&
	printf "\145" &&
	printf "\\$(printf "%03o" "$1")" &&
	dd if=b-ref.tmp bs=1 skip=$((1 + $(test_oid rawsz))) 2>/dev/null
}

# Return the base of the first one-byte-header REF_DELTA for the given OID.
first_ref_base () {
	idx=$(echo .git/objects/pack/*.idx) &&
	offset=$(git show-index <"$idx" |
		awk -v oid="$1" '$2 == oid { print $1; exit }') &&
	dd if="${idx%.idx}.pack" bs=1 skip=$((offset + 1)) \
		count=$(test_oid rawsz) 2>/dev/null |
	test-tool hexdump |
	tr -d " \n"
}

# The order of equal-OID entries in the .idx is unspecified. Retain a pack
# which selects $1 as a delta against $2. Unless $3 is "-", also require the
# first copy searched during recovery to be a REF_DELTA against $3.
install_cycle () {
	cycle_oid=$1 &&
	cycle_base=$2 &&
	first_base=$3 &&
	shift 3 &&
	for pack
	do
		clear_packs &&
		git index-pack --fix-thin --stdin <"$pack" &&
		selected_base=$(echo "$cycle_oid" |
			git cat-file --batch-check="%(deltabase)") ||
		return 1
		if test "$selected_base" = "$cycle_base" &&
		   { test "$first_base" = "-" ||
		     test "$(first_ref_base "$cycle_oid")" = "$first_base"; }
		then
			return 0
		fi
	done
	return 1
}

make_cycle_pack () {
	cycle_pack=$1 &&
	shift &&
	test-tool -C alt-source pack-deltas --num-objects=6 >refs.tmp <<-EOF &&
	REF_DELTA $T $X
	REF_DELTA $X $1
	REF_DELTA $Y $Z
	REF_DELTA $Z $X
	REF_DELTA $X $2
	REF_DELTA $X $3
	EOF
	{
		pack_header 8 &&
		pack_entries refs.tmp &&
		cat a-full &&
		pack_obj_b_ofs_a "$a_full_size"
	} >"$cycle_pack" &&
	pack_trailer "$cycle_pack"
}

check_blob () {
	test "$(git cat-file -t "$1")" = blob &&
	git cat-file blob "$1" >actual &&
	test_cmp_bin "$2" actual
}

# double-check our hand-constucted packs
test_expect_success 'index-pack works with a single delta (A->B)' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A $B &&
		pack_obj $B
	} >ab.pack &&
	pack_trailer ab.pack &&
	git index-pack --stdin <ab.pack &&
	git cat-file -t $A &&
	git cat-file -t $B
'

test_expect_success 'index-pack works with a single delta (B->A)' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A &&
		pack_obj $B $A
	} >ba.pack &&
	pack_trailer ba.pack &&
	git index-pack --stdin <ba.pack &&
	git cat-file -t $A &&
	git cat-file -t $B
'

test_expect_success 'index-pack detects missing base objects' '
	clear_packs &&
	{
		pack_header 1 &&
		pack_obj $A $B
	} >missing.pack &&
	pack_trailer missing.pack &&
	test_must_fail git index-pack --fix-thin --stdin <missing.pack
'

test_expect_success 'index-pack detects REF_DELTA cycles' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A $B &&
		pack_obj $B $A
	} >cycle.pack &&
	pack_trailer cycle.pack &&
	test_must_fail git index-pack --fix-thin --stdin <cycle.pack
'

test_expect_success 'failover to an object in another pack' '
	clear_packs &&
	git index-pack --stdin <ab.pack &&

	# This cycle does not fail since the existence of A & B in
	# the repo allows us to resolve the cycle.
	git index-pack --stdin --fix-thin <cycle.pack
'

test_expect_success 'failover to a duplicate object in the same pack' '
	{
		pack_header 3 &&
		pack_obj $A &&
		pack_obj $B $A &&
		pack_obj $A $B
	} >recoverable-1.pack &&
	pack_trailer recoverable-1.pack &&
	{
		pack_header 3 &&
		pack_obj $A $B &&
		pack_obj $B $A &&
		pack_obj $A
	} >recoverable-2.pack &&
	pack_trailer recoverable-2.pack &&

	# The selected copy of A is part of the cycle, but the full copy
	# lets both type and content lookups resolve it.
	install_cycle "$A" "$B" - recoverable-1.pack recoverable-2.pack &&
	printf "\7\0" >expect &&
	check_blob "$A" expect
'

test_expect_success 'failover from a mixed REF/OFS cycle' '
	pack_obj "$A" "$B" >a-ref &&
	pack_obj "$B" >b-full &&
	a_ref_size=$(wc -c <a-ref) &&
	b_full_size=$(wc -c <b-full) &&

	{
		pack_header 3 &&
		cat a-ref &&
		cat b-full &&
		pack_obj_b_ofs_a "$((a_ref_size + b_full_size))"
	} >mixed-1.pack &&
	pack_trailer mixed-1.pack &&
	{
		pack_header 3 &&
		cat a-ref &&
		pack_obj_b_ofs_a "$a_ref_size" &&
		cat b-full
	} >mixed-2.pack &&
	pack_trailer mixed-2.pack &&

	# The REF_DELTA for A selects the OFS_DELTA copy of B; the
	# full B is its escape.
	install_cycle "$B" "$A" - mixed-1.pack mixed-2.pack &&
	printf "\7\0" >expect &&
	check_blob "$A" expect
'

test_expect_success 'failover after a tail into a three-object delta cycle' '
	git init alt-source &&
	printf "\7\76" |
		git -C alt-source hash-object -w --stdin >/dev/null &&
	X=$(printf x | git -C alt-source hash-object -w --stdin) &&
	Y=$(printf y | git -C alt-source hash-object -w --stdin) &&
	Z=$(printf z | git -C alt-source hash-object -w --stdin) &&
	printf "tail T\n" >tail &&
	T=$(git -C alt-source hash-object -w --stdin <tail) &&

	pack_obj "$A" >a-full &&
	a_full_size=$(wc -c <a-full) &&
	make_cycle_pack alternate-1.pack "$B" "$Y" "$Y" &&
	make_cycle_pack alternate-2.pack "$Y" "$B" "$Y" &&
	make_cycle_pack alternate-3.pack "$Y" "$Y" "$B" &&

	# Lookup of T follows T->X->Y->Z->X. Recovery must exhaust that
	# branch, then use X->B->A, whose final edge is an OFS_DELTA.
	install_cycle "$X" "$Y" "$Y" \
		alternate-1.pack alternate-2.pack alternate-3.pack &&
	check_blob "$T" tail
'

test_expect_success 'index-pack works with thin pack A->B->C with B on disk' '
	git init server &&
	(
		cd server &&
		test_commit_bulk 4
	) &&

	A=$(git -C server rev-parse HEAD^{tree}) &&
	B=$(git -C server rev-parse HEAD~1^{tree}) &&
	C=$(git -C server rev-parse HEAD~2^{tree}) &&
	git -C server reset --hard HEAD~1 &&

	test-tool -C server pack-deltas --num-objects=2 >thin.pack <<-EOF &&
	REF_DELTA $A $B
	REF_DELTA $B $C
	EOF

	git clone "file://$(pwd)/server" client &&
	(
		cd client &&
		git index-pack --fix-thin --stdin <../thin.pack
	)
'

test_done
