#!/bin/sh

test_description='test git repo-info'

. ./test-lib.sh

# Test whether a key-value pair is correctly returned
#
# Usage: test_repo_info <label> <init command> <repo_name> <key> <expected value>
#
# Arguments:
#   label: the label of the test
#   init_command: a command which creates a repository
#   repo_name: the name of the repository that will be created in init_command
#   key: the key of the field that is being tested
#   expected_value: the value that the field should contain
test_repo_info () {
	label=$1
	init_command=$2
	repo_name=$3
	key=$4
	expected_value=$5

	test_expect_success "setup: $label" '
		eval "$init_command $repo_name"
	'

	test_expect_success "lines: $label" '
		echo "$key=$expected_value" > expect &&
		git -C "$repo_name" repo info "$key" >actual &&
		test_cmp expect actual
	'

	test_expect_success "nul: $label" '
		printf "%s\n%s\0" "$key" "$expected_value" >expect &&
		git -C "$repo_name" repo info --format=nul "$key" >actual &&
		test_cmp_bin expect actual
	'
}

test_repo_info 'ref format files is retrieved correctly' \
	'git init --ref-format=files' 'format-files' 'references.format' 'files'

test_repo_info 'ref format reftable is retrieved correctly' \
	'git init --ref-format=reftable' 'format-reftable' 'references.format' 'reftable'

test_repo_info 'bare repository = false is retrieved correctly' \
	'git init' 'nonbare' 'layout.bare' 'false'

test_repo_info 'bare repository = true is retrieved correctly' \
	'git init --bare' 'bare' 'layout.bare' 'true'

test_repo_info 'shallow repository = false is retrieved correctly' \
	'git init' 'nonshallow' 'layout.shallow' 'false'

test_expect_success 'setup remote' '
	git init remote &&
	echo x >remote/x &&
	git -C remote add x &&
	git -C remote commit -m x
'

test_repo_info 'shallow repository = true is retrieved correctly' \
	'git clone --depth 1 "file://$PWD/remote"' 'shallow' 'layout.shallow' 'true'

test_repo_info 'object.format = sha1 is retrieved correctly' \
	'git init --object-format=sha1' 'sha1' 'object.format' 'sha1'

test_repo_info 'object.format = sha256 is retrieved correctly' \
	'git init --object-format=sha256' 'sha256' 'object.format' 'sha256'

test_expect_success 'values returned in order requested' '
	cat >expect <<-\EOF &&
	layout.bare=false
	references.format=files
	layout.bare=false
	EOF
	git init --ref-format=files ordered &&
	git -C ordered repo info layout.bare references.format layout.bare >actual &&
	test_cmp expect actual
'

test_expect_success 'git-repo-info fails if an invalid key is requested' '
	echo "error: key ${SQ}foo${SQ} not found" >expect &&
	test_must_fail git repo info foo 2>actual &&
	test_cmp expect actual
'

test_expect_success 'git-repo-info outputs data even if there is an invalid field' '
	echo "references.format=$(test_detect_ref_format)" >expect &&
	test_must_fail git repo info foo references.format bar >actual &&
	test_cmp expect actual
'

test_expect_success 'git-repo-info aborts when requesting an invalid format' '
	echo "fatal: invalid format ${SQ}foo${SQ}" >expect &&
	test_must_fail git repo info --format=foo 2>actual &&
	test_cmp expect actual
'

test_expect_success '-z uses nul-terminated format' '
	printf "layout.bare\nfalse\0layout.shallow\nfalse\0" >expected &&
	git repo info -z layout.bare layout.shallow >actual &&
	test_cmp expected actual
'

test_expect_success 'git repo info uses the last requested format' '
	echo "layout.bare=false" >expected &&
	git repo info --format=nul -z --format=lines layout.bare >actual &&
	test_cmp expected actual
'

test_expect_success 'git repo info --all and git repo info $(git repo info --keys) output the same data' '
	git repo info $(git repo info --keys) >expect &&
	git repo info --all >actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info --all <key> aborts' '
	echo "fatal: --all and <key> cannot be used together" >expect &&
	test_must_fail git repo info --all object.format 2>actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info --keys --format=nul uses nul-terminated output' '
	git repo info --keys --format=lines >lines &&
	lf_to_nul <lines >expect &&
	git repo info --keys --format=nul >actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info --keys aborts when using --format other than lines or nul' '
	echo "fatal: --keys can only be used with --format=lines or --format=nul" >expect &&
	test_must_fail git repo info --keys --format=table 2>actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info --keys aborts when requesting keys' '
	echo "fatal: --keys cannot be used with a <key> or --all" >expect &&
	test_must_fail git repo info --keys --all 2>actual_all &&
	test_must_fail git repo info --keys some.key 2>actual_key &&
	test_cmp expect actual_all &&
	test_cmp expect actual_key
'

test_expect_success 'git repo info --keys uses lines as its default output format' '
	git repo info --keys --format=lines >expect &&
	git repo info --keys >actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info -h shows only repo info usage' '
	git repo info -h >actual &&
	test_grep "git repo info" actual &&
	test_grep ! "git repo structure" actual
'

# Helper function to test path keys in both absolute and relative formats.
# $1: label for the test
# $2: field_name (e.g., commondir)
# $3: expected_dir (the directory name, e.g., .git or custom-common)
# $4: init_command (extra setup like exporting env vars)
test_repo_info_path () {
	label=$1
	field_name=$2
	expected_dir=$3
	init_command=$4

	test_expect_success "absolute: $label" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			mkdir -p repo/sub &&
			cd repo/sub &&
			ROOT="$(test-tool path-utils real_path ..)" && export ROOT &&
			eval "$init_command" &&
			case "$expected_dir" in
			/*) EXPECT_ABS="$expected_dir" ;;
			*) EXPECT_ABS="$ROOT/$expected_dir" ;;
			esac &&
			echo "path.$field_name.absolute=$EXPECT_ABS" >expect &&
			git repo info "path.$field_name.absolute" >actual &&
			test_cmp expect actual
		)
	'

	test_expect_success "relative: $label" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			mkdir -p repo/sub &&
			cd repo/sub &&
			ROOT="$(test-tool path-utils real_path ..)" && export ROOT &&
			eval "$init_command" &&
			case "$expected_dir" in
			/*) EXPECT_REL="$(test-tool path-utils relative_path "$expected_dir" "$PWD")" ;;
			*) EXPECT_REL="../$expected_dir" ;;
			esac &&
			echo "path.$field_name.relative=$EXPECT_REL" >expect &&
			git repo info "path.$field_name.relative" >actual &&
			test_cmp expect actual
		)
	'
}

test_repo_info_path 'commondir standard' 'commondir' '.git'

test_repo_info_path 'commondir with GIT_COMMON_DIR and GIT_DIR' 'commondir' \
	'custom-common' \
	'GIT_COMMON_DIR="$ROOT/custom-common" && export GIT_COMMON_DIR &&
	 GIT_DIR="../.git" && export GIT_DIR &&
	 git init --bare "$ROOT/custom-common"'

test_repo_info_path 'commondir with only GIT_DIR' 'commondir' \
	'.git' \
	'GIT_DIR="../.git" && export GIT_DIR'

test_repo_info_path 'gitdir standard' 'gitdir' '.git'

test_repo_info_path 'gitdir with explicit GIT_DIR' 'gitdir' \
	'.git' \
	'GIT_DIR="../.git" && export GIT_DIR'

test_repo_info_path 'grafts standard' 'grafts' '.git/info/grafts'

test_repo_info_path 'grafts with GIT_GRAFT_FILE override' 'grafts' \
	'custom-graft-file' \
	'GIT_GRAFT_FILE="$ROOT/custom-graft-file" && export GIT_GRAFT_FILE'

test_repo_info_path 'hooks standard' 'hooks' '.git/hooks'

test_repo_info_path 'hooks with core.hooksPath override' 'hooks' \
	'custom-hooks' \
	'git config core.hooksPath "$ROOT/custom-hooks" && mkdir -p "$ROOT/custom-hooks"'

# /dev/null is not a real, canonicalizable filesystem path on Windows,
# so path resolution for core.hooksPath=/dev/null cannot be expected to
# produce a literal "/dev/null" the way it does on POSIX systems.
if ! test_have_prereq MINGW
then
	test_repo_info_path 'hooks with core.hooksPath=/dev/null' 'hooks' \
		'/dev/null' \
		'git config core.hooksPath /dev/null'
fi

test_repo_info_path 'index standard' 'index' '.git/index'

test_repo_info_path 'index with GIT_INDEX_FILE override' 'index' \
	'custom-index-file' \
	'GIT_INDEX_FILE="$ROOT/custom-index-file" && export GIT_INDEX_FILE'

test_expect_success 'path.index in a bare repository returns default index location' '
	test_when_finished "rm -rf bare.git" &&
	git init --bare bare.git &&
	(
		cd bare.git &&
		ROOT="$(test-tool path-utils real_path .)" &&

		echo "path.index.absolute=$ROOT/index" >expect.abs &&
		git repo info path.index.absolute >actual.abs &&
		test_cmp expect.abs actual.abs &&

		echo "path.index.relative=index" >expect.rel &&
		git repo info path.index.relative >actual.rel &&
		test_cmp expect.rel actual.rel
	)
'

test_expect_success 'path.superproject-root absolute and relative' '
	test_when_finished "rm -rf sub super" &&
	git init sub &&
	test_commit -C sub initial &&
	git init super &&
	(
		cd super &&
		git -c protocol.file.allow=always submodule add "../sub" sub &&
		git commit -m "add submodule" &&

		cd sub &&
		ROOT="$(test-tool path-utils real_path ..)" &&

		echo "path.superproject-root.absolute=$ROOT" >expect.abs &&
		git repo info path.superproject-root.absolute >actual.abs &&
		test_cmp expect.abs actual.abs &&

		echo "path.superproject-root.relative=../" >expect.rel &&
		git repo info path.superproject-root.relative >actual.rel &&
		test_cmp expect.rel actual.rel
	)
'

test_expect_success 'path.superproject-root returns empty when not in a submodule' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "path.superproject-root.absolute=" >expect.abs &&
		git repo info path.superproject-root.absolute >actual.abs &&
		test_cmp expect.abs actual.abs &&

		echo "path.superproject-root.relative=" >expect.rel &&
		git repo info path.superproject-root.relative >actual.rel &&
		test_cmp expect.rel actual.rel
	)
'

test_expect_success 'path.toplevel absolute and relative' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		mkdir -p repo/sub &&
		cd repo/sub &&

		ROOT="$(test-tool path-utils real_path ..)" &&

		echo "path.toplevel.absolute=$ROOT" >expect.abs &&
		git repo info path.toplevel.absolute >actual.abs &&
		test_cmp expect.abs actual.abs &&

		echo "path.toplevel.relative=../" >expect.rel &&
		git repo info path.toplevel.relative >actual.rel &&
		test_cmp expect.rel actual.rel
	)
'

test_expect_success 'path.toplevel absolute and relative in a bare repository' '
	test_when_finished "rm -rf bare.git" &&
	git init --bare bare.git &&
	(
		cd bare.git &&

		echo "path.toplevel.absolute=" >expect.abs &&
		git repo info path.toplevel.absolute >actual.abs &&
		test_cmp expect.abs actual.abs &&

		echo "path.toplevel.relative=" >expect.rel &&
		git repo info path.toplevel.relative >actual.rel &&
		test_cmp expect.rel actual.rel
	)
'

test_done
