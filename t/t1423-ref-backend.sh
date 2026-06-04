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
#   <via> if 'config', set the backend via the 'extensions.refStorage' config.
#         if 'env', set the backend via the 'GIT_REFERENCE_BACKEND' env.
run_with_uri () {
	repo=$1 &&
	backend=$2 &&
	uri=$3 &&
	cmd=$4 &&
	via=$5 &&

	git -C "$repo" config set core.repositoryformatversion 1 &&
	if test "$via" = "env"
	then
		test_env GIT_REFERENCE_BACKEND="$uri" git -C "$repo" $cmd
	elif test "$via" = "config"
	then
		git -C "$repo" config set extensions.refStorage "$uri" &&
		git -C "$repo" $cmd &&
		git -C "$repo" config set extensions.refStorage "$backend"
	fi
}

# Test a repository with a given reference storage by running and comparing
# 'git refs list' before and after setting the new reference backend. If
# err_msg is set, expect the command to fail and grep for the provided err_msg.
# Usage: run_with_uri <repo> <backend> <uri> <cmd>
#   <repo> is the relative path to the repo to run the command in.
#   <backend> is the original ref storage of the repo.
#   <uri> is the new URI to be set for the ref storage.
#   <via> if 'config', set the backend via the 'extensions.refStorage' config.
#         if 'env', set the backend via the 'GIT_REFERENCE_BACKEND' env.
#   <err_msg> (optional) if set, check if 'git-refs(1)' failed with the provided msg.
test_refs_backend () {
	repo=$1 &&
	backend=$2 &&
	uri=$3 &&
	via=$4 &&
	err_msg=$5 &&


	if test -n "$err_msg";
	then
		if test "$via" = "env"
		then
			test_env GIT_REFERENCE_BACKEND="$uri" test_must_fail git -C "$repo" refs list 2>err
		elif test "$via" = "config"
		then
			git -C "$repo" config set extensions.refStorage "$uri" &&
			test_must_fail git -C "$repo" refs list 2>err &&
			test_grep "$err_msg" err
		fi
	else
		git -C "$repo" refs list >expect &&
		run_with_uri "$repo" "$backend" "$uri" "refs list" "$via">actual &&
		test_cmp expect actual
	fi
}

# Verify that the expected files are present in the gitdir and the refsdir.
# Usage: verify_files_exist <gitdir> <refdir>
#   <gitdir> is the path for the gitdir.
#   <refdir> is the path for the refdir.
verify_files_exist () {
	gitdir=$1 &&
	refdir=$2 &&

	# verify that the stubs were added to the $GITDIR.
	echo "repository uses alternate refs storage" >expect &&
	test_cmp expect $gitdir/refs/heads &&
	echo "ref: refs/heads/.invalid" >expect &&
	test_cmp expect $gitdir/HEAD

	# verify that backend specific files exist.
	case "$GIT_DEFAULT_REF_FORMAT" in
	files)
		test_path_is_dir $refdir/refs/heads &&
		test_path_is_file $refdir/HEAD;;
	reftable)
		test_path_is_dir $refdir/reftable &&
		test_path_is_file $refdir/reftable/tables.list;;
	*)
		BUG "unhandled ref format $GIT_DEFAULT_REF_FORMAT";;
	esac
}

methods="config env"
for method in $methods
do

test_expect_success "$method: URI is invalid" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_refs_backend repo files "reftable@/home/reftable" "$method" \
		"invalid value for ${SQ}extensions.refstorage${SQ}"
'

test_expect_success "$method: URI ends with colon" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_refs_backend repo files "reftable:" "$method" \
		"invalid value for ${SQ}extensions.refstorage${SQ}"
'

test_expect_success "$method: unknown reference backend" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_refs_backend repo files "db://.git" "$method" \
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

		test_expect_success "$method: read from $to_format backend, $dir dir" '
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

		test_expect_success "$method: write to $to_format backend, $dir dir" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				BACKEND_PATH="$dir/$(sed "s/.* ${SQ}.git\/\(.*\)${SQ}/\1/" out)" &&

				test_refs_backend . $from_format "$to_format://$BACKEND_PATH" "$method" &&

				git refs list >expect &&
				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" \
					"tag -d 1" "$method" &&
				git refs list >actual &&
				test_cmp expect actual &&

				git refs list | grep -v "refs/tags/1" >expect &&
				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" \
					"refs list" "$method" >actual &&
				test_cmp expect actual
			)
		'

		test_expect_success "$method: with worktree and $to_format backend, $dir dir" '
			test_when_finished "rm -rf repo wt" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				BACKEND_PATH="$dir/$(sed "s/.* ${SQ}.git\/\(.*\)${SQ}/\1/" out)" &&

				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" \
					"worktree add ../wt 2" "$method" &&

				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" \
					"for-each-ref --include-root-refs" "$method" >actual &&
				run_with_uri ../wt "$from_format" "$to_format://$BACKEND_PATH" \
					"for-each-ref --include-root-refs" "$method" >expect &&
				! test_cmp expect actual &&

				run_with_uri . "$from_format" "$to_format://$BACKEND_PATH" \
					"rev-parse 2" "$method" >actual &&
				run_with_uri ../wt "$from_format" "$to_format://$BACKEND_PATH" \
					"rev-parse HEAD" "$method" >expect &&
				test_cmp expect actual
			)
		'
	done # closes dir

	test_expect_success "migrating repository to $to_format with alternate refs directory" '
		test_when_finished "rm -rf repo refdir" &&
		mkdir refdir &&
		GIT_REFERENCE_BACKEND="${from_format}://$(pwd)/refdir" git init repo &&
		(
			cd repo &&

			test_commit 1 &&
			test_commit 2 &&
			test_commit 3 &&

			git refs migrate --ref-format=$to_format &&
			git refs list >out &&
			test_grep "refs/tags/1"	out &&
			test_grep "refs/tags/2"	out &&
			test_grep "refs/tags/3"	out
		)
	'

done # closes to_format
done # closes from_format

done # closes method

test_expect_success 'initializing repository with alt ref directory' '
	test_when_finished "rm -rf repo refdir" &&
	mkdir refdir &&
	BACKEND="$(test_detect_ref_format)://$(pwd)/refdir" &&
	GIT_REFERENCE_BACKEND=$BACKEND git init repo &&
	verify_files_exist repo/.git refdir &&
	(
		cd repo &&

		git config get extensions.refstorage >actual &&
		echo $BACKEND >expect &&
		test_cmp expect actual &&

		test_commit 1 &&
		test_commit 2 &&
		test_commit 3 &&
		git refs list >out &&
		test_grep "refs/tags/1"	out &&
		test_grep "refs/tags/2"	out &&
		test_grep "refs/tags/3"	out
	)
'

test_expect_success 'cloning repository with alt ref directory' '
	test_when_finished "rm -rf source repo refdir" &&
	mkdir refdir &&

	git init source &&
	test_commit -C source 1 &&
	test_commit -C source 2 &&
	test_commit -C source 3 &&

	BACKEND="$(test_detect_ref_format)://$(pwd)/refdir" &&
	GIT_REFERENCE_BACKEND=$BACKEND git clone source repo &&

	git -C repo config get extensions.refstorage >actual &&
	echo $BACKEND >expect &&
	test_cmp expect actual &&

	verify_files_exist repo/.git refdir &&

	git -C source for-each-ref refs/tags/ >expect &&
	git -C repo for-each-ref refs/tags/ >actual &&
	test_cmp expect actual
'

test_done
