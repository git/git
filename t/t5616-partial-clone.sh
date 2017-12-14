#!/bin/sh

test_description='git partial clone'

. ./test-lib.sh

# create a normal "src" repo where we can later create new commits.
# expect_1.oids will contain a list of the OIDs of all blobs.
test_expect_success 'setup normal src repo' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	git init src &&
	for n in 1 2 3 4
	do
		echo "This is file: $n" > src/file.$n.txt
		git -C src add file.$n.txt
		git -C src commit -m "file $n"
		git -C src ls-files -s file.$n.txt >>temp
	done &&
	awk -f print_2.awk <temp | sort >expect_1.oids &&
	test_line_count = 4 expect_1.oids
'

# bare clone "src" giving "srv.bare" for use as our server.
test_expect_success 'setup bare clone for server' '
	git clone --bare "file://$(pwd)/src" srv.bare &&
	git -C srv.bare config --local uploadpack.allowfilter 1 &&
	git -C srv.bare config --local uploadpack.allowanysha1inwant 1
'

# do basic partial clone from "srv.bare"
# confirm we are missing all of the known blobs.
# confirm partial clone was registered in the local config.
test_expect_success 'do partial clone 1' '
	git clone --no-checkout --filter=blob:none "file://$(pwd)/srv.bare" pc1 &&
	git -C pc1 rev-list HEAD --quiet --objects --missing=print \
		| awk -f print_1.awk \
		| sed "s/?//" \
		| sort >observed.oids &&
	test_cmp expect_1.oids observed.oids &&
	test "$(git -C pc1 config --local core.repositoryformatversion)" = "1" &&
	test "$(git -C pc1 config --local extensions.partialclone)" = "origin" &&
	test "$(git -C pc1 config --local core.partialclonefilter)" = "blob:none"
'

# checkout master to force dynamic object fetch of blobs at HEAD.
test_expect_success 'verify checkout with dynamic object fetch' '
	git -C pc1 rev-list HEAD --quiet --objects --missing=print >observed &&
	test_line_count = 4 observed &&
	git -C pc1 checkout master &&
	git -C pc1 rev-list HEAD --quiet --objects --missing=print >observed &&
	test_line_count = 0 observed
'

# create new commits in "src" repo to establish a blame history on file.1.txt
# and push to "srv.bare".
test_expect_success 'push new commits to server' '
	git -C src remote add srv "file://$(pwd)/srv.bare" &&
	for x in a b c d e
	do
		echo "Mod file.1.txt $x" >>src/file.1.txt
		git -C src add file.1.txt
		git -C src commit -m "mod $x"
	done &&
	git -C src blame master -- file.1.txt >expect.blame &&
	git -C src push -u srv master
'

# (partial) fetch in the partial clone repo from the promisor remote.
# verify that fetch inherited the filter-spec from the config and DOES NOT
# have the new blobs.
test_expect_success 'partial fetch inherits filter settings' '
	git -C pc1 fetch origin &&
	git -C pc1 rev-list master..origin/master --quiet --objects --missing=print >observed &&
	test_line_count = 5 observed
'

# force dynamic object fetch using diff.
# we should only get 1 new blob (for the file in origin/master).
test_expect_success 'verify diff causes dynamic object fetch' '
	git -C pc1 diff master..origin/master -- file.1.txt &&
	git -C pc1 rev-list master..origin/master --quiet --objects --missing=print >observed &&
	test_line_count = 4 observed
'

# force full dynamic object fetch of the file's history using blame.
# we should get the intermediate blobs for the file.
test_expect_success 'verify blame causes dynamic object fetch' '
	git -C pc1 blame origin/master -- file.1.txt >observed.blame &&
	test_cmp expect.blame observed.blame &&
	git -C pc1 rev-list master..origin/master --quiet --objects --missing=print >observed &&
	test_line_count = 0 observed
'

# create new commits in "src" repo to establish a history on file.2.txt
# and push to "srv.bare".
test_expect_success 'push new commits to server for file.2.txt' '
	for x in a b c d e f
	do
		echo "Mod file.2.txt $x" >>src/file.2.txt
		git -C src add file.2.txt
		git -C src commit -m "mod $x"
	done &&
	git -C src push -u srv master
'

# Do FULL fetch by disabling inherited filter-spec using --no-filter.
# Verify we have all the new blobs.
test_expect_success 'override inherited filter-spec using --no-filter' '
	git -C pc1 fetch --no-filter origin &&
	git -C pc1 rev-list master..origin/master --quiet --objects --missing=print >observed &&
	test_line_count = 0 observed
'

# create new commits in "src" repo to establish a history on file.3.txt
# and push to "srv.bare".
test_expect_success 'push new commits to server for file.3.txt' '
	for x in a b c d e f
	do
		echo "Mod file.3.txt $x" >>src/file.3.txt
		git -C src add file.3.txt
		git -C src commit -m "mod $x"
	done &&
	git -C src push -u srv master
'

# Do a partial fetch and then try to manually fetch the missing objects.
# This can be used as the basis of a pre-command hook to bulk fetch objects
# perhaps combined with a command in dry-run mode.
test_expect_success 'manual prefetch of missing objects' '
	git -C pc1 fetch --filter=blob:none origin &&
	git -C pc1 rev-list master..origin/master --quiet --objects --missing=print \
		| awk -f print_1.awk \
		| sed "s/?//" \
		| sort >observed.oids &&
	test_line_count = 6 observed.oids &&
	git -C pc1 fetch-pack --stdin "file://$(pwd)/srv.bare" <observed.oids &&
	git -C pc1 rev-list master..origin/master --quiet --objects --missing=print \
		| awk -f print_1.awk \
		| sed "s/?//" \
		| sort >observed.oids &&
	test_line_count = 0 observed.oids
'

test_done
