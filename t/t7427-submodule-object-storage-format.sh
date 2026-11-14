#!/bin/sh

test_description='submodules handle the object storage format'

. ./test-lib.sh

test_object_storage_format () {
	echo "$2" >expect &&
	git -C "$1" rev-parse --show-object-storage-format >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	git config set --global protocol.file.allow always
'

test_expect_success 'add existing repository with different object storage format' '
	test_when_finished "rm -rf parent" &&

	git init parent &&
	(
		cd parent &&
		test_commit parent &&
		git init --object-storage-format=files://objs submodule &&
		test_commit -C submodule submodule &&
		git submodule add ./submodule &&
		test_object_storage_format submodule files://objs
	)
'

test_expect_success 'add submodules with explicit files storage format' '
	test_when_finished "rm -rf submodule upstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	test_object_storage_format upstream files &&
	git -C upstream submodule add --object-storage-format=files "file://$(pwd)/submodule" &&
	test_object_storage_format upstream/submodule files
'

test_expect_success 'add submodules with garbage object storage format' '
	test_when_finished "rm -rf submodule upstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	cat >expect <<-EOF &&
	fatal: unknown object storage format ${SQ}garbage${SQ}
	EOF
	test_must_fail git -C upstream submodule add --object-storage-format=garbage "file://$(pwd)/submodule" 2>err &&
	test_cmp expect err
'

test_expect_success 'add submodules with object storage payload is rejected' '
	test_when_finished "rm -rf submodule upstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	cat >expect <<-EOF &&
	fatal: unknown object storage format ${SQ}files://objs${SQ}
	EOF
	test_must_fail git -C upstream submodule add --object-storage-format=files://objs "file://$(pwd)/submodule" 2>err &&
	test_cmp expect err
'

test_expect_success 'recursive clone propagates object storage format' '
	test_when_finished "rm -rf submodule upstream downstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	git -C upstream submodule add "file://$(pwd)/submodule" &&
	git -C upstream commit -am "add submodule" &&

	test_object_storage_format upstream files &&
	test_object_storage_format upstream/submodule files &&

	git clone --object-storage-format=files://objs --recurse-submodules \
		upstream downstream &&
	test_object_storage_format downstream files://objs &&
	test_object_storage_format downstream/submodule files &&
	test_path_is_dir downstream/.git/objs/pack &&
	test_path_is_dir downstream/.git/modules/submodule/objects/pack
'

test_expect_success 'clone submodules with explicit object storage format' '
	test_when_finished "rm -rf submodule upstream downstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	git -C upstream submodule add "file://$(pwd)/submodule" &&
	git -C upstream commit -m "upstream submodule" &&

	git clone --no-recurse-submodules "file://$(pwd)/upstream" downstream &&
	test_object_storage_format downstream files &&
	git -C downstream submodule update --init --object-storage-format=files &&
	test_object_storage_format downstream/submodule files &&
	test_path_is_dir downstream/.git/modules/submodule/objects/pack
'

test_done
