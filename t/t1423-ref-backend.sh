#!/bin/sh

test_description='Test reference backend URIs'

. ./test-lib.sh

# Run a git command with the provided reference storage. Reset the backend
# post running the command.
# Usage: run_with_uri <repo> <backend> <uri> <cmd>
#   <repo> is the relative path to the repo to run the command in.
#   <backend> is the original ref storage of the repo.
#   <uri> is the new URI to be set for the ref storage.
#   <cmd> is the git subcommand to be run in the repository.
run_with_uri () {
	repo=$1 &&
	backend=$2 &&
	uri=$3 &&
	cmd=$4 &&

	git -C "$repo" config set core.repositoryformatversion 1
	git -C "$repo" config set extensions.refStorage "$uri" &&
	git -C "$repo" $cmd &&
	git -C "$repo" config set extensions.refStorage "$backend"
}

# Test a repository with a given reference storage by running and comparing
# 'git refs list' before and after setting the new reference backend. If
# err_msg is set, expect the command to fail and grep for the provided err_msg.
# Usage: run_with_uri <repo> <backend> <uri> <cmd>
#   <repo> is the relative path to the repo to run the command in.
#   <backend> is the original ref storage of the repo.
#   <uri> is the new URI to be set for the ref storage.
#   <err_msg> (optional) if set, check if 'git-refs(1)' failed with the provided msg.
test_refs_backend () {
	repo=$1 &&
	backend=$2 &&
	uri=$3 &&
	err_msg=$4 &&

	git -C "$repo" config set core.repositoryformatversion 1 &&
	if test -n "$err_msg";
	then
		git -C "$repo" config set extensions.refStorage "$uri" &&
		test_must_fail git -C "$repo" refs list 2>err &&
		test_grep "$err_msg" err
	else
		git -C "$repo" refs list >expect &&
		run_with_uri "$repo" "$backend" "$uri" "refs list" >actual &&
		test_cmp expect actual
	fi
}

test_expect_success 'URI is invalid' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_refs_backend repo files "reftable@/home/reftable" \
		"invalid value for ${SQ}extensions.refstorage${SQ}"
'

test_expect_success 'URI ends with colon' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_refs_backend repo files "reftable:" \
		"invalid value for ${SQ}extensions.refstorage${SQ}"
'

test_expect_success 'unknown reference backend' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_refs_backend repo files "db://.git" \
		"invalid value for ${SQ}extensions.refstorage${SQ}"
'

ref_formats="files reftable"
for from_format in $ref_formats
do

for to_format in $ref_formats
do
	if test "$from_format" = "$to_format"
	then
		continue
	fi


	for dir in "$(pwd)/repo/.git" "."
	do

		test_expect_success "read from $to_format backend, $dir dir" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				BACKEND_PATH="$dir/$(sed "s/.* ${SQ}.git\/\(.*\)${SQ}/\1/" out)" &&
				test_refs_backend . $from_format "$to_format://$BACKEND_PATH" "$method"
			)
		'

		test_expect_success "write to $to_format backend, $dir dir" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				BACKEND_PATH="$dir/$(sed "s/.* ${SQ}.git\/\(.*\)${SQ}/\1/" out)" &&

				test_refs_backend . $from_format "$to_format://$BACKEND_PATH" &&

				git refs list >expect &&
				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" "tag -d 1" &&
				git refs list >actual &&
				test_cmp expect actual &&

				git refs list | grep -v "refs/tags/1" >expect &&
				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" "refs list" >actual &&
				test_cmp expect actual
			)
		'

		test_expect_success "with worktree and $to_format backend, $dir dir" '
			test_when_finished "rm -rf repo wt" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				BACKEND_PATH="$dir/$(sed "s/.* ${SQ}.git\/\(.*\)${SQ}/\1/" out)" &&

				git config set core.repositoryformatversion 1 &&
				git config set extensions.refStorage "$to_format://$BACKEND_PATH" &&

				git worktree add ../wt 2
			) &&

			git -C repo for-each-ref --include-root-refs >expect &&
			git -C wt for-each-ref --include-root-refs >expect &&
			! test_cmp expect actual &&

			git -C wt rev-parse 2 >expect &&
			git -C wt rev-parse HEAD >actual &&
			test_cmp expect actual
		'
	done # closes dir
done # closes to_format
done # closes from_format

test_done
