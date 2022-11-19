#!/bin/sh

test_description="Tests performance of ref operations"

. ./perf-lib.sh

test_perf_large_repo

test_perf 'git pack-refs (v1)' '
	git commit --allow-empty -m "change one ref" &&
	git pack-refs --all
'

test_perf 'git for-each-ref (v1)' '
	git for-each-ref --format="%(refname)" >/dev/null
'

test_perf 'git for-each-ref prefix (v1)' '
	git for-each-ref --format="%(refname)" refs/tags/ >/dev/null
'

test_expect_success 'configure packed-refs v2' '
	git config core.repositoryFormatVersion 1 &&
	git config --add extensions.refFormat files &&
	git config --add extensions.refFormat packed &&
	git config --add extensions.refFormat packed-v2 &&
	git config refs.packedRefsVersion 2 &&
	git commit --allow-empty -m "change one ref" &&
	git pack-refs --all &&
	test_copy_bytes 16 .git/packed-refs | xxd >actual &&
	grep PREF actual
'

test_perf 'git pack-refs (v2)' '
	git commit --allow-empty -m "change one ref" &&
	git pack-refs --all
'

test_perf 'git pack-refs (v2;hashing)' '
	git commit --allow-empty -m "change one ref" &&
	git -c refs.hashPackedRefs=true pack-refs --all
'

test_perf 'git for-each-ref (v2)' '
	git for-each-ref --format="%(refname)" >/dev/null
'

test_perf 'git for-each-ref prefix (v2)' '
	git for-each-ref --format="%(refname)" refs/tags/ >/dev/null
'

test_done
