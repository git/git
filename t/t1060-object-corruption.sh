#!/bin/sh

test_description='see how we handle various forms of corruption'

. ./test-lib.sh

# convert "1234abcd" to ".git/objects/12/34abcd"
obj_to_file() {
	echo "$(git rev-parse --git-dir)/objects/$(git rev-parse "$1" | sed 's,..,&/,')"
}

# Convert byte at offset "$2" of object "$1" into '\0'
corrupt_byte() {
	obj_file=$(obj_to_file "$1") &&
	chmod +w "$obj_file" &&
	printf '\0' | dd of="$obj_file" bs=1 seek="$2" conv=notrunc
}

test_expect_success 'setup corrupt repo' '
	git init bit-error &&
	(
		cd bit-error &&
		test_commit content &&
		corrupt_byte HEAD:content.t 10
	) &&
	git init no-bit-error &&
	(
		# distinct commit from bit-error, but containing a
		# non-corrupted version of the same blob
		cd no-bit-error &&
		test_tick &&
		test_commit content
	)
'

test_expect_success 'setup repo with missing object' '
	git init missing &&
	(
		cd missing &&
		test_commit content &&
		rm -f "$(obj_to_file HEAD:content.t)"
	)
'

test_expect_success 'setup repo with misnamed object' '
	git init misnamed &&
	(
		cd misnamed &&
		test_commit content &&
		good=$(obj_to_file HEAD:content.t) &&
		blob=$(echo corrupt | git hash-object -w --stdin) &&
		bad=$(obj_to_file $blob) &&
		rm -f "$good" &&
		mv "$bad" "$good"
	)
'

test_expect_success 'streaming a corrupt blob fails' '
	(
		cd bit-error &&
		test_must_fail git cat-file blob HEAD:content.t
	)
'

test_expect_success 'getting type of a corrupt blob fails' '
	(
		cd bit-error &&
		test_must_fail git cat-file -s HEAD:content.t
	)
'

test_expect_success 'read-tree -u detects bit-errors in blobs' '
	(
		cd bit-error &&
		rm -f content.t &&
		test_must_fail git read-tree --reset -u HEAD
	)
'

test_expect_success 'read-tree -u detects missing objects' '
	(
		cd missing &&
		rm -f content.t &&
		test_must_fail git read-tree --reset -u HEAD
	)
'

# We use --bare to make sure that the transport detects it, not the checkout
# phase.
test_expect_success 'clone --no-local --bare detects corruption' '
	test_must_fail git clone --no-local --bare bit-error corrupt-transport
'

test_expect_success 'clone --no-local --bare detects missing object' '
	test_must_fail git clone --no-local --bare missing missing-transport
'

test_expect_success 'clone --no-local --bare detects misnamed object' '
	test_must_fail git clone --no-local --bare misnamed misnamed-transport
'

# We do not expect --local to detect corruption at the transport layer,
# so we are really checking the checkout() code path.
test_expect_success 'clone --local detects corruption' '
	test_must_fail git clone --local bit-error corrupt-checkout
'

test_expect_success 'error detected during checkout leaves repo intact' '
	test_path_is_dir corrupt-checkout/.git
'

test_expect_success 'clone --local detects missing objects' '
	test_must_fail git clone --local missing missing-checkout
'

test_expect_failure 'clone --local detects misnamed objects' '
	test_must_fail git clone --local misnamed misnamed-checkout
'

test_expect_success 'fetch into corrupted repo with index-pack' '
	cp -R bit-error bit-error-cp &&
	test_when_finished "rm -rf bit-error-cp" &&
	(
		cd bit-error-cp &&
		test_must_fail git -c transfer.unpackLimit=1 \
			fetch ../no-bit-error 2>stderr &&
		test_grep ! -i collision stderr
	)
'

test_expect_success 'internal tree objects are not "missing"' '
	git init missing-empty &&
	(
		cd missing-empty &&
		empty_tree=$(git hash-object -t tree /dev/null) &&
		commit=$(echo foo | git commit-tree $empty_tree) &&
		git rev-list --objects $commit
	)
'

test_expect_success 'partial clone of corrupted repository' '
	test_config -C misnamed uploadpack.allowFilter true &&
	git clone --no-local --no-checkout --filter=blob:none \
		misnamed corrupt-partial && \
	test_must_fail git -C corrupt-partial checkout --force
'

test_done
