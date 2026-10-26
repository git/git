#!/bin/sh

test_description='submodule urls relative to the server root (^/)'

. ./test-lib.sh

# Keep MSYS2 from turning "^/..." arguments into Windows paths. The value
# must not look like a path itself, or MSYS2 rewrites it for child processes.
MSYS2_ARG_CONV_EXCL='^'
export MSYS2_ARG_CONV_EXCL

test_expect_success 'setup' '
	git config --global protocol.file.allow always &&
	git config --global url."$(pwd)/server/".insteadOf https://example.com/ &&
	git config --global --add url."$(pwd)/server/".insteadOf git@example.com: &&

	git init --bare server/org/dep.git &&
	git init --bare server/org/lib.git &&
	git init --bare server/org/team/super.git &&

	git clone https://example.com/org/dep.git dep &&
	test_commit -C dep dep &&
	git -C dep push origin HEAD &&

	git clone https://example.com/org/lib.git lib &&
	test_commit -C lib lib &&
	git -C lib submodule add ^/org/dep.git dep &&
	git -C lib commit -m "add dep" &&
	git -C lib push origin HEAD &&

	git clone https://example.com/org/team/super.git super &&
	test_commit -C super super &&
	git -C super submodule add ^/org/lib.git lib &&
	git -C super commit -m "add lib" &&
	git -C super push origin HEAD &&

	git clone --bare server/org/team/super.git server/me/super.git
'

test_expect_success 'add records the url as given' '
	test_cmp_config -C super "^/org/lib.git" -f .gitmodules submodule.lib.url &&
	test_cmp_config -C super https://example.com/org/lib.git submodule.lib.url
'

test_expect_success 'clone of the upstream in a subgroup' '
	git clone --recurse-submodules https://example.com/org/team/super.git upstream &&
	test_cmp_config -C upstream https://example.com/org/lib.git submodule.lib.url &&
	test_path_is_file upstream/lib/lib.t &&
	test_path_is_file upstream/lib/dep/dep.t
'

test_expect_success 'clone of a fork at a different depth' '
	git clone --recurse-submodules https://example.com/me/super.git fork &&
	test_cmp_config -C fork https://example.com/org/lib.git submodule.lib.url &&
	test_cmp_config -C fork/lib https://example.com/org/dep.git submodule.dep.url &&
	test_path_is_file fork/lib/dep/dep.t
'

test_expect_success 'clone over scp-like ssh uses ssh for submodules' '
	git clone --recurse-submodules git@example.com:me/super.git fork-ssh &&
	test_cmp_config -C fork-ssh git@example.com:org/lib.git submodule.lib.url &&
	test_cmp_config -C fork-ssh/lib git@example.com:org/dep.git submodule.dep.url &&
	test_path_is_file fork-ssh/lib/dep/dep.t
'

test_expect_success 'sync follows a changed superproject remote' '
	git -C fork remote set-url origin ssh://git@example.com:2222/me/super.git &&
	git -C fork submodule sync &&
	test_cmp_config -C fork ssh://git@example.com:2222/org/lib.git submodule.lib.url &&
	test_cmp_config -C fork/lib ssh://git@example.com:2222/org/lib.git remote.origin.url
'

test_expect_success 'get-default-remote finds the submodule remote by its ^/ url' '
	git -C fork/lib remote rename origin upstream &&
	git -C fork/lib remote add other https://example.com/org/dep.git &&
	echo upstream >expect &&
	git -C fork submodule--helper get-default-remote lib >actual &&
	test_cmp expect actual
'

test_expect_success 'init fails when the remote has no host' '
	git clone server/me/super.git local &&
	test_must_fail git -C local submodule init 2>err &&
	test_grep "cannot resolve .* without a remote url that has a host" err
'

test_expect_success 'init without a remote does not suggest the cwd fallback' '
	git init noremote &&
	git -C noremote config -f .gitmodules submodule.lib.path lib &&
	git -C noremote config -f .gitmodules submodule.lib.url "^/org/lib.git" &&
	git -C noremote update-index --add --cacheinfo \
		160000,$(git -C lib rev-parse HEAD),lib &&
	test_must_fail git -C noremote submodule init 2>err &&
	test_grep "cannot resolve" err &&
	test_grep ! "authoritative upstream" err
'

test_expect_success 'update fails when the remote has no host' '
	git -C noremote config submodule.lib.active true &&
	test_must_fail git -C noremote submodule update 2>err &&
	test_grep "cannot resolve .* without a remote url that has a host" err
'

test_expect_success 'update --init fails even if the submodule is not updated' '
	test_must_fail git -C noremote -c submodule.lib.update=none \
		submodule update --init 2>err &&
	test_grep "cannot resolve" err
'

test_expect_success 'sync fails when the remote has no host' '
	git -C local config submodule.lib.active true &&
	test_must_fail git -C local submodule sync 2>err &&
	test_grep "cannot resolve .* without a remote url that has a host" err
'

test_expect_success 'add fails when the remote has no host' '
	test_must_fail git -C local submodule add "^/org/dep.git" dep 2>err &&
	test_grep "cannot resolve .* without a remote url that has a host" err &&
	test_path_is_missing local/dep
'

test_expect_success 'clone --recurse-submodules stops when the remote has no host' '
	git init --bare server/org/mixed.git &&
	git init mixed &&
	git -C mixed config -f .gitmodules submodule.lib.path lib &&
	git -C mixed config -f .gitmodules submodule.lib.url "^/org/lib.git" &&
	git -C mixed config -f .gitmodules submodule.zz.path zz &&
	git -C mixed config -f .gitmodules submodule.zz.url ../dep.git &&
	git -C mixed update-index --add \
		--cacheinfo 160000,$(git -C lib rev-parse HEAD),lib \
		--cacheinfo 160000,$(git -C dep rev-parse HEAD),zz &&
	git -C mixed add .gitmodules &&
	git -C mixed commit -m mixed &&
	git -C mixed push "$(pwd)/server/org/mixed.git" HEAD &&

	test_must_fail git clone --recurse-submodules server/org/mixed.git \
		mixed-clone 2>err &&
	test_grep "cannot resolve .* without a remote url that has a host" err &&
	test_path_is_missing mixed-clone/zz/dep.t
'

test_expect_success 'nested ^/ fails when the submodule remote has no host' '
	git init --bare server/org/rel.git &&
	git init rel &&
	git -C rel config -f .gitmodules submodule.lib.path lib &&
	git -C rel config -f .gitmodules submodule.lib.url ../lib.git &&
	git -C rel update-index --add --cacheinfo \
		160000,$(git -C lib rev-parse HEAD),lib &&
	git -C rel add .gitmodules &&
	git -C rel commit -m rel &&
	git -C rel push "$(pwd)/server/org/rel.git" HEAD &&

	test_must_fail git clone --recurse-submodules server/org/rel.git \
		rel-clone 2>err &&
	test_grep "cannot resolve .*dep.git. without a remote url that has a host" err
'

test_expect_success 'explicit url overrides a root-relative one' '
	git -C local config submodule.lib.url "$(pwd)/server/org/lib.git" &&
	git -C local submodule update --init &&
	test_path_is_file local/lib/lib.t &&
	git -C local submodule update --remote &&
	test_path_is_file local/lib/lib.t
'

test_expect_success '^/ means the server root even if a local ^ directory exists' '
	git init literal &&
	mkdir -p "literal/^/org" &&
	git clone --bare server/org/lib.git "literal/^/org/lib.git" &&
	git -C literal config -f .gitmodules submodule.lib.path lib &&
	git -C literal config -f .gitmodules submodule.lib.url "^/org/lib.git" &&
	git -C literal update-index --add --cacheinfo \
		160000,$(git -C lib rev-parse HEAD),lib &&

	git -C literal remote add origin https://example.com/me/super.git &&
	git -C literal submodule init &&
	test_cmp_config -C literal https://example.com/org/lib.git submodule.lib.url &&

	git -C literal config --unset submodule.lib.url &&
	git -C literal remote set-url origin "$(pwd)/server/me/super.git" &&
	test_must_fail git -C literal submodule init
'

test_expect_success 'add from a subdirectory is refused' '
	mkdir super/sub &&
	test_must_fail git -C super/sub submodule add ^/org/dep.git dep 2>err &&
	test_grep "Relative path can only be used from the toplevel" err
'

test_done
