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

	test_expect_success "$label" '
		eval "$init_command $repo_name" &&
		echo "$key=$expected_value" >expected &&
		git -C $repo_name repo info "$key" >actual &&
		test_cmp expected actual
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
	echo "error: key ${SQ}foo${SQ} not found" >expected_err &&
	test_must_fail git repo info foo 2>actual_err &&
	test_cmp expected_err actual_err
'

test_expect_success 'git-repo-info outputs data even if there is an invalid field' '
	echo "references.format=$(test_detect_ref_format)" >expected &&
	test_must_fail git repo info foo references.format bar >actual &&
	test_cmp expected actual
'

test_done
