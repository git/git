#!/bin/sh

setup_basic_ls_tree_data () {
	mkdir dir &&
	test_commit dir/sub-file &&
	test_commit top-file &&
	git clone . submodule &&
	git submodule add ./submodule &&
	git commit -m"add submodule"
}
