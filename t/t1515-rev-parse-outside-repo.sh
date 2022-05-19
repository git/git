#!/bin/sh

test_description='check that certain rev-parse options work outside repo'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'set up non-repo directory' '
	GIT_CEILING_DIRECTORIES=$(pwd) &&
	export GIT_CEILING_DIRECTORIES &&
	mkdir non-repo &&
	cd non-repo &&
	# confirm that but does not find a repo
	test_must_fail but rev-parse --but-dir
'

# Rather than directly test the output of sq-quote directly,
# make sure the shell can read back a tricky case, since
# that's what we really care about anyway.
tricky="really tricky with \\ and \" and '"
dump_args () {
	for i in "$@"; do
		echo "arg: $i"
	done
}
test_expect_success 'rev-parse --sq-quote' '
	dump_args "$tricky" easy >expect &&
	eval "dump_args $(but rev-parse --sq-quote "$tricky" easy)" >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --local-env-vars' '
	but rev-parse --local-env-vars >actual &&
	# we do not want to depend on the complete list here,
	# so just look for something plausible
	grep ^GIT_DIR actual
'

test_expect_success 'rev-parse --resolve-but-dir' '
	but init --separate-but-dir repo dir &&
	test_must_fail but rev-parse --resolve-but-dir . &&
	echo "$(pwd)/repo" >expect &&
	but rev-parse --resolve-but-dir dir/.but >actual &&
	test_cmp expect actual
'

test_done
