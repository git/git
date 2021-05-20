#!/bin/sh

test_description='check handling of disallowed .gitmodule urls'
. ./test-lib.sh

test_expect_success 'create submodule with protected dash in url' '
	git init upstream &&
	git -C upstream commit --allow-empty -m base &&
	mv upstream ./-upstream &&
	git submodule add ./-upstream sub &&
	git add sub .gitmodules &&
	git commit -m submodule
'

test_expect_success 'clone can recurse submodule' '
	test_when_finished "rm -rf dst" &&
	git clone --recurse-submodules . dst &&
	echo base >expect &&
	git -C dst/sub log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'fsck accepts protected dash' '
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	git push dst HEAD
'

test_expect_success 'remove ./ protection from .gitmodules url' '
	perl -i -pe "s{\./}{}" .gitmodules &&
	git commit -am "drop protection"
'

test_expect_success 'clone rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	test_must_fail git clone --recurse-submodules . dst 2>err &&
	test_i18ngrep ignoring err
'

test_expect_success 'fsck rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'trailing backslash is handled correctly' '
	git init testmodule &&
	test_commit -C testmodule c &&
	git submodule add ./testmodule &&
	: ensure that the name ends in a double backslash &&
	sed -e "s|\\(submodule \"testmodule\\)\"|\\1\\\\\\\\\"|" \
		-e "s|url = .*|url = \" --should-not-be-an-option\"|" \
		<.gitmodules >.new &&
	mv .new .gitmodules &&
	git commit -am "Add testmodule" &&
	test_must_fail git clone --verbose --recurse-submodules . dolly 2>err &&
	test_i18ngrep ! "unknown option" err
'

test_expect_success 'fsck rejects missing URL scheme' '
	git checkout --orphan missing-scheme &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = http::one.example.com/foo.git
	EOF
	git add .gitmodules &&
	test_tick &&
	git commit -m "gitmodules with missing URL scheme" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects relative URL resolving to missing scheme' '
	git checkout --orphan relative-missing-scheme &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = "..\\../.\\../:one.example.com/foo.git"
	EOF
	git add .gitmodules &&
	test_tick &&
	git commit -m "gitmodules with relative URL that strips off scheme" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects empty URL scheme' '
	git checkout --orphan empty-scheme &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = http::://one.example.com/foo.git
	EOF
	git add .gitmodules &&
	test_tick &&
	git commit -m "gitmodules with empty URL scheme" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects relative URL resolving to empty scheme' '
	git checkout --orphan relative-empty-scheme &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = ../../../:://one.example.com/foo.git
	EOF
	git add .gitmodules &&
	test_tick &&
	git commit -m "relative gitmodules URL resolving to empty scheme" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects empty hostname' '
	git checkout --orphan empty-host &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = http:///one.example.com/foo.git
	EOF
	git add .gitmodules &&
	test_tick &&
	git commit -m "gitmodules with extra slashes" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects relative url that produced empty hostname' '
	git checkout --orphan messy-relative &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = ../../..//one.example.com/foo.git
	EOF
	git add .gitmodules &&
	test_tick &&
	git commit -m "gitmodules abusing relative_path" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck permits embedded newline with unrecognized scheme' '
	git checkout --orphan newscheme &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = "data://acjbkd%0akajfdickajkd"
	EOF
	git add .gitmodules &&
	git commit -m "gitmodules with unrecognized scheme" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	git push dst HEAD
'

test_expect_success 'fsck rejects embedded newline in url' '
	# create an orphan branch to avoid existing .gitmodules objects
	git checkout --orphan newline &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
	url = "https://one.example.com?%0ahost=two.example.com/foo.git"
	EOF
	git add .gitmodules &&
	git commit -m "gitmodules with newline" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects embedded newline in relative url' '
	git checkout --orphan relative-newline &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
		url = "./%0ahost=two.example.com/foo.git"
	EOF
	git add .gitmodules &&
	git commit -m "relative url with newline" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_expect_success 'fsck rejects embedded newline in git url' '
	git checkout --orphan git-newline &&
	cat >.gitmodules <<-\EOF &&
	[submodule "foo"]
	url = "git://example.com:1234/repo%0a.git"
	EOF
	git add .gitmodules &&
	git commit -m "git url with newline" &&
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_done
