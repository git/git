#!/bin/sh

test_description=clone

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

X=
test_have_prereq !MINGW || X=.exe

test_expect_success setup '

	rm -fr .but &&
	test_create_repo src &&
	(
		cd src &&
		>file &&
		but add file &&
		but cummit -m initial &&
		echo 1 >file &&
		but add file &&
		but cummit -m updated
	)

'

test_expect_success 'clone with excess parameters (1)' '

	rm -fr dst &&
	test_must_fail but clone -n src dst junk

'

test_expect_success 'clone with excess parameters (2)' '

	rm -fr dst &&
	test_must_fail but clone -n "file://$(pwd)/src" dst junk

'

test_expect_success 'output from clone' '
	rm -fr dst &&
	but clone -n "file://$(pwd)/src" dst >output 2>&1 &&
	test $(grep Clon output | wc -l) = 1
'

test_expect_success 'clone does not keep pack' '

	rm -fr dst &&
	but clone -n "file://$(pwd)/src" dst &&
	! test -f dst/file &&
	! (echo dst/.but/objects/pack/pack-* | grep "\.keep")

'

test_expect_success 'clone checks out files' '

	rm -fr dst &&
	but clone src dst &&
	test -f dst/file

'

test_expect_success 'clone respects GIT_WORK_TREE' '

	GIT_WORK_TREE=worktree but clone src bare &&
	test -f bare/config &&
	test -f worktree/file

'

test_expect_success 'clone from hooks' '

	test_create_repo r0 &&
	cd r0 &&
	test_cummit initial &&
	cd .. &&
	but init r1 &&
	cd r1 &&
	test_hook pre-cummit <<-\EOF &&
	but clone ../r0 ../r2
	exit 1
	EOF
	: >file &&
	but add file &&
	test_must_fail but cummit -m invoke-hook &&
	cd .. &&
	test_cmp r0/.but/HEAD r2/.but/HEAD &&
	test_cmp r0/initial.t r2/initial.t

'

test_expect_success 'clone creates intermediate directories' '

	but clone src long/path/to/dst &&
	test -f long/path/to/dst/file

'

test_expect_success 'clone creates intermediate directories for bare repo' '

	but clone --bare src long/path/to/bare/dst &&
	test -f long/path/to/bare/dst/config

'

test_expect_success 'clone --mirror' '

	but clone --mirror src mirror &&
	test -f mirror/HEAD &&
	test ! -f mirror/file &&
	FETCH="$(cd mirror && but config remote.origin.fetch)" &&
	test "+refs/*:refs/*" = "$FETCH" &&
	MIRROR="$(cd mirror && but config --bool remote.origin.mirror)" &&
	test "$MIRROR" = true

'

test_expect_success 'clone --mirror with detached HEAD' '

	( cd src && but checkout HEAD^ && but rev-parse HEAD >../expected ) &&
	but clone --mirror src mirror.detached &&
	( cd src && but checkout - ) &&
	GIT_DIR=mirror.detached but rev-parse HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'clone --bare with detached HEAD' '

	( cd src && but checkout HEAD^ && but rev-parse HEAD >../expected ) &&
	but clone --bare src bare.detached &&
	( cd src && but checkout - ) &&
	GIT_DIR=bare.detached but rev-parse HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'clone --bare names the local repository <name>.but' '

	but clone --bare src &&
	test -d src.but

'

test_expect_success 'clone --mirror does not repeat tags' '

	(cd src &&
	 but tag some-tag HEAD) &&
	but clone --mirror src mirror2 &&
	(cd mirror2 &&
	 but show-ref 2> clone.err > clone.out) &&
	! grep Duplicate mirror2/clone.err &&
	grep some-tag mirror2/clone.out

'

test_expect_success 'clone to destination with trailing /' '

	but clone src target-1/ &&
	T=$( cd target-1 && but rev-parse HEAD ) &&
	S=$( cd src && but rev-parse HEAD ) &&
	test "$T" = "$S"

'

test_expect_success 'clone to destination with extra trailing /' '

	but clone src target-2/// &&
	T=$( cd target-2 && but rev-parse HEAD ) &&
	S=$( cd src && but rev-parse HEAD ) &&
	test "$T" = "$S"

'

test_expect_success 'clone to an existing empty directory' '
	mkdir target-3 &&
	but clone src target-3 &&
	T=$( cd target-3 && but rev-parse HEAD ) &&
	S=$( cd src && but rev-parse HEAD ) &&
	test "$T" = "$S"
'

test_expect_success 'clone to an existing non-empty directory' '
	mkdir target-4 &&
	>target-4/Fakefile &&
	test_must_fail but clone src target-4
'

test_expect_success 'clone to an existing path' '
	>target-5 &&
	test_must_fail but clone src target-5
'

test_expect_success 'clone a void' '
	mkdir src-0 &&
	(
		cd src-0 && but init
	) &&
	but clone "file://$(pwd)/src-0" target-6 2>err-6 &&
	! grep "fatal:" err-6 &&
	(
		cd src-0 && test_cummit A
	) &&
	but clone "file://$(pwd)/src-0" target-7 2>err-7 &&
	! grep "fatal:" err-7 &&
	# There is no reason to insist they are bit-for-bit
	# identical, but this test should suffice for now.
	test_cmp target-6/.but/config target-7/.but/config
'

test_expect_success 'clone respects global branch.autosetuprebase' '
	(
		test_config="$HOME/.butconfig" &&
		but config -f "$test_config" branch.autosetuprebase remote &&
		rm -fr dst &&
		but clone src dst &&
		cd dst &&
		actual="z$(but config branch.main.rebase)" &&
		test ztrue = $actual
	)
'

test_expect_success 'respect url-encoding of file://' '
	but init x+y &&
	but clone "file://$PWD/x+y" xy-url-1 &&
	but clone "file://$PWD/x%2By" xy-url-2
'

test_expect_success 'do not query-string-decode + in URLs' '
	rm -rf x+y &&
	but init "x y" &&
	test_must_fail but clone "file://$PWD/x+y" xy-no-plus
'

test_expect_success 'do not respect url-encoding of non-url path' '
	but init x+y &&
	test_must_fail but clone x%2By xy-regular &&
	but clone x+y xy-regular
'

test_expect_success 'clone separate butdir' '
	rm -rf dst &&
	but clone --separate-but-dir realbutdir src dst &&
	test -d realbutdir/refs
'

test_expect_success 'clone separate butdir: output' '
	echo "butdir: $(pwd)/realbutdir" >expected &&
	test_cmp expected dst/.but
'

test_expect_success 'clone from .but file' '
	but clone dst/.but dst2
'

test_expect_success 'fetch from .but butfile' '
	(
		cd dst2 &&
		but fetch ../dst/.but
	)
'

test_expect_success 'fetch from butfile parent' '
	(
		cd dst2 &&
		but fetch ../dst
	)
'

test_expect_success 'clone separate butdir where target already exists' '
	rm -rf dst &&
	echo foo=bar >>realbutdir/config &&
	test_must_fail but clone --separate-but-dir realbutdir src dst &&
	grep foo=bar realbutdir/config
'

test_expect_success 'clone --reference from original' '
	but clone --shared --bare src src-1 &&
	but clone --bare src src-2 &&
	but clone --reference=src-2 --bare src-1 target-8 &&
	grep /src-2/ target-8/objects/info/alternates
'

test_expect_success 'clone with more than one --reference' '
	but clone --bare src src-3 &&
	but clone --bare src src-4 &&
	but clone --reference=src-3 --reference=src-4 src target-9 &&
	grep /src-3/ target-9/.but/objects/info/alternates &&
	grep /src-4/ target-9/.but/objects/info/alternates
'

test_expect_success 'clone from original with relative alternate' '
	mkdir nest &&
	but clone --bare src nest/src-5 &&
	echo ../../../src/.but/objects >nest/src-5/objects/info/alternates &&
	but clone --bare nest/src-5 target-10 &&
	grep /src/\\.but/objects target-10/objects/info/alternates
'

test_expect_success 'clone checking out a tag' '
	but clone --branch=some-tag src dst.tag &&
	GIT_DIR=src/.but but rev-parse some-tag >expected &&
	GIT_DIR=dst.tag/.but but rev-parse HEAD >actual &&
	test_cmp expected actual &&
	GIT_DIR=dst.tag/.but but config remote.origin.fetch >fetch.actual &&
	echo "+refs/heads/*:refs/remotes/origin/*" >fetch.expected &&
	test_cmp fetch.expected fetch.actual
'

test_expect_success 'set up ssh wrapper' '
	cp "$GIT_BUILD_DIR/t/helper/test-fake-ssh$X" \
		"$TRASH_DIRECTORY/ssh$X" &&
	GIT_SSH="$TRASH_DIRECTORY/ssh$X" &&
	export GIT_SSH &&
	export TRASH_DIRECTORY &&
	>"$TRASH_DIRECTORY"/ssh-output
'

copy_ssh_wrapper_as () {
	rm -f "${1%$X}$X" &&
	cp "$TRASH_DIRECTORY/ssh$X" "${1%$X}$X" &&
	test_when_finished "rm $(but rev-parse --sq-quote "${1%$X}$X")" &&
	GIT_SSH="${1%$X}$X" &&
	test_when_finished "GIT_SSH=\"\$TRASH_DIRECTORY/ssh\$X\""
}

expect_ssh () {
	test_when_finished '
		(cd "$TRASH_DIRECTORY" && rm -f ssh-expect && >ssh-output)
	' &&
	{
		case "$#" in
		1)
			;;
		2)
			echo "ssh: $1 but-upload-pack '$2'"
			;;
		3)
			echo "ssh: $1 $2 but-upload-pack '$3'"
			;;
		*)
			echo "ssh: $1 $2 but-upload-pack '$3' $4"
		esac
	} >"$TRASH_DIRECTORY/ssh-expect" &&
	(cd "$TRASH_DIRECTORY" && test_cmp ssh-expect ssh-output)
}

test_expect_success 'clone myhost:src uses ssh' '
	GIT_TEST_PROTOCOL_VERSION=0 but clone myhost:src ssh-clone &&
	expect_ssh myhost src
'

test_expect_success !MINGW,!CYGWIN 'clone local path foo:bar' '
	cp -R src "foo:bar" &&
	but clone "foo:bar" foobar &&
	expect_ssh none
'

test_expect_success 'bracketed hostnames are still ssh' '
	GIT_TEST_PROTOCOL_VERSION=0 but clone "[myhost:123]:src" ssh-bracket-clone &&
	expect_ssh "-p 123" myhost src
'

test_expect_success 'OpenSSH variant passes -4' '
	GIT_TEST_PROTOCOL_VERSION=0 but clone -4 "[myhost:123]:src" ssh-ipv4-clone &&
	expect_ssh "-4 -p 123" myhost src
'

test_expect_success 'variant can be overridden' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/putty" &&
	but -c ssh.variant=putty clone -4 "[myhost:123]:src" ssh-putty-clone &&
	expect_ssh "-4 -P 123" myhost src
'

test_expect_success 'variant=auto picks based on basename' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink" &&
	but -c ssh.variant=auto clone -4 "[myhost:123]:src" ssh-auto-clone &&
	expect_ssh "-4 -P 123" myhost src
'

test_expect_success 'simple does not support -4/-6' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/simple" &&
	test_must_fail but clone -4 "myhost:src" ssh-4-clone-simple
'

test_expect_success 'simple does not support port' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/simple" &&
	test_must_fail but clone "[myhost:123]:src" ssh-bracket-clone-simple
'

test_expect_success 'uplink is treated as simple' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/uplink" &&
	test_must_fail but clone "[myhost:123]:src" ssh-bracket-clone-uplink &&
	but clone "myhost:src" ssh-clone-uplink &&
	expect_ssh myhost src
'

test_expect_success 'OpenSSH-like uplink is treated as ssh' '
	write_script "$TRASH_DIRECTORY/uplink" <<-EOF &&
	if test "\$1" = "-G"
	then
		exit 0
	fi &&
	exec "\$TRASH_DIRECTORY/ssh$X" "\$@"
	EOF
	test_when_finished "rm -f \"\$TRASH_DIRECTORY/uplink\"" &&
	GIT_SSH="$TRASH_DIRECTORY/uplink" &&
	test_when_finished "GIT_SSH=\"\$TRASH_DIRECTORY/ssh\$X\"" &&
	GIT_TEST_PROTOCOL_VERSION=0 but clone "[myhost:123]:src" ssh-bracket-clone-sshlike-uplink &&
	expect_ssh "-p 123" myhost src
'

test_expect_success 'plink is treated specially (as putty)' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink" &&
	but clone "[myhost:123]:src" ssh-bracket-clone-plink-0 &&
	expect_ssh "-P 123" myhost src
'

test_expect_success 'plink.exe is treated specially (as putty)' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink.exe" &&
	but clone "[myhost:123]:src" ssh-bracket-clone-plink-1 &&
	expect_ssh "-P 123" myhost src
'

test_expect_success 'tortoiseplink is like putty, with extra arguments' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/tortoiseplink" &&
	but clone "[myhost:123]:src" ssh-bracket-clone-plink-2 &&
	expect_ssh "-batch -P 123" myhost src
'

test_expect_success 'double quoted plink.exe in GIT_SSH_COMMAND' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink.exe" &&
	GIT_SSH_COMMAND="\"$TRASH_DIRECTORY/plink.exe\" -v" \
		but clone "[myhost:123]:src" ssh-bracket-clone-plink-3 &&
	expect_ssh "-v -P 123" myhost src
'

test_expect_success 'single quoted plink.exe in GIT_SSH_COMMAND' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink.exe" &&
	GIT_SSH_COMMAND="$SQ$TRASH_DIRECTORY/plink.exe$SQ -v" \
		but clone "[myhost:123]:src" ssh-bracket-clone-plink-4 &&
	expect_ssh "-v -P 123" myhost src
'

test_expect_success 'GIT_SSH_VARIANT overrides plink detection' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink" &&
	GIT_TEST_PROTOCOL_VERSION=0 GIT_SSH_VARIANT=ssh \
		but clone "[myhost:123]:src" ssh-bracket-clone-variant-1 &&
	expect_ssh "-p 123" myhost src
'

test_expect_success 'ssh.variant overrides plink detection' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink" &&
	GIT_TEST_PROTOCOL_VERSION=0 but -c ssh.variant=ssh \
		clone "[myhost:123]:src" ssh-bracket-clone-variant-2 &&
	expect_ssh "-p 123" myhost src
'

test_expect_success 'GIT_SSH_VARIANT overrides plink detection to plink' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink" &&
	GIT_SSH_VARIANT=plink \
	but clone "[myhost:123]:src" ssh-bracket-clone-variant-3 &&
	expect_ssh "-P 123" myhost src
'

test_expect_success 'GIT_SSH_VARIANT overrides plink to tortoiseplink' '
	copy_ssh_wrapper_as "$TRASH_DIRECTORY/plink" &&
	GIT_SSH_VARIANT=tortoiseplink \
	but clone "[myhost:123]:src" ssh-bracket-clone-variant-4 &&
	expect_ssh "-batch -P 123" myhost src
'

test_expect_success 'clean failure on broken quoting' '
	test_must_fail \
		env GIT_SSH_COMMAND="${SQ}plink.exe -v" \
		but clone "[myhost:123]:src" sq-failure
'

counter=0
# $1 url
# $2 none|host
# $3 path
test_clone_url () {
	counter=$(($counter + 1))
	test_might_fail env GIT_TEST_PROTOCOL_VERSION=0 but clone "$1" tmp$counter &&
	shift &&
	expect_ssh "$@"
}

test_expect_success !MINGW,!CYGWIN 'clone c:temp is ssl' '
	test_clone_url c:temp c temp
'

test_expect_success MINGW 'clone c:temp is dos drive' '
	test_clone_url c:temp none
'

#ip v4
for repo in rep rep/home/project 123
do
	test_expect_success "clone host:$repo" '
		test_clone_url host:$repo host $repo
	'
done

#ipv6
for repo in rep rep/home/project 123
do
	test_expect_success "clone [::1]:$repo" '
		test_clone_url [::1]:$repo ::1 "$repo"
	'
done
#home directory
test_expect_success "clone host:/~repo" '
	test_clone_url host:/~repo host "~repo"
'

test_expect_success "clone [::1]:/~repo" '
	test_clone_url [::1]:/~repo ::1 "~repo"
'

# Corner cases
for url in foo/bar:baz [foo]bar/baz:qux [foo/bar]:baz
do
	test_expect_success "clone $url is not ssh" '
		test_clone_url $url none
	'
done

#with ssh:// scheme
#ignore trailing colon
for tcol in "" :
do
	test_expect_success "clone ssh://host.xz$tcol/home/user/repo" '
		test_clone_url "ssh://host.xz$tcol/home/user/repo" host.xz /home/user/repo
	'
	# from home directory
	test_expect_success "clone ssh://host.xz$tcol/~repo" '
	test_clone_url "ssh://host.xz$tcol/~repo" host.xz "~repo"
'
done

# with port number
test_expect_success 'clone ssh://host.xz:22/home/user/repo' '
	test_clone_url "ssh://host.xz:22/home/user/repo" "-p 22 host.xz" "/home/user/repo"
'

# from home directory with port number
test_expect_success 'clone ssh://host.xz:22/~repo' '
	test_clone_url "ssh://host.xz:22/~repo" "-p 22 host.xz" "~repo"
'

#IPv6
for tuah in ::1 [::1] [::1]: user@::1 user@[::1] user@[::1]: [user@::1] [user@::1]:
do
	ehost=$(echo $tuah | sed -e "s/1]:/1]/" | tr -d "[]")
	test_expect_success "clone ssh://$tuah/home/user/repo" "
	  test_clone_url ssh://$tuah/home/user/repo $ehost /home/user/repo
	"
done

#IPv6 from home directory
for tuah in ::1 [::1] user@::1 user@[::1] [user@::1]
do
	euah=$(echo $tuah | tr -d "[]")
	test_expect_success "clone ssh://$tuah/~repo" "
	  test_clone_url ssh://$tuah/~repo $euah '~repo'
	"
done

#IPv6 with port number
for tuah in [::1] user@[::1] [user@::1]
do
	euah=$(echo $tuah | tr -d "[]")
	test_expect_success "clone ssh://$tuah:22/home/user/repo" "
	  test_clone_url ssh://$tuah:22/home/user/repo '-p 22' $euah /home/user/repo
	"
done

#IPv6 from home directory with port number
for tuah in [::1] user@[::1] [user@::1]
do
	euah=$(echo $tuah | tr -d "[]")
	test_expect_success "clone ssh://$tuah:22/~repo" "
	  test_clone_url ssh://$tuah:22/~repo '-p 22' $euah '~repo'
	"
done

test_expect_success 'clone from a repository with two identical branches' '

	(
		cd src &&
		but checkout -b another main
	) &&
	but clone src target-11 &&
	test "z$( cd target-11 && but symbolic-ref HEAD )" = zrefs/heads/another

'

test_expect_success 'shallow clone locally' '
	but clone --depth=1 --no-local src ssrrcc &&
	but clone ssrrcc ddsstt &&
	test_cmp ssrrcc/.but/shallow ddsstt/.but/shallow &&
	( cd ddsstt && but fsck )
'

test_expect_success 'GIT_TRACE_PACKFILE produces a usable pack' '
	rm -rf dst.but &&
	GIT_TRACE_PACKFILE=$PWD/tmp.pack but clone --no-local --bare src dst.but &&
	but init --bare replay.but &&
	but -C replay.but index-pack -v --stdin <tmp.pack
'

test_expect_success 'clone on case-insensitive fs' '
	but init icasefs &&
	(
		cd icasefs &&
		o=$(but hash-object -w --stdin </dev/null | hex2oct) &&
		t=$(printf "100644 X\0${o}100644 x\0${o}" |
			but hash-object -w -t tree --stdin) &&
		c=$(but cummit-tree -m bogus $t) &&
		but update-ref refs/heads/bogus $c &&
		but clone -b bogus . bogus 2>warning
	)
'

test_expect_success CASE_INSENSITIVE_FS 'colliding file detection' '
	grep X icasefs/warning &&
	grep x icasefs/warning &&
	test_i18ngrep "the following paths have collided" icasefs/warning
'

test_expect_success 'clone with GIT_DEFAULT_HASH' '
	(
		sane_unset GIT_DEFAULT_HASH &&
		but init --object-format=sha1 test-sha1 &&
		but init --object-format=sha256 test-sha256
	) &&
	test_cummit -C test-sha1 foo &&
	test_cummit -C test-sha256 foo &&
	GIT_DEFAULT_HASH=sha1 but clone test-sha256 test-clone-sha256 &&
	GIT_DEFAULT_HASH=sha256 but clone test-sha1 test-clone-sha1 &&
	but -C test-clone-sha1 status &&
	but -C test-clone-sha256 status
'

partial_clone_server () {
	       SERVER="$1" &&

	rm -rf "$SERVER" client &&
	test_create_repo "$SERVER" &&
	test_cummit -C "$SERVER" one &&
	HASH1=$(but -C "$SERVER" hash-object one.t) &&
	but -C "$SERVER" revert HEAD &&
	test_cummit -C "$SERVER" two &&
	HASH2=$(but -C "$SERVER" hash-object two.t) &&
	test_config -C "$SERVER" uploadpack.allowfilter 1 &&
	test_config -C "$SERVER" uploadpack.allowanysha1inwant 1
}

partial_clone () {
	       SERVER="$1" &&
	       URL="$2" &&

	partial_clone_server "${SERVER}" &&
	but clone --filter=blob:limit=0 "$URL" client &&

	but -C client fsck &&

	# Ensure that unneeded blobs are not inadvertently fetched.
	test_config -C client remote.origin.promisor "false" &&
	but -C client config --unset remote.origin.partialclonefilter &&
	test_must_fail but -C client cat-file -e "$HASH1" &&

	# But this blob was fetched, because clone performs an initial checkout
	but -C client cat-file -e "$HASH2"
}

test_expect_success 'partial clone' '
	partial_clone server "file://$(pwd)/server"
'

test_expect_success 'partial clone with -o' '
	partial_clone_server server &&
	but clone -o blah --filter=blob:limit=0 "file://$(pwd)/server" client &&
	test_cmp_config -C client "blob:limit=0" --get-all remote.blah.partialclonefilter
'

test_expect_success 'partial clone: warn if server does not support object filtering' '
	rm -rf server client &&
	test_create_repo server &&
	test_cummit -C server one &&

	but clone --filter=blob:limit=0 "file://$(pwd)/server" client 2> err &&

	test_i18ngrep "filtering not recognized by server" err
'

test_expect_success 'batch missing blob request during checkout' '
	rm -rf server client &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	but -C server add a b &&

	but -C server cummit -m x &&
	echo aa >server/a &&
	echo bb >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&

	but clone --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is only one negotiation by checking that there is
	# only "done" line sent. ("done" marks the end of negotiation.)
	GIT_TRACE_PACKET="$(pwd)/trace" but -C client checkout HEAD^ &&
	grep "fetch> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_expect_success 'batch missing blob request does not inadvertently try to fetch butlinks' '
	rm -rf server client &&

	test_create_repo repo_for_submodule &&
	test_cummit -C repo_for_submodule x &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&

	echo aa >server/a &&
	echo bb >server/b &&
	# Also add a butlink pointing to an arbitrary repository
	but -C server submodule add "$(pwd)/repo_for_submodule" c &&
	but -C server add a b c &&
	but -C server cummit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&

	# Make sure that it succeeds
	but clone --filter=blob:limit=0 "file://$(pwd)/server" client
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'partial clone using HTTP' '
	partial_clone "$HTTPD_DOCUMENT_ROOT_PATH/server" "$HTTPD_URL/smart/server"
'

test_expect_success 'reject cloning shallow repository using HTTP' '
	test_when_finished "rm -rf repo" &&
	but clone --bare --no-local --depth=1 src "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	test_must_fail but -c protocol.version=2 clone --reject-shallow $HTTPD_URL/smart/repo.but repo 2>err &&
	test_i18ngrep -e "source repository is shallow, reject to clone." err &&

	but clone --no-reject-shallow $HTTPD_URL/smart/repo.but repo
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
