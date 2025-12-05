# Helper to verify if repo $1 contains a submodule named $2 with gitdir path $3

# This does not check filesystem existence. That is done in submodule.c via the
# submodule_name_to_gitdir() API which this helper ends up calling. The gitdirs
# might or might not exist (e.g. when adding a new submodule), so this only
# checks the expected configuration path, which might be overridden by the user.

verify_submodule_gitdir_path() {
	repo="$1" &&
	name="$2" &&
	path="$3" &&
	(
		cd "$repo" &&
		# Compute expected absolute path
		expected="$(git rev-parse --git-common-dir)/$path" &&
		expected="$(test-tool path-utils real_path "$expected")" &&
		# Compute actual absolute path
		actual="$(git submodule--helper gitdir "$name")" &&
		actual="$(test-tool path-utils real_path "$actual")" &&
		echo "$expected" >expect &&
		echo "$actual" >actual &&
		test_cmp expect actual
	)
}
