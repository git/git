#!/bin/sh

test_description='test use of bundle URI in "git fetch"'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up repos and bundles' '
	git init source &&
	test_commit -C source A &&
	git clone --no-local source go-A-to-C &&
	test_commit -C source B &&
	git clone --no-local source go-B-to-C &&
	git clone --no-local source go-B-to-D &&
	git -C source bundle create B.bundle main &&
	test_commit -C source C &&
	git -C source bundle create B-to-C.bundle B..main &&
	git -C source config uploadpack.advertiseBundleURIs true &&
	git -C source config bundle.version 1 &&
	git -C source config bundle.mode all &&
	git -C source config bundle.heuristic creationToken &&
	git -C source config bundle.bundle-B.uri "file://$(pwd)/source/B.bundle" &&
	git -C source config bundle.bundle-B.creationToken 1 &&
	git -C source config bundle.bundle-B-to-C.uri "file://$(pwd)/source/B-to-C.bundle" &&
	git -C source config bundle.bundle-B-to-C.creationToken 2
'

test_expect_success 'fetches one bundle URI to get up-to-date' '
	git -C go-B-to-C -c transfer.bundleURI=true fetch origin &&
	test 1 = $(ls go-B-to-C/.git/objects/bundles | wc -l) &&
	test 2 = $(git -C go-B-to-C config fetch.bundleCreationToken)
'

test_expect_success 'fetches two bundle URIs to get up-to-date' '
	git -C go-A-to-C -c transfer.bundleURI=true fetch origin &&
	test 2 = $(ls go-A-to-C/.git/objects/bundles | wc -l) &&
	test 2 = $(git -C go-A-to-C config fetch.bundleCreationToken)
'

test_expect_success 'fetches one bundle URI and objects from remote' '
	test_commit -C source D &&
	git -C go-B-to-D -c transfer.bundleURI=true fetch origin &&
	test 1 = $(ls go-B-to-D/.git/objects/bundles | wc -l) &&
	test 2 = $(git -C go-B-to-D config fetch.bundleCreationToken)
'

test_done
