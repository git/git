#!/bin/sh

test_description='check handling of disallowed .butmodule urls'
. ./test-lib.sh

test_expect_success 'create submodule with protected dash in url' '
	but init upstream &&
	but -C upstream cummit --allow-empty -m base &&
	mv upstream ./-upstream &&
	but submodule add ./-upstream sub &&
	but add sub .butmodules &&
	but cummit -m submodule
'

test_expect_success 'clone can recurse submodule' '
	test_when_finished "rm -rf dst" &&
	but clone --recurse-submodules . dst &&
	echo base >expect &&
	but -C dst/sub log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'fsck accepts protected dash' '
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	but push dst HEAD
'

test_expect_success 'remove ./ protection from .butmodules url' '
	perl -i -pe "s{\./}{}" .butmodules &&
	but cummit -am "drop protection"
'

test_expect_success 'clone rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	test_must_fail but clone --recurse-submodules . dst 2>err &&
	test_i18ngrep ignoring err
'

test_expect_success 'fsck rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'trailing backslash is handled correctly' '
	but init testmodule &&
	test_cummit -C testmodule c &&
	but submodule add ./testmodule &&
	: ensure that the name ends in a double backslash &&
	sed -e "s|\\(submodule \"testmodule\\)\"|\\1\\\\\\\\\"|" \
		-e "s|url = .*|url = \" --should-not-be-an-option\"|" \
		<.butmodules >.new &&
	mv .new .butmodules &&
	but cummit -am "Add testmodule" &&
	test_must_fail but clone --verbose --recurse-submodules . dolly 2>err &&
	test_i18ngrep ! "unknown option" err
'

test_expect_success 'fsck rejects missing URL scheme' '
	but checkout --orphan missing-scheme &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = http::one.example.com/foo.but
	EOF
	but add .butmodules &&
	test_tick &&
	but cummit -m "butmodules with missing URL scheme" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects relative URL resolving to missing scheme' '
	but checkout --orphan relative-missing-scheme &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = "..\\../.\\../:one.example.com/foo.but"
	EOF
	but add .butmodules &&
	test_tick &&
	but cummit -m "butmodules with relative URL that strips off scheme" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects empty URL scheme' '
	but checkout --orphan empty-scheme &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = http::://one.example.com/foo.but
	EOF
	but add .butmodules &&
	test_tick &&
	but cummit -m "butmodules with empty URL scheme" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects relative URL resolving to empty scheme' '
	but checkout --orphan relative-empty-scheme &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = ../../../:://one.example.com/foo.but
	EOF
	but add .butmodules &&
	test_tick &&
	but cummit -m "relative butmodules URL resolving to empty scheme" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects empty hostname' '
	but checkout --orphan empty-host &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = http:///one.example.com/foo.but
	EOF
	but add .butmodules &&
	test_tick &&
	but cummit -m "butmodules with extra slashes" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects relative url that produced empty hostname' '
	but checkout --orphan messy-relative &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = ../../..//one.example.com/foo.but
	EOF
	but add .butmodules &&
	test_tick &&
	but cummit -m "butmodules abusing relative_path" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck permits embedded newline with unrecognized scheme' '
	but checkout --orphan newscheme &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = "data://acjbkd%0akajfdickajkd"
	EOF
	but add .butmodules &&
	but cummit -m "butmodules with unrecognized scheme" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	but push dst HEAD
'

test_expect_success 'fsck rejects embedded newline in url' '
	# create an orphan branch to avoid existing .butmodules objects
	but checkout --orphan newline &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
	url = "https://one.example.com?%0ahost=two.example.com/foo.but"
	EOF
	but add .butmodules &&
	but cummit -m "butmodules with newline" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects embedded newline in relative url' '
	but checkout --orphan relative-newline &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
		url = "./%0ahost=two.example.com/foo.but"
	EOF
	but add .butmodules &&
	but cummit -m "relative url with newline" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_expect_success 'fsck rejects embedded newline in but url' '
	but checkout --orphan but-newline &&
	cat >.butmodules <<-\EOF &&
	[submodule "foo"]
	url = "but://example.com:1234/repo%0a.but"
	EOF
	but add .butmodules &&
	but cummit -m "but url with newline" &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesUrl err
'

test_done
