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

test_done
