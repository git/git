#!/bin/sh
#
# spatchcache: a poor-man's "ccache"-alike for "spatch" in git.git
#
# This caching command relies on the peculiarities of the Makefile
# driving "spatch" in git.git, in particular if we invoke:
#
#	make
#	# See "spatchCache.cacheWhenStderr" for why "--very-quiet" is
#	# used
#	make coccicheck SPATCH_FLAGS=--very-quiet
#
# We can with COMPUTE_HEADER_DEPENDENCIES (auto-detected as true with
# "gcc" and "clang") write e.g. a .depend/grep.o.d for grep.c, when we
# compile grep.o.
#
# The .depend/grep.o.d will have the full header dependency tree of
# grep.c, and we can thus cache the output of "spatch" by:
#
#	1. Hashing all of those files
#	2. Hashing our source file, and the *.cocci rule we're
#	   applying
#	3. Running spatch, if suggests no changes (by far the common
#	   case) we invoke "spatchCache.getCmd" and
#	   "spatchCache.setCmd" with a hash SHA-256 to ask "does this
#	   ID have no changes" or "say that ID had no changes>
#	4. If no "spatchCache.{set,get}Cmd" is specified we'll use
#	   "redis-cli" and maintain a SET called "spatch-cache". Set
#	   appropriate redis memory policies to keep it from growing
#	   out of control.
#
# This along with the general incremental "make" support for
# "contrib/coccinelle" makes it viable to (re-)run coccicheck
# e.g. when merging integration branches.
#
# Note that the "--very-quiet" flag is currently critical. The cache
# will refuse to cache anything that has output on STDERR (which might
# be errors from spatch), but see spatchCache.cacheWhenStderr below.
#
# The STDERR (and exit code) could in principle be cached (as with
# ccache), but then the simple structure in the Redis cache would need
# to change, so just supply "--very-quiet" for now.
#
# To use this, simply set SPATCH to
# contrib/coccinelle/spatchcache. Then optionally set:
#
#	[spatchCache]
#		# Optional: path to a custom spatch
#		spatch = ~/g/coccicheck/spatch.opt
#
# As well as this trace config (debug implies trace):
#
#		cacheWhenStderr = true
#		trace = false
#		debug = false
#
# The ".depend/grep.o.d" can also be customized, as a string that will
# be eval'd, it has access to a "$dirname" and "$basename":
#
#	[spatchCache]
#		dependFormat = "$dirname/.depend/${basename%.c}.o.d"
#
# Setting "trace" to "true" allows for seeing when we have a cache HIT
# or MISS. To debug whether the cache is working do that, and run e.g.:
#
#	redis-cli FLUSHALL
#	<make && make coccicheck, as above>
#	grep -hore HIT -e MISS -e SET -e NOCACHE -e CANTCACHE .build/contrib/coccinelle | sort | uniq -c
#	    600 CANTCACHE
#	   7365 MISS
#	   7365 SET
#
# A subsequent "make cocciclean && make coccicheck" should then have
# all "HIT"'s and "CANTCACHE"'s.
#
# The "spatchCache.cacheWhenStderr" option is critical when using
# spatchCache.{trace,debug} to debug whether something is set in the
# cache, as we'll write to the spatch logs in .build/* we'd otherwise
# always emit a NOCACHE.
#
# Reading the config can make the command much slower, to work around
# this the config can be set in the environment, with environment
# variable name corresponding to the config key. "default" can be used
# to use whatever's the script default, e.g. setting
# spatchCache.cacheWhenStderr=true and deferring to the defaults for
# the rest is:
#
#	export GIT_CONTRIB_SPATCHCACHE_DEBUG=default
#	export GIT_CONTRIB_SPATCHCACHE_TRACE=default
#	export GIT_CONTRIB_SPATCHCACHE_CACHEWHENSTDERR=true
#	export GIT_CONTRIB_SPATCHCACHE_SPATCH=default
#	export GIT_CONTRIB_SPATCHCACHE_DEPENDFORMAT=default
#	export GIT_CONTRIB_SPATCHCACHE_SETCMD=default
#	export GIT_CONTRIB_SPATCHCACHE_GETCMD=default

set -e

env_or_config () {
	env="$1"
	shift
	if test "$env" = "default"
	then
		# Avoid expensive "git config" invocation
		return
	elif test -n "$env"
	then
		echo "$env"
	else
		git config $@ || :
	fi
}

## Our own configuration & options
debug=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_DEBUG" --bool "spatchCache.debug")
if test "$debug" != "true"
then
	debug=
fi
if test -n "$debug"
then
	set -x
fi

trace=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_TRACE" --bool "spatchCache.trace")
if test "$trace" != "true"
then
	trace=
fi
if test -n "$debug"
then
	# debug implies trace
	trace=true
fi

cacheWhenStderr=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_CACHEWHENSTDERR" --bool "spatchCache.cacheWhenStderr")
if test "$cacheWhenStderr" != "true"
then
	cacheWhenStderr=
fi

trace_it () {
	if test -z "$trace"
	then
		return
	fi
	echo "$@" >&2
}

spatch=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_SPATCH" --path "spatchCache.spatch")
if test -n "$spatch"
then
	if test -n "$debug"
	then
		trace_it "custom spatchCache.spatch='$spatch'"
	fi
else
	spatch=spatch
fi

dependFormat='$dirname/.depend/${basename%.c}.o.d'
dependFormatCfg=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_DEPENDFORMAT" "spatchCache.dependFormat")
if test -n "$dependFormatCfg"
then
	dependFormat="$dependFormatCfg"
fi

set=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_SETCMD" "spatchCache.setCmd")
get=$(env_or_config "$GIT_CONTRIB_SPATCHCACHE_GETCMD" "spatchCache.getCmd")

## Parse spatch()-like command-line for caching info
arg_sp=
arg_file=
args="$@"
spatch_opts() {
	while test $# != 0
	do
		arg_file="$1"
		case "$1" in
		--sp-file)
			arg_sp="$2"
			;;
		esac
		shift
	done
}
spatch_opts "$@"
if ! test -f "$arg_file"
then
	arg_file=
fi

hash_for_cache() {
	# Parameters that should affect the cache
	echo "args=$args"
	echo "config spatchCache.spatch=$spatch"
	echo "config spatchCache.debug=$debug"
	echo "config spatchCache.trace=$trace"
	echo "config spatchCache.cacheWhenStderr=$cacheWhenStderr"
	echo

	# Our target file and its dependencies
	git hash-object "$1" "$2" $(grep -E -o '^[^:]+:$' "$3" | tr -d ':')
}

# Sanity checks
if ! test -f "$arg_sp" && ! test -f "$arg_file"
then
	echo $0: no idea how to cache "$@" >&2
	exit 128
fi

# Main logic
dirname=$(dirname "$arg_file")
basename=$(basename "$arg_file")
eval "dep=$dependFormat"

if ! test -f "$dep"
then
	trace_it "$0: CANTCACHE have no '$dep' for '$arg_file'!"
	exec "$spatch" "$@"
fi

if test -n "$debug"
then
	trace_it "$0: The full cache input for '$arg_sp' '$arg_file' '$dep'"
	hash_for_cache "$arg_sp" "$arg_file" "$dep" >&2
fi
sum=$(hash_for_cache "$arg_sp" "$arg_file" "$dep" | git hash-object --stdin)

trace_it "$0: processing '$arg_file' with '$arg_sp' rule, and got hash '$sum' for it + '$dep'"

getret=
if test -z "$get"
then
	if test $(redis-cli SISMEMBER spatch-cache "$sum") = 1
	then
		getret=0
	else
		getret=1
	fi
else
	$set "$sum"
	getret=$?
fi

if test "$getret" = 0
then
	trace_it "$0: HIT for '$arg_file' with '$arg_sp'"
	exit 0
else
	trace_it "$0: MISS: for '$arg_file' with '$arg_sp'"
fi

out="$(mktemp)"
err="$(mktemp)"

set +e
"$spatch" "$@" >"$out" 2>>"$err"
ret=$?
cat "$out"
cat "$err" >&2
set -e

nocache=
if test $ret != 0
then
	nocache="exited non-zero: $ret"
elif test -s "$out"
then
	nocache="had patch output"
elif test -z "$cacheWhenStderr" && test -s "$err"
then
	nocache="had stderr (use --very-quiet or spatchCache.cacheWhenStderr=true?)"
fi

if test -n "$nocache"
then
	trace_it "$0: NOCACHE ($nocache): for '$arg_file' with '$arg_sp'"
	exit "$ret"
fi

trace_it "$0: SET: for '$arg_file' with '$arg_sp'"

setret=
if test -z "$set"
then
	if test $(redis-cli SADD spatch-cache "$sum") = 1
	then
		setret=0
	else
		setret=1
	fi
else
	"$set" "$sum"
	setret=$?
fi

if test "$setret" != 0
then
	echo "FAILED to set '$sum' in cache!" >&2
	exit 128
fi

exit "$ret"
