#!/bin/sh

test_description='Test git update-ref error handling'
. ./test-lib.sh

# Create some references, perhaps run pack-refs --all, then try to
# create some more references. Ensure that the second creation fails
# with the correct error message.
# Usage: test_update_rejected <before> <pack> <create> <error>
#   <before> is a ws-separated list of refs to create before the test
#   <pack> (true or false) tells whether to pack the refs before the test
#   <create> is a list of variables to attempt creating
#   <error> is a string to look for in the stderr of update-ref.
# All references are created in the namespace specified by the current
# value of $prefix.
test_update_rejected () {
	before="$1" &&
	pack="$2" &&
	create="$3" &&
	error="$4" &&
	printf "create $prefix/%s $C\n" $before |
	git update-ref --stdin &&
	git for-each-ref $prefix >unchanged &&
	if $pack
	then
		git pack-refs --all
	fi &&
	printf "create $prefix/%s $C\n" $create >input &&
	test_must_fail git update-ref --stdin <input 2>output.err &&
	grep -F "$error" output.err &&
	git for-each-ref $prefix >actual &&
	test_cmp unchanged actual
}

Q="'"

# Test adding and deleting D/F-conflicting references in a single
# transaction.
df_test() {
	prefix="$1"
	pack=: symadd=false symdel=false add_del=false addref= delref=
	shift
	while test $# -gt 0
	do
		case "$1" in
		--pack)
			pack="git pack-refs --all"
			shift
			;;
		--sym-add)
			# Perform the add via a symbolic reference
			symadd=true
			shift
			;;
		--sym-del)
			# Perform the del via a symbolic reference
			symdel=true
			shift
			;;
		--del-add)
			# Delete first reference then add second
			add_del=false
			delref="$prefix/r/$2"
			addref="$prefix/r/$3"
			shift 3
			;;
		--add-del)
			# Add first reference then delete second
			add_del=true
			addref="$prefix/r/$2"
			delref="$prefix/r/$3"
			shift 3
			;;
		*)
			echo 1>&2 "Extra args to df_test: $*"
			return 1
			;;
		esac
	done
	git update-ref "$delref" $C &&
	if $symadd
	then
		addname="$prefix/s/symadd" &&
		git symbolic-ref "$addname" "$addref"
	else
		addname="$addref"
	fi &&
	if $symdel
	then
		delname="$prefix/s/symdel" &&
		git symbolic-ref "$delname" "$delref"
	else
		delname="$delref"
	fi &&
	cat >expected-err <<-EOF &&
	fatal: cannot lock ref $Q$addname$Q: $Q$delref$Q exists; cannot create $Q$addref$Q
	EOF
	$pack &&
	if $add_del
	then
		printf "%s\n" "create $addname $D" "delete $delname"
	else
		printf "%s\n" "delete $delname" "create $addname $D"
	fi >commands &&
	test_must_fail git update-ref --stdin <commands 2>output.err &&
	test_cmp expected-err output.err &&
	printf "%s\n" "$C $delref" >expected-refs &&
	git for-each-ref --format="%(objectname) %(refname)" $prefix/r >actual-refs &&
	test_cmp expected-refs actual-refs
}

test_expect_success 'setup' '

	git commit --allow-empty -m Initial &&
	C=$(git rev-parse HEAD) &&
	git commit --allow-empty -m Second &&
	D=$(git rev-parse HEAD) &&
	git commit --allow-empty -m Third &&
	E=$(git rev-parse HEAD)
'

test_expect_success 'existing loose ref is a simple prefix of new' '

	prefix=refs/1l &&
	test_update_rejected "a c e" false "b c/x d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x$Q"

'

test_expect_success 'existing packed ref is a simple prefix of new' '

	prefix=refs/1p &&
	test_update_rejected "a c e" true "b c/x d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x$Q"

'

test_expect_success 'existing loose ref is a deeper prefix of new' '

	prefix=refs/2l &&
	test_update_rejected "a c e" false "b c/x/y d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x/y$Q"

'

test_expect_success 'existing packed ref is a deeper prefix of new' '

	prefix=refs/2p &&
	test_update_rejected "a c e" true "b c/x/y d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x/y$Q"

'

test_expect_success 'new ref is a simple prefix of existing loose' '

	prefix=refs/3l &&
	test_update_rejected "a c/x e" false "b c d" \
		"$Q$prefix/c/x$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'new ref is a simple prefix of existing packed' '

	prefix=refs/3p &&
	test_update_rejected "a c/x e" true "b c d" \
		"$Q$prefix/c/x$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'new ref is a deeper prefix of existing loose' '

	prefix=refs/4l &&
	test_update_rejected "a c/x/y e" false "b c d" \
		"$Q$prefix/c/x/y$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'new ref is a deeper prefix of existing packed' '

	prefix=refs/4p &&
	test_update_rejected "a c/x/y e" true "b c d" \
		"$Q$prefix/c/x/y$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'one new ref is a simple prefix of another' '

	prefix=refs/5 &&
	test_update_rejected "a e" false "b c c/x d" \
		"cannot process $Q$prefix/c$Q and $Q$prefix/c/x$Q at the same time"

'

test_expect_success 'empty directory should not fool rev-parse' '
	prefix=refs/e-rev-parse &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	echo "$C" >expected &&
	git rev-parse $prefix/foo >actual &&
	test_cmp expected actual
'

test_expect_success 'empty directory should not fool for-each-ref' '
	prefix=refs/e-for-each-ref &&
	git update-ref $prefix/foo $C &&
	git for-each-ref $prefix >expected &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	git for-each-ref $prefix >actual &&
	test_cmp expected actual
'

test_expect_success 'empty directory should not fool create' '
	prefix=refs/e-create &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "create %s $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool verify' '
	prefix=refs/e-verify &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "verify %s $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 1-arg update' '
	prefix=refs/e-update-1 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "update %s $D\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 2-arg update' '
	prefix=refs/e-update-2 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "update %s $D $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 0-arg delete' '
	prefix=refs/e-delete-0 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "delete %s\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'empty directory should not fool 1-arg delete' '
	prefix=refs/e-delete-1 &&
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	mkdir -p .git/$prefix/foo/bar/baz &&
	printf "delete %s $C\n" $prefix/foo |
	git update-ref --stdin
'

test_expect_success 'D/F conflict prevents add long + delete short' '
	df_test refs/df-al-ds --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents add short + delete long' '
	df_test refs/df-as-dl --add-del foo foo/bar
'

test_expect_success 'D/F conflict prevents delete long + add short' '
	df_test refs/df-dl-as --del-add foo/bar foo
'

test_expect_success 'D/F conflict prevents delete short + add long' '
	df_test refs/df-ds-al --del-add foo foo/bar
'

test_expect_success 'D/F conflict prevents add long + delete short packed' '
	df_test refs/df-al-dsp --pack --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents add short + delete long packed' '
	df_test refs/df-as-dlp --pack --add-del foo foo/bar
'

test_expect_success 'D/F conflict prevents delete long packed + add short' '
	df_test refs/df-dlp-as --pack --del-add foo/bar foo
'

test_expect_success 'D/F conflict prevents delete short packed + add long' '
	df_test refs/df-dsp-al --pack --del-add foo foo/bar
'

# Try some combinations involving symbolic refs...

test_expect_success 'D/F conflict prevents indirect add long + delete short' '
	df_test refs/df-ial-ds --sym-add --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents indirect add long + indirect delete short' '
	df_test refs/df-ial-ids --sym-add --sym-del --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents indirect add short + indirect delete long' '
	df_test refs/df-ias-idl --sym-add --sym-del --add-del foo foo/bar
'

test_expect_success 'D/F conflict prevents indirect delete long + indirect add short' '
	df_test refs/df-idl-ias --sym-add --sym-del --del-add foo/bar foo
'

test_expect_success 'D/F conflict prevents indirect add long + delete short packed' '
	df_test refs/df-ial-dsp --sym-add --pack --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents indirect add long + indirect delete short packed' '
	df_test refs/df-ial-idsp --sym-add --sym-del --pack --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents add long + indirect delete short packed' '
	df_test refs/df-al-idsp --sym-del --pack --add-del foo/bar foo
'

test_expect_success 'D/F conflict prevents indirect delete long packed + indirect add short' '
	df_test refs/df-idlp-ias --sym-add --sym-del --pack --del-add foo/bar foo
'

# Test various errors when reading the old values of references...

test_expect_success 'missing old value blocks update' '
	prefix=refs/missing-update &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: unable to resolve reference $Q$prefix/foo$Q
	EOF
	printf "%s\n" "update $prefix/foo $E $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'incorrect old value blocks update' '
	prefix=refs/incorrect-update &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: is at $C but expected $D
	EOF
	printf "%s\n" "update $prefix/foo $E $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'existing old value blocks create' '
	prefix=refs/existing-create &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: reference already exists
	EOF
	printf "%s\n" "create $prefix/foo $E" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'incorrect old value blocks delete' '
	prefix=refs/incorrect-delete &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: is at $C but expected $D
	EOF
	printf "%s\n" "delete $prefix/foo $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'missing old value blocks indirect update' '
	prefix=refs/missing-indirect-update &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: unable to resolve reference $Q$prefix/foo$Q
	EOF
	printf "%s\n" "update $prefix/symref $E $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'incorrect old value blocks indirect update' '
	prefix=refs/incorrect-indirect-update &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: is at $C but expected $D
	EOF
	printf "%s\n" "update $prefix/symref $E $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'existing old value blocks indirect create' '
	prefix=refs/existing-indirect-create &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: reference already exists
	EOF
	printf "%s\n" "create $prefix/symref $E" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'incorrect old value blocks indirect delete' '
	prefix=refs/incorrect-indirect-delete &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: is at $C but expected $D
	EOF
	printf "%s\n" "delete $prefix/symref $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'missing old value blocks indirect no-deref update' '
	prefix=refs/missing-noderef-update &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: reference is missing but expected $D
	EOF
	printf "%s\n" "option no-deref" "update $prefix/symref $E $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'incorrect old value blocks indirect no-deref update' '
	prefix=refs/incorrect-noderef-update &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: is at $C but expected $D
	EOF
	printf "%s\n" "option no-deref" "update $prefix/symref $E $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'existing old value blocks indirect no-deref create' '
	prefix=refs/existing-noderef-create &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: reference already exists
	EOF
	printf "%s\n" "option no-deref" "create $prefix/symref $E" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'incorrect old value blocks indirect no-deref delete' '
	prefix=refs/incorrect-noderef-delete &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	git update-ref $prefix/foo $C &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: is at $C but expected $D
	EOF
	printf "%s\n" "option no-deref" "delete $prefix/symref $D" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'non-empty directory blocks create' '
	prefix=refs/ne-create &&
	mkdir -p .git/$prefix/foo/bar &&
	: >.git/$prefix/foo/bar/baz.lock &&
	test_when_finished "rm -f .git/$prefix/foo/bar/baz.lock" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: there is a non-empty directory $Q.git/$prefix/foo$Q blocking reference $Q$prefix/foo$Q
	EOF
	printf "%s\n" "update $prefix/foo $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: unable to resolve reference $Q$prefix/foo$Q
	EOF
	printf "%s\n" "update $prefix/foo $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'broken reference blocks create' '
	prefix=refs/broken-create &&
	mkdir -p .git/$prefix &&
	echo "gobbledigook" >.git/$prefix/foo &&
	test_when_finished "rm -f .git/$prefix/foo" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: unable to resolve reference $Q$prefix/foo$Q: reference broken
	EOF
	printf "%s\n" "update $prefix/foo $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/foo$Q: unable to resolve reference $Q$prefix/foo$Q: reference broken
	EOF
	printf "%s\n" "update $prefix/foo $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'non-empty directory blocks indirect create' '
	prefix=refs/ne-indirect-create &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	mkdir -p .git/$prefix/foo/bar &&
	: >.git/$prefix/foo/bar/baz.lock &&
	test_when_finished "rm -f .git/$prefix/foo/bar/baz.lock" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: there is a non-empty directory $Q.git/$prefix/foo$Q blocking reference $Q$prefix/foo$Q
	EOF
	printf "%s\n" "update $prefix/symref $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: unable to resolve reference $Q$prefix/foo$Q
	EOF
	printf "%s\n" "update $prefix/symref $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'broken reference blocks indirect create' '
	prefix=refs/broken-indirect-create &&
	git symbolic-ref $prefix/symref $prefix/foo &&
	echo "gobbledigook" >.git/$prefix/foo &&
	test_when_finished "rm -f .git/$prefix/foo" &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: unable to resolve reference $Q$prefix/foo$Q: reference broken
	EOF
	printf "%s\n" "update $prefix/symref $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err &&
	cat >expected <<-EOF &&
	fatal: cannot lock ref $Q$prefix/symref$Q: unable to resolve reference $Q$prefix/foo$Q: reference broken
	EOF
	printf "%s\n" "update $prefix/symref $D $C" |
	test_must_fail git update-ref --stdin 2>output.err &&
	test_cmp expected output.err
'

test_expect_success 'no bogus intermediate values during delete' '
	prefix=refs/slow-transaction &&
	# Set up a reference with differing loose and packed versions:
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	git update-ref $prefix/foo $D &&
	git for-each-ref $prefix >unchanged &&
	# Now try to update the reference, but hold the `packed-refs` lock
	# for a while to see what happens while the process is blocked:
	: >.git/packed-refs.lock &&
	test_when_finished "rm -f .git/packed-refs.lock" &&
	{
		# Note: the following command is intentionally run in the
		# background. We increase the timeout so that `update-ref`
		# attempts to acquire the `packed-refs` lock for longer than
		# it takes for us to do the check then delete it:
		git -c core.packedrefstimeout=3000 update-ref -d $prefix/foo &
	} &&
	pid2=$! &&
	# Give update-ref plenty of time to get to the point where it tries
	# to lock packed-refs:
	sleep 1 &&
	# Make sure that update-ref did not complete despite the lock:
	kill -0 $pid2 &&
	# Verify that the reference still has its old value:
	sha1=$(git rev-parse --verify --quiet $prefix/foo || echo undefined) &&
	case "$sha1" in
	$D)
		# This is what we hope for; it means that nothing
		# user-visible has changed yet.
		: ;;
	undefined)
		# This is not correct; it means the deletion has happened
		# already even though update-ref should not have been
		# able to acquire the lock yet.
		echo "$prefix/foo deleted prematurely" &&
		break
		;;
	$C)
		# This value should never be seen. Probably the loose
		# reference has been deleted but the packed reference
		# is still there:
		echo "$prefix/foo incorrectly observed to be C" &&
		break
		;;
	*)
		# WTF?
		echo "unexpected value observed for $prefix/foo: $sha1" &&
		break
		;;
	esac >out &&
	rm -f .git/packed-refs.lock &&
	wait $pid2 &&
	test_must_be_empty out &&
	test_must_fail git rev-parse --verify --quiet $prefix/foo
'

test_expect_success 'delete fails cleanly if packed-refs file is locked' '
	prefix=refs/locked-packed-refs &&
	# Set up a reference with differing loose and packed versions:
	git update-ref $prefix/foo $C &&
	git pack-refs --all &&
	git update-ref $prefix/foo $D &&
	git for-each-ref $prefix >unchanged &&
	# Now try to delete it while the `packed-refs` lock is held:
	: >.git/packed-refs.lock &&
	test_when_finished "rm -f .git/packed-refs.lock" &&
	test_must_fail git update-ref -d $prefix/foo >out 2>err &&
	git for-each-ref $prefix >actual &&
	test_i18ngrep "Unable to create $Q.*packed-refs.lock$Q: File exists" err &&
	test_cmp unchanged actual
'

test_done
