#!/bin/sh
#
# Copyright (c) 2008 David Reiss
#

test_description='Test various path utilities'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

norm_path() {
	expected=$(test-tool path-utils print_path "$2")
	test_expect_success $3 "normalize path: $1 => $2" \
	"test \"\$(test-tool path-utils normalize_path_copy '$1')\" = '$expected'"
}

relative_path() {
	expected=$(test-tool path-utils print_path "$3")
	test_expect_success $4 "relative path: $1 $2 => $3" \
	"test \"\$(test-tool path-utils relative_path '$1' '$2')\" = '$expected'"
}

test_submodule_relative_url() {
	test_expect_success "test_submodule_relative_url: $1 $2 $3 => $4" "
		actual=\$(test-tool submodule resolve-relative-url '$1' '$2' '$3') &&
		test \"\$actual\" = '$4'
	"
}

test_git_path() {
	test_expect_success "git-path $1 $2 => $3" "
		$1 git rev-parse --git-path $2 >actual &&
		echo $3 >expect &&
		test_cmp expect actual
	"
}

# On Windows, we are using MSYS's bash, which mangles the paths.
# Absolute paths are anchored at the MSYS installation directory,
# which means that the path / accounts for this many characters:
rootoff=$(test-tool path-utils normalize_path_copy / | wc -c)
# Account for the trailing LF:
if test $rootoff = 2; then
	rootoff=	# we are on Unix
else
	rootoff=$(($rootoff-1))
	# In MSYS2, the root directory "/" is translated into a Windows
	# directory *with* trailing slash. Let's test for that and adjust
	# our expected longest ancestor length accordingly.
	case "$(test-tool path-utils print_path /)" in
	*/) rootslash=1;;
	*) rootslash=0;;
	esac
fi

ancestor() {
	# We do some math with the expected ancestor length.
	expected=$3
	case "$rootoff,$expected,$2" in
	*,*,//*) ;; # leave UNC paths alone
	[0-9]*,[0-9]*,/*)
		# On Windows, expect MSYS2 pseudo root translation for
		# Unix-style absolute paths
		expected=$(($expected-$rootslash+$rootoff))
		;;
	esac
	test_expect_success $4 "longest ancestor: $1 $2 => $expected" \
	"actual=\$(test-tool path-utils longest_ancestor_length '$1' '$2') &&
	 test \"\$actual\" = '$expected'"
}

# Some absolute path tests should be skipped on Windows due to path mangling
# on POSIX-style absolute paths
case $(uname -s) in
*MINGW*)
	;;
*CYGWIN*)
	;;
*)
	test_set_prereq POSIX
	;;
esac

test_expect_success basename 'test-tool path-utils basename'
test_expect_success dirname 'test-tool path-utils dirname'

norm_path "" ""
norm_path . ""
norm_path ./ ""
norm_path ./. ""
norm_path ./.. ++failed++
norm_path ../. ++failed++
norm_path ./../.// ++failed++
norm_path dir/.. ""
norm_path dir/sub/../.. ""
norm_path dir/sub/../../.. ++failed++
norm_path dir dir
norm_path dir// dir/
norm_path ./dir dir
norm_path dir/. dir/
norm_path dir///./ dir/
norm_path dir//sub/.. dir/
norm_path dir/sub/../ dir/
norm_path dir/sub/../. dir/
norm_path dir/s1/../s2/ dir/s2/
norm_path d1/s1///s2/..//../s3/ d1/s3/
norm_path d1/s1//../s2/../../d2 d2
norm_path d1/.../d2 d1/.../d2
norm_path d1/..././../d2 d1/d2

norm_path / /
norm_path // / POSIX
norm_path /// / POSIX
norm_path /. /
norm_path /./ / POSIX
norm_path /./.. ++failed++ POSIX
norm_path /../. ++failed++
norm_path /./../.// ++failed++ POSIX
norm_path /dir/.. / POSIX
norm_path /dir/sub/../.. / POSIX
norm_path /dir/sub/../../.. ++failed++ POSIX
norm_path /dir /dir
norm_path /dir// /dir/
norm_path /./dir /dir
norm_path /dir/. /dir/
norm_path /dir///./ /dir/
norm_path /dir//sub/.. /dir/
norm_path /dir/sub/../ /dir/
norm_path //dir/sub/../. /dir/ POSIX
norm_path /dir/s1/../s2/ /dir/s2/
norm_path /d1/s1///s2/..//../s3/ /d1/s3/
norm_path /d1/s1//../s2/../../d2 /d2
norm_path /d1/.../d2 /d1/.../d2
norm_path /d1/..././../d2 /d1/d2

ancestor / / -1
ancestor /foo / 0
ancestor /foo /fo -1
ancestor /foo /foo -1
ancestor /foo /bar -1
ancestor /foo /foo/bar -1
ancestor /foo /foo:/bar -1
ancestor /foo /:/foo:/bar 0
ancestor /foo /foo:/:/bar 0
ancestor /foo /:/bar:/foo 0
ancestor /foo/bar / 0
ancestor /foo/bar /fo -1
ancestor /foo/bar /foo 4
ancestor /foo/bar /foo/ba -1
ancestor /foo/bar /:/fo 0
ancestor /foo/bar /foo:/foo/ba 4
ancestor /foo/bar /bar -1
ancestor /foo/bar /fo -1
ancestor /foo/bar /foo:/bar 4
ancestor /foo/bar /:/foo:/bar 4
ancestor /foo/bar /foo:/:/bar 4
ancestor /foo/bar /:/bar:/fo 0
ancestor /foo/bar /:/bar 0
ancestor /foo/bar /foo 4
ancestor /foo/bar /foo:/bar 4
ancestor /foo/bar /bar -1

# Windows-specific: DOS drives, network shares
ancestor C:/Users/me C:/ 2 MINGW
ancestor D:/Users/me C:/ -1 MINGW
ancestor //server/share/my-directory //server/share/ 14 MINGW

test_expect_success 'strip_path_suffix' '
	test c:/msysgit = $(test-tool path-utils strip_path_suffix \
		c:/msysgit/libexec//git-core libexec/git-core)
'

test_expect_success 'absolute path rejects the empty string' '
	test_must_fail test-tool path-utils absolute_path ""
'

test_expect_success MINGW '<drive-letter>:\\abc is an absolute path' '
	for letter in : \" C Z 1 ä
	do
		path=$letter:\\abc &&
		absolute="$(test-tool path-utils absolute_path "$path")" &&
		test "$path" = "$absolute" || return 1
	done
'

test_expect_success 'real path rejects the empty string' '
	test_must_fail test-tool path-utils real_path ""
'

test_expect_success POSIX 'real path works on absolute paths 1' '
	nopath="hopefully-absent-path" &&
	test "/" = "$(test-tool path-utils real_path "/")" &&
	test "/$nopath" = "$(test-tool path-utils real_path "/$nopath")"
'

test_expect_success 'real path works on absolute paths 2' '
	nopath="hopefully-absent-path" &&
	# Find an existing top-level directory for the remaining tests:
	d=$(pwd -P | sed -e "s|^\([^/]*/[^/]*\)/.*|\1|") &&
	test "$d" = "$(test-tool path-utils real_path "$d")" &&
	test "$d/$nopath" = "$(test-tool path-utils real_path "$d/$nopath")"
'

test_expect_success POSIX 'real path removes extra leading slashes' '
	nopath="hopefully-absent-path" &&
	test "/" = "$(test-tool path-utils real_path "///")" &&
	test "/$nopath" = "$(test-tool path-utils real_path "///$nopath")" &&
	# Find an existing top-level directory for the remaining tests:
	d=$(pwd -P | sed -e "s|^\([^/]*/[^/]*\)/.*|\1|") &&
	test "$d" = "$(test-tool path-utils real_path "//$d")" &&
	test "$d/$nopath" = "$(test-tool path-utils real_path "//$d/$nopath")"
'

test_expect_success 'real path removes other extra slashes' '
	nopath="hopefully-absent-path" &&
	# Find an existing top-level directory for the remaining tests:
	d=$(pwd -P | sed -e "s|^\([^/]*/[^/]*\)/.*|\1|") &&
	test "$d" = "$(test-tool path-utils real_path "$d///")" &&
	test "$d/$nopath" = "$(test-tool path-utils real_path "$d///$nopath")"
'

test_expect_success SYMLINKS 'real path works on symlinks' '
	mkdir first &&
	ln -s ../.git first/.git &&
	mkdir second &&
	ln -s ../first second/other &&
	mkdir third &&
	dir="$(cd .git && pwd -P)" &&
	dir2=third/../second/other/.git &&
	test "$dir" = "$(test-tool path-utils real_path $dir2)" &&
	file="$dir"/index &&
	test "$file" = "$(test-tool path-utils real_path $dir2/index)" &&
	basename=blub &&
	test "$dir/$basename" = "$(cd .git && test-tool path-utils real_path "$basename")" &&
	ln -s ../first/file .git/syml &&
	sym="$(cd first && pwd -P)"/file &&
	test "$sym" = "$(test-tool path-utils real_path "$dir2/syml")"
'

test_expect_success SYMLINKS 'prefix_path works with absolute paths to work tree symlinks' '
	ln -s target symlink &&
	test "$(test-tool path-utils prefix_path prefix "$(pwd)/symlink")" = "symlink"
'

test_expect_success 'prefix_path works with only absolute path to work tree' '
	echo "" >expected &&
	test-tool path-utils prefix_path prefix "$(pwd)" >actual &&
	test_cmp expected actual
'

test_expect_success 'prefix_path rejects absolute path to dir with same beginning as work tree' '
	test_must_fail test-tool path-utils prefix_path prefix "$(pwd)a"
'

test_expect_success SYMLINKS 'prefix_path works with absolute path to a symlink to work tree having  same beginning as work tree' '
	git init repo &&
	ln -s repo repolink &&
	test "a" = "$(cd repo && test-tool path-utils prefix_path prefix "$(pwd)/../repolink/a")"
'

relative_path /foo/a/b/c/	/foo/a/b/	c/
relative_path /foo/a/b/c/	/foo/a/b	c/
relative_path /foo/a//b//c/	///foo/a/b//	c/		POSIX
relative_path /foo/a/b		/foo/a/b	./
relative_path /foo/a/b/		/foo/a/b	./
relative_path /foo/a		/foo/a/b	../
relative_path /			/foo/a/b/	../../../
relative_path /foo/a/c		/foo/a/b/	../c
relative_path /foo/a/c		/foo/a/b	../c
relative_path /foo/x/y		/foo/a/b/	../../x/y
relative_path /foo/a/b		"<empty>"	/foo/a/b
relative_path /foo/a/b 		"<null>"	/foo/a/b
relative_path foo/a/b/c/	foo/a/b/	c/
relative_path foo/a/b/c/	foo/a/b		c/
relative_path foo/a/b//c	foo/a//b	c
relative_path foo/a/b/		foo/a/b/	./
relative_path foo/a/b/		foo/a/b		./
relative_path foo/a		foo/a/b		../
relative_path foo/x/y		foo/a/b		../../x/y
relative_path foo/a/c		foo/a/b		../c
relative_path foo/a/b		/foo/x/y	foo/a/b
relative_path /foo/a/b		foo/x/y		/foo/a/b
relative_path d:/a/b		D:/a/c		../b		MINGW
relative_path C:/a/b		D:/a/c		C:/a/b		MINGW
relative_path foo/a/b		"<empty>"	foo/a/b
relative_path foo/a/b 		"<null>"	foo/a/b
relative_path "<empty>"		/foo/a/b	./
relative_path "<empty>"		"<empty>"	./
relative_path "<empty>"		"<null>"	./
relative_path "<null>"		"<empty>"	./
relative_path "<null>"		"<null>"	./
relative_path "<null>"		/foo/a/b	./

test_git_path A=B                info/grafts .git/info/grafts
test_git_path GIT_GRAFT_FILE=foo info/grafts foo
test_git_path GIT_GRAFT_FILE=foo info/////grafts foo
test_git_path GIT_INDEX_FILE=foo index foo
test_git_path GIT_INDEX_FILE=foo index/foo .git/index/foo
test_git_path GIT_INDEX_FILE=foo index2 .git/index2
test_expect_success 'setup fake objects directory foo' 'mkdir foo'
test_git_path GIT_OBJECT_DIRECTORY=foo objects foo
test_git_path GIT_OBJECT_DIRECTORY=foo objects/foo foo/foo
test_git_path GIT_OBJECT_DIRECTORY=foo objects2 .git/objects2
test_expect_success 'setup common repository' 'git --git-dir=bar init'
test_git_path GIT_COMMON_DIR=bar index                    .git/index
test_git_path GIT_COMMON_DIR=bar index.lock               .git/index.lock
test_git_path GIT_COMMON_DIR=bar HEAD                     .git/HEAD
test_git_path GIT_COMMON_DIR=bar logs/HEAD                .git/logs/HEAD
test_git_path GIT_COMMON_DIR=bar logs/HEAD.lock           .git/logs/HEAD.lock
test_git_path GIT_COMMON_DIR=bar logs/refs/bisect/foo     .git/logs/refs/bisect/foo
test_git_path GIT_COMMON_DIR=bar logs/refs                bar/logs/refs
test_git_path GIT_COMMON_DIR=bar logs/refs/               bar/logs/refs/
test_git_path GIT_COMMON_DIR=bar logs/refs/bisec/foo      bar/logs/refs/bisec/foo
test_git_path GIT_COMMON_DIR=bar logs/refs/bisec          bar/logs/refs/bisec
test_git_path GIT_COMMON_DIR=bar logs/refs/bisectfoo      bar/logs/refs/bisectfoo
test_git_path GIT_COMMON_DIR=bar objects                  bar/objects
test_git_path GIT_COMMON_DIR=bar objects/bar              bar/objects/bar
test_git_path GIT_COMMON_DIR=bar info/exclude             bar/info/exclude
test_git_path GIT_COMMON_DIR=bar info/grafts              bar/info/grafts
test_git_path GIT_COMMON_DIR=bar info/sparse-checkout     .git/info/sparse-checkout
test_git_path GIT_COMMON_DIR=bar info//sparse-checkout    .git/info//sparse-checkout
test_git_path GIT_COMMON_DIR=bar remotes/bar              bar/remotes/bar
test_git_path GIT_COMMON_DIR=bar branches/bar             bar/branches/bar
test_git_path GIT_COMMON_DIR=bar logs/refs/heads/main     bar/logs/refs/heads/main
test_git_path GIT_COMMON_DIR=bar refs/heads/main          bar/refs/heads/main
test_git_path GIT_COMMON_DIR=bar refs/bisect/foo          .git/refs/bisect/foo
test_git_path GIT_COMMON_DIR=bar hooks/me                 bar/hooks/me
test_git_path GIT_COMMON_DIR=bar config                   bar/config
test_git_path GIT_COMMON_DIR=bar packed-refs              bar/packed-refs
test_git_path GIT_COMMON_DIR=bar shallow                  bar/shallow
test_git_path GIT_COMMON_DIR=bar common                   bar/common
test_git_path GIT_COMMON_DIR=bar common/file              bar/common/file

# In the tests below, $(pwd) must be used because it is a native path on
# Windows and avoids MSYS's path mangling (which simplifies "foo/../bar" and
# strips the dot from trailing "/.").

test_submodule_relative_url "../" "../foo" "../submodule" "../../submodule"
test_submodule_relative_url "../" "../foo/bar" "../submodule" "../../foo/submodule"
test_submodule_relative_url "../" "../foo/submodule" "../submodule" "../../foo/submodule"
test_submodule_relative_url "../" "./foo" "../submodule" "../submodule"
test_submodule_relative_url "../" "./foo/bar" "../submodule" "../foo/submodule"
test_submodule_relative_url "../../../" "../foo/bar" "../sub/a/b/c" "../../../../foo/sub/a/b/c"
test_submodule_relative_url "../" "$(pwd)/addtest" "../repo" "$(pwd)/repo"
test_submodule_relative_url "../" "foo/bar" "../submodule" "../foo/submodule"
test_submodule_relative_url "../" "foo" "../submodule" "../submodule"

test_submodule_relative_url "(null)" "../foo/bar" "../sub/a/b/c" "../foo/sub/a/b/c"
test_submodule_relative_url "(null)" "../foo/bar" "../sub/a/b/c/" "../foo/sub/a/b/c"
test_submodule_relative_url "(null)" "../foo/bar/" "../sub/a/b/c" "../foo/sub/a/b/c"
test_submodule_relative_url "(null)" "../foo/bar" "../submodule" "../foo/submodule"
test_submodule_relative_url "(null)" "../foo/submodule" "../submodule" "../foo/submodule"
test_submodule_relative_url "(null)" "../foo" "../submodule" "../submodule"
test_submodule_relative_url "(null)" "./foo/bar" "../submodule" "foo/submodule"
test_submodule_relative_url "(null)" "./foo" "../submodule" "submodule"
test_submodule_relative_url "(null)" "//somewhere else/repo" "../subrepo" "//somewhere else/subrepo"
test_submodule_relative_url "(null)" "//somewhere else/repo" "../../subrepo" "//subrepo"
test_submodule_relative_url "(null)" "//somewhere else/repo" "../../../subrepo" "/subrepo"
test_submodule_relative_url "(null)" "//somewhere else/repo" "../../../../subrepo" "subrepo"
test_submodule_relative_url "(null)" "$(pwd)/subsuper_update_r" "../subsubsuper_update_r" "$(pwd)/subsubsuper_update_r"
test_submodule_relative_url "(null)" "$(pwd)/super_update_r2" "../subsuper_update_r" "$(pwd)/subsuper_update_r"
test_submodule_relative_url "(null)" "$(pwd)/." "../." "$(pwd)/."
test_submodule_relative_url "(null)" "$(pwd)" "./." "$(pwd)/."
test_submodule_relative_url "(null)" "$(pwd)/addtest" "../repo" "$(pwd)/repo"
test_submodule_relative_url "(null)" "$(pwd)" "./å äö" "$(pwd)/å äö"
test_submodule_relative_url "(null)" "$(pwd)/." "../submodule" "$(pwd)/submodule"
test_submodule_relative_url "(null)" "$(pwd)/submodule" "../submodule" "$(pwd)/submodule"
test_submodule_relative_url "(null)" "$(pwd)/home2/../remote" "../bundle1" "$(pwd)/home2/../bundle1"
test_submodule_relative_url "(null)" "$(pwd)/submodule_update_repo" "./." "$(pwd)/submodule_update_repo/."
test_submodule_relative_url "(null)" "file:///tmp/repo" "../subrepo" "file:///tmp/subrepo"
test_submodule_relative_url "(null)" "foo/bar" "../submodule" "foo/submodule"
test_submodule_relative_url "(null)" "foo" "../submodule" "submodule"
test_submodule_relative_url "(null)" "helper:://hostname/repo" "../subrepo" "helper:://hostname/subrepo"
test_submodule_relative_url "(null)" "helper:://hostname/repo" "../../subrepo" "helper:://subrepo"
test_submodule_relative_url "(null)" "helper:://hostname/repo" "../../../subrepo" "helper::/subrepo"
test_submodule_relative_url "(null)" "helper:://hostname/repo" "../../../../subrepo" "helper::subrepo"
test_submodule_relative_url "(null)" "helper:://hostname/repo" "../../../../../subrepo" "helper:subrepo"
test_submodule_relative_url "(null)" "helper:://hostname/repo" "../../../../../../subrepo" ".:subrepo"
test_submodule_relative_url "(null)" "ssh://hostname/repo" "../subrepo" "ssh://hostname/subrepo"
test_submodule_relative_url "(null)" "ssh://hostname/repo" "../../subrepo" "ssh://subrepo"
test_submodule_relative_url "(null)" "ssh://hostname/repo" "../../../subrepo" "ssh:/subrepo"
test_submodule_relative_url "(null)" "ssh://hostname/repo" "../../../../subrepo" "ssh:subrepo"
test_submodule_relative_url "(null)" "ssh://hostname/repo" "../../../../../subrepo" ".:subrepo"
test_submodule_relative_url "(null)" "ssh://hostname:22/repo" "../subrepo" "ssh://hostname:22/subrepo"
test_submodule_relative_url "(null)" "user@host:path/to/repo" "../subrepo" "user@host:path/to/subrepo"
test_submodule_relative_url "(null)" "user@host:repo" "../subrepo" "user@host:subrepo"
test_submodule_relative_url "(null)" "user@host:repo" "../../subrepo" ".:subrepo"

test_expect_success 'match .gitmodules' '
	test-tool path-utils is_dotgitmodules \
		.gitmodules \
		\
		.git${u200c}modules \
		\
		.Gitmodules \
		.gitmoduleS \
		\
		".gitmodules " \
		".gitmodules." \
		".gitmodules  " \
		".gitmodules. " \
		".gitmodules ." \
		".gitmodules.." \
		".gitmodules   " \
		".gitmodules.  " \
		".gitmodules . " \
		".gitmodules  ." \
		\
		".Gitmodules " \
		".Gitmodules." \
		".Gitmodules  " \
		".Gitmodules. " \
		".Gitmodules ." \
		".Gitmodules.." \
		".Gitmodules   " \
		".Gitmodules.  " \
		".Gitmodules . " \
		".Gitmodules  ." \
		\
		GITMOD~1 \
		gitmod~1 \
		GITMOD~2 \
		gitmod~3 \
		GITMOD~4 \
		\
		"GITMOD~1 " \
		"gitmod~2." \
		"GITMOD~3  " \
		"gitmod~4. " \
		"GITMOD~1 ." \
		"gitmod~2   " \
		"GITMOD~3.  " \
		"gitmod~4 . " \
		\
		GI7EBA~1 \
		gi7eba~9 \
		\
		GI7EB~10 \
		GI7EB~11 \
		GI7EB~99 \
		GI7EB~10 \
		GI7E~100 \
		GI7E~101 \
		GI7E~999 \
		~1000000 \
		~9999999 \
		\
		.gitmodules:\$DATA \
		"gitmod~4 . :\$DATA" \
		\
		--not \
		".gitmodules x"  \
		".gitmodules .x" \
		\
		" .gitmodules" \
		\
		..gitmodules \
		\
		gitmodules \
		\
		.gitmodule \
		\
		".gitmodules x " \
		".gitmodules .x" \
		\
		GI7EBA~ \
		GI7EBA~0 \
		GI7EBA~~1 \
		GI7EBA~X \
		Gx7EBA~1 \
		GI7EBX~1 \
		\
		GI7EB~1 \
		GI7EB~01 \
		GI7EB~1X \
		\
		.gitmodules,:\$DATA
'

test_expect_success 'match .gitattributes' '
	test-tool path-utils is_dotgitattributes \
		.gitattributes \
		.git${u200c}attributes \
		.Gitattributes \
		.gitattributeS \
		GITATT~1 \
		GI7D29~1
'

test_expect_success 'match .gitignore' '
	test-tool path-utils is_dotgitignore \
		.gitignore \
		.git${u200c}ignore \
		.Gitignore \
		.gitignorE \
		GITIGN~1 \
		GI250A~1
'

test_expect_success 'match .mailmap' '
	test-tool path-utils is_dotmailmap \
		.mailmap \
		.mail${u200c}map \
		.Mailmap \
		.mailmaP \
		MAILMA~1 \
		MABA30~1
'

test_expect_success MINGW 'is_valid_path() on Windows' '
	test-tool path-utils is_valid_path \
		win32 \
		"win32 x" \
		../hello.txt \
		C:\\git \
		comm \
		conout.c \
		com0.c \
		lptN \
		\
		--not \
		"win32 "  \
		"win32 /x "  \
		"win32."  \
		"win32 . ." \
		.../hello.txt \
		colon:test \
		"AUX.c" \
		"abc/conOut\$  .xyz/test" \
		lpt8 \
		com9.c \
		"lpt*" \
		Nul \
		"PRN./abc"
'

test_lazy_prereq RUNTIME_PREFIX '
	test true = "$RUNTIME_PREFIX"
'

test_lazy_prereq CAN_EXEC_IN_PWD '
	cp "$GIT_EXEC_PATH"/git$X ./ &&
	./git rev-parse
'

test_expect_success !VALGRIND,RUNTIME_PREFIX,CAN_EXEC_IN_PWD 'RUNTIME_PREFIX works' '
	mkdir -p pretend/bin pretend/libexec/git-core &&
	echo "echo HERE" | write_script pretend/libexec/git-core/git-here &&
	cp "$GIT_EXEC_PATH"/git$X pretend/bin/ &&
	GIT_EXEC_PATH= ./pretend/bin/git here >actual &&
	echo HERE >expect &&
	test_cmp expect actual'

test_expect_success !VALGRIND,RUNTIME_PREFIX,CAN_EXEC_IN_PWD '%(prefix)/ works' '
	mkdir -p pretend/bin &&
	cp "$GIT_EXEC_PATH"/git$X pretend/bin/ &&
	git config yes.path "%(prefix)/yes" &&
	GIT_EXEC_PATH= ./pretend/bin/git config --path yes.path >actual &&
	echo "$(pwd)/pretend/yes" >expect &&
	test_cmp expect actual
'

test_done
