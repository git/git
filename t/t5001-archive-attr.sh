#!/bin/sh

test_description='but archive attribute tests'

. ./test-lib.sh

SUBSTFORMAT='%H (%h)%n'

test_expect_exists() {
	test_expect_${2:-success} " $1 exists" "test -e $1"
}

test_expect_missing() {
	test_expect_${2:-success} " $1 does not exist" "test ! -e $1"
}

extract_tar_to_dir () {
	(mkdir "$1" && cd "$1" && "$TAR" xf -) <"$1.tar"
}

test_expect_success 'setup' '
	echo ignored >ignored &&
	echo ignored export-ignore >>.but/info/attributes &&
	but add ignored &&

	echo ignored by tree >ignored-by-tree &&
	echo ignored-by-tree export-ignore >.butattributes &&
	mkdir ignored-by-tree.d &&
	>ignored-by-tree.d/file &&
	echo ignored-by-tree.d export-ignore >>.butattributes &&
	but add ignored-by-tree ignored-by-tree.d .butattributes &&

	echo ignored by worktree >ignored-by-worktree &&
	echo ignored-by-worktree export-ignore >.butattributes &&
	but add ignored-by-worktree &&

	mkdir excluded-by-pathspec.d &&
	>excluded-by-pathspec.d/file &&
	but add excluded-by-pathspec.d &&

	printf "A\$Format:%s\$O" "$SUBSTFORMAT" >nosubstfile &&
	printf "A\$Format:%s\$O" "$SUBSTFORMAT" >substfile1 &&
	printf "A not substituted O" >substfile2 &&
	echo "substfile?" export-subst >>.but/info/attributes &&
	but add nosubstfile substfile1 substfile2 &&

	but cummit -m. &&

	but clone --bare . bare &&
	cp .but/info/attributes bare/info/attributes
'

test_expect_success 'but archive' '
	but archive HEAD >archive.tar &&
	(mkdir archive && cd archive && "$TAR" xf -) <archive.tar
'

test_expect_missing	archive/ignored
test_expect_missing	archive/ignored-by-tree
test_expect_missing	archive/ignored-by-tree.d
test_expect_missing	archive/ignored-by-tree.d/file
test_expect_exists	archive/ignored-by-worktree
test_expect_exists	archive/excluded-by-pathspec.d
test_expect_exists	archive/excluded-by-pathspec.d/file

test_expect_success 'but archive with pathspec' '
	but archive HEAD ":!excluded-by-pathspec.d" >archive-pathspec.tar &&
	extract_tar_to_dir archive-pathspec
'

test_expect_missing	archive-pathspec/ignored
test_expect_missing	archive-pathspec/ignored-by-tree
test_expect_missing	archive-pathspec/ignored-by-tree.d
test_expect_missing	archive-pathspec/ignored-by-tree.d/file
test_expect_exists	archive-pathspec/ignored-by-worktree
test_expect_missing	archive-pathspec/excluded-by-pathspec.d
test_expect_missing	archive-pathspec/excluded-by-pathspec.d/file

test_expect_success 'but archive with wildcard pathspec' '
	but archive HEAD ":!excluded-by-p*" >archive-pathspec-wildcard.tar &&
	extract_tar_to_dir archive-pathspec-wildcard
'

test_expect_missing	archive-pathspec-wildcard/ignored
test_expect_missing	archive-pathspec-wildcard/ignored-by-tree
test_expect_missing	archive-pathspec-wildcard/ignored-by-tree.d
test_expect_missing	archive-pathspec-wildcard/ignored-by-tree.d/file
test_expect_exists	archive-pathspec-wildcard/ignored-by-worktree
test_expect_missing	archive-pathspec-wildcard/excluded-by-pathspec.d
test_expect_missing	archive-pathspec-wildcard/excluded-by-pathspec.d/file

test_expect_success 'but archive with worktree attributes' '
	but archive --worktree-attributes HEAD >worktree.tar &&
	(mkdir worktree && cd worktree && "$TAR" xf -) <worktree.tar
'

test_expect_missing	worktree/ignored
test_expect_exists	worktree/ignored-by-tree
test_expect_missing	worktree/ignored-by-worktree

test_expect_success 'but archive --worktree-attributes option' '
	but archive --worktree-attributes --worktree-attributes HEAD >worktree.tar &&
	(mkdir worktree2 && cd worktree2 && "$TAR" xf -) <worktree.tar
'

test_expect_missing	worktree2/ignored
test_expect_exists	worktree2/ignored-by-tree
test_expect_missing	worktree2/ignored-by-worktree

test_expect_success 'but archive vs. bare' '
	(cd bare && but archive HEAD) >bare-archive.tar &&
	test_cmp_bin archive.tar bare-archive.tar
'

test_expect_success 'but archive with worktree attributes, bare' '
	(cd bare && but archive --worktree-attributes HEAD) >bare-worktree.tar &&
	(mkdir bare-worktree && cd bare-worktree && "$TAR" xf -) <bare-worktree.tar
'

test_expect_missing	bare-worktree/ignored
test_expect_exists	bare-worktree/ignored-by-tree
test_expect_exists	bare-worktree/ignored-by-worktree

test_expect_success 'export-subst' '
	but log "--pretty=format:A${SUBSTFORMAT}O" HEAD >substfile1.expected &&
	test_cmp nosubstfile archive/nosubstfile &&
	test_cmp substfile1.expected archive/substfile1 &&
	test_cmp substfile2 archive/substfile2
'

test_expect_success 'export-subst expands %(describe) once' '
	echo "\$Format:%(describe)\$" >substfile3 &&
	echo "\$Format:%(describe)\$" >>substfile3 &&
	echo "\$Format:%(describe)${LF}%(describe)\$" >substfile4 &&
	but add substfile[34] &&
	but cummit -m export-subst-describe &&
	but tag -m export-subst-describe export-subst-describe &&
	but archive HEAD >archive-describe.tar &&
	extract_tar_to_dir archive-describe &&
	desc=$(but describe) &&
	grep -F "$desc" archive-describe/substfile[34] >substituted &&
	test_line_count = 1 substituted
'

test_done
