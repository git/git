#!/bin/sh

test_description='test git repo-info'

. ./test-lib.sh

# git-repo-info keys. It must contain the same keys listed in the const
# repo_info_fields, in lexicographical order.
REPO_INFO_KEYS='
	layout.bare
	layout.shallow
	object.format
	path.common-dir
	path.config-file
	path.git-dir
	path.git-prefix
	path.grafts-file
	path.hooks-directory
	path.index-file
	path.logs-directory
	path.objects-directory
	path.packed-refs-file
	path.refs-directory
	path.shallow-file
	path.superproject-working-tree
	path.toplevel
	references.format
'

REPO_INFO_PATH_KEYS='
	path.common-dir
	path.config-file
	path.git-dir
	path.git-prefix
	path.grafts-file
	path.hooks-directory
	path.index-file
	path.logs-directory
	path.objects-directory
	path.packed-refs-file
	path.refs-directory
	path.shallow-file
	path.superproject-working-tree
	path.toplevel
'

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

	test_expect_success "keyvalue: $label" '
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

test_expect_success 'category key returns all matching keys' '
	cat >expect <<-\EOF &&
	layout.bare=false
	layout.shallow=false
	EOF
	git init category-layout &&
	git -C category-layout repo info layout >actual &&
	test_cmp expect actual
'

test_expect_success 'mixed key/category requests preserve request order' '
	cat >expect <<-EOF &&
	object.format=$(test_oid algo)
	layout.bare=false
	layout.shallow=false
	EOF
	git init mixed-order &&
	git -C mixed-order repo info object.format layout >actual &&
	test_cmp expect actual
'

test_expect_success 'path.git-dir matches rev-parse --absolute-git-dir' '
	git init path-git-dir &&
	expected_value=$(git -C path-git-dir rev-parse --absolute-git-dir) &&
	echo "path.git-dir=$expected_value" >expect &&
	git -C path-git-dir repo info path.git-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'path.common-dir matches rev-parse --git-common-dir' '
	git init path-common-dir &&
	expected_value=$(git -C path-common-dir rev-parse --path-format=absolute --git-common-dir) &&
	echo "path.common-dir=$expected_value" >expect &&
	git -C path-common-dir repo info path.common-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'path.toplevel matches rev-parse --show-toplevel' '
	git init path-toplevel &&
	expected_value=$(git -C path-toplevel rev-parse --show-toplevel) &&
	echo "path.toplevel=$expected_value" >expect &&
	git -C path-toplevel repo info path.toplevel >actual &&
	test_cmp expect actual
'

test_expect_success 'path.toplevel is empty in bare repository' '
	git init --bare bare-path-toplevel &&
	echo "path.toplevel=" >expect &&
	git -C bare-path-toplevel repo info path.toplevel >actual &&
	test_cmp expect actual
'

test_expect_success 'path.git-prefix matches rev-parse --show-prefix' '
	git init path-prefix &&
	mkdir -p path-prefix/a/b &&
	expected_value=$(git -C path-prefix/a/b rev-parse --show-prefix) &&
	echo "path.git-prefix=$expected_value" >expect &&
	git -C path-prefix/a/b repo info path.git-prefix >actual &&
	test_cmp expect actual
'

test_expect_success 'git-path style keys match rev-parse --git-path' '
	git init path-git-path &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path info/grafts) &&
	echo "path.grafts-file=$expected_value" >expect &&
	git -C path-git-path repo info path.grafts-file >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path index) &&
	echo "path.index-file=$expected_value" >expect &&
	git -C path-git-path repo info path.index-file >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path objects) &&
	echo "path.objects-directory=$expected_value" >expect &&
	git -C path-git-path repo info path.objects-directory >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path hooks) &&
	echo "path.hooks-directory=$expected_value" >expect &&
	git -C path-git-path repo info path.hooks-directory >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path config) &&
	echo "path.config-file=$expected_value" >expect &&
	git -C path-git-path repo info path.config-file >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path logs) &&
	echo "path.logs-directory=$expected_value" >expect &&
	git -C path-git-path repo info path.logs-directory >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path packed-refs) &&
	echo "path.packed-refs-file=$expected_value" >expect &&
	git -C path-git-path repo info path.packed-refs-file >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path refs) &&
	echo "path.refs-directory=$expected_value" >expect &&
	git -C path-git-path repo info path.refs-directory >actual &&
	test_cmp expect actual &&

	expected_value=$(git -C path-git-path rev-parse --path-format=absolute --git-path shallow) &&
	echo "path.shallow-file=$expected_value" >expect &&
	git -C path-git-path repo info path.shallow-file >actual &&
	test_cmp expect actual
'

test_expect_success 'path.superproject-working-tree is empty when not a submodule' '
	git init path-superproject &&
	echo "path.superproject-working-tree=" >expect &&
	git -C path-superproject repo info path.superproject-working-tree >actual &&
	test_cmp expect actual
'

test_expect_success 'path.superproject-working-tree matches rev-parse in submodule' '
	git init path-superproject-origin &&
	echo x >path-superproject-origin/x &&
	git -C path-superproject-origin add x &&
	git -C path-superproject-origin commit -m x &&

	git init path-superproject-parent &&
	git -C path-superproject-parent -c protocol.file.allow=always submodule add ../path-superproject-origin sm &&

	expected_value=$(git -C path-superproject-parent/sm rev-parse --show-superproject-working-tree) &&
	echo "path.superproject-working-tree=$expected_value" >expect &&
	git -C path-superproject-parent/sm repo info path.superproject-working-tree >actual &&
	test_cmp expect actual
'

test_expect_success 'path category returns all path keys' '
	git init path-category &&
	>expect &&
	for key in $REPO_INFO_PATH_KEYS
	do
		git -C path-category repo info "$key" >>expect || return 1
	done &&
	git -C path-category repo info path >actual &&
	test_cmp expect actual
'

test_expect_success 'path-format=relative matches rev-parse for git-dir' '
	git init path-format-relative &&
	expected_value=$(git -C path-format-relative rev-parse --path-format=relative --git-dir) &&
	echo "path.git-dir=$expected_value" >expect &&
	git -C path-format-relative repo info --path-format=relative path.git-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info uses the last requested path format' '
	git init path-format-last &&
	expected_value=$(git -C path-format-last rev-parse --path-format=relative --git-dir) &&
	echo "path.git-dir=$expected_value" >expect &&
	git -C path-format-last repo info --path-format=absolute --path-format=relative path.git-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'git-repo-info aborts when requesting an invalid path format' '
	echo "fatal: invalid path format ${SQ}foo${SQ}" >expect &&
	test_must_fail git repo info --path-format=foo path.git-dir 2>actual &&
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
	git repo info --format=nul -z --format=keyvalue layout.bare >actual &&
	test_cmp expected actual
'

test_expect_success 'git repo info --all returns all key-value pairs' '
	git repo info $REPO_INFO_KEYS >expect &&
	git repo info --all >actual &&
	test_cmp expect actual
'

test_expect_success 'git repo info --all <key> aborts' '
	echo "fatal: --all and <key> cannot be used together" >expect &&
	test_must_fail git repo info --all object.format 2>actual &&
	test_cmp expect actual
'

test_done
