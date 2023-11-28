#!/bin/sh

test_description='handling of missing objects in rev-list'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# We setup the repository with two commits, this way HEAD is always
# available and we can hide commit 1.
test_expect_success 'create repository and alternate directory' '
	test_commit 1 &&
	test_commit 2 &&
	test_commit 3
'

for obj in "HEAD~1" "HEAD~1^{tree}" "HEAD:1.t"
do
	test_expect_success "rev-list --missing=error fails with missing object $obj" '
		oid="$(git rev-parse $obj)" &&
		path=".git/objects/$(test_oid_to_path $oid)" &&

		mv "$path" "$path.hidden" &&
		test_when_finished "mv $path.hidden $path" &&

		test_must_fail git rev-list --missing=error --objects \
			--no-object-names HEAD
	'
done

for obj in "HEAD~1" "HEAD~1^{tree}" "HEAD:1.t"
do
	for action in "allow-any" "print"
	do
		test_expect_success "rev-list --missing=$action with missing $obj" '
			oid="$(git rev-parse $obj)" &&
			path=".git/objects/$(test_oid_to_path $oid)" &&

			# Before the object is made missing, we use rev-list to
			# get the expected oids.
			git rev-list --objects --no-object-names \
				HEAD ^$obj >expect.raw &&

			# Blobs are shared by all commits, so evethough a commit/tree
			# might be skipped, its blob must be accounted for.
			if [ $obj != "HEAD:1.t" ]; then
				echo $(git rev-parse HEAD:1.t) >>expect.raw &&
				echo $(git rev-parse HEAD:2.t) >>expect.raw
			fi &&

			mv "$path" "$path.hidden" &&
			test_when_finished "mv $path.hidden $path" &&

			git rev-list --missing=$action --objects --no-object-names \
				HEAD >actual.raw &&

			# When the action is to print, we should also add the missing
			# oid to the expect list.
			case $action in
			allow-any)
				;;
			print)
				grep ?$oid actual.raw &&
				echo ?$oid >>expect.raw
				;;
			esac &&

			sort actual.raw >actual &&
			sort expect.raw >expect &&
			test_cmp expect actual
		'
	done
done

test_done
