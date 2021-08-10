#!/bin/sh
#
# Build and test Git
#

. ${0%/*}/lib.sh

case "$CI_OS_NAME" in
windows*) cmd //c mklink //j t\\.prove "$(cygpath -aw "$cache_dir/.prove")";;
*) ln -s "$cache_dir/.prove" t/.prove;;
esac

if test "$jobname" = "pedantic"
then
	export DEVOPTS=pedantic
fi

make
case "$jobname" in
linux-gcc)
	export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
	make test
	export GIT_TEST_SPLIT_INDEX=yes
	export GIT_TEST_MERGE_ALGORITHM=recursive
	export GIT_TEST_FULL_IN_PACK_ARRAY=true
	export GIT_TEST_OE_SIZE=10
	export GIT_TEST_OE_DELTA_SIZE=5
	export GIT_TEST_COMMIT_GRAPH=1
	export GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS=1
	export GIT_TEST_MULTI_PACK_INDEX=1
	export GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=1
	export GIT_TEST_ADD_I_USE_BUILTIN=1
	export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
	export GIT_TEST_WRITE_REV_INDEX=1
	export GIT_TEST_CHECKOUT_WORKERS=2
	make test && make -C contrib/subtree test || exit 1
	;;
linux-clang)
	export GIT_TEST_DEFAULT_HASH=sha1
	make test
	export GIT_TEST_DEFAULT_HASH=sha256
	make test && make -C contrib/subtree test || exit 1
	;;
linux-gcc-4.8|pedantic)
	# Don't run the tests; we only care about whether Git can be
	# built with GCC 4.8 or with pedantic
	;;
*)
	make test && make -C contrib/subtree test || exit 1
	;;
esac

check_unignored_build_artifacts

save_good_tree
