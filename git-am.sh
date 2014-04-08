#!/bin/sh
#
# Copyright (c) 2005, 2006 Junio C Hamano

SUBDIRECTORY_OK=Yes
OPTIONS_KEEPDASHDASH=
OPTIONS_STUCKLONG=t
OPTIONS_SPEC="\
git am [options] [(<mbox>|<Maildir>)...]
git am [options] (--continue | --skip | --abort)
--
i,interactive   run interactively
b,binary*       (historical option -- no-op)
3,3way          allow fall back on 3way merging if needed
q,quiet         be quiet
s,signoff       add a Signed-off-by line to the commit message
u,utf8          recode into utf8 (default)
k,keep          pass -k flag to git-mailinfo
keep-non-patch  pass -b flag to git-mailinfo
keep-cr         pass --keep-cr flag to git-mailsplit for mbox format
no-keep-cr      do not pass --keep-cr flag to git-mailsplit independent of am.keepcr
c,scissors      strip everything before a scissors line
whitespace=     pass it through git-apply
ignore-space-change pass it through git-apply
ignore-whitespace pass it through git-apply
directory=      pass it through git-apply
exclude=        pass it through git-apply
include=        pass it through git-apply
C=              pass it through git-apply
p=              pass it through git-apply
patch-format=   format the patch(es) are in
reject          pass it through git-apply
resolvemsg=     override error message when patch failure occurs
continue        continue applying patches after resolving a conflict
r,resolved      synonyms for --continue
skip            skip the current patch
abort           restore the original branch and abort the patching operation.
committer-date-is-author-date    lie about committer date
ignore-date     use current timestamp for author date
rerere-autoupdate update the index with reused conflict resolution if possible
S,gpg-sign?     GPG-sign commits
rebasing*       (internal use for git-rebase)"

. git-sh-setup
. git-sh-i18n
prefix=$(git rev-parse --show-prefix)
set_reflog_action am
require_work_tree
cd_to_toplevel

git var GIT_COMMITTER_IDENT >/dev/null ||
	die "$(gettext "You need to set your committer info first")"

if git rev-parse --verify -q HEAD >/dev/null
then
	HAS_HEAD=yes
else
	HAS_HEAD=
fi

cmdline="git am"
if test '' != "$interactive"
then
	cmdline="$cmdline -i"
fi
if test '' != "$threeway"
then
	cmdline="$cmdline -3"
fi

sq () {
	git rev-parse --sq-quote "$@"
}

stop_here () {
    echo "$1" >"$dotest/next"
    git rev-parse --verify -q HEAD >"$dotest/abort-safety"
    exit 1
}

safe_to_abort () {
	if test -f "$dotest/dirtyindex"
	then
		return 1
	fi

	if ! test -s "$dotest/abort-safety"
	then
		return 0
	fi

	abort_safety=$(cat "$dotest/abort-safety")
	if test "z$(git rev-parse --verify -q HEAD)" = "z$abort_safety"
	then
		return 0
	fi
	gettextln "You seem to have moved HEAD since the last 'am' failure.
Not rewinding to ORIG_HEAD" >&2
	return 1
}

stop_here_user_resolve () {
    if [ -n "$resolvemsg" ]; then
	    printf '%s\n' "$resolvemsg"
	    stop_here $1
    fi
    eval_gettextln "When you have resolved this problem, run \"\$cmdline --continue\".
If you prefer to skip this patch, run \"\$cmdline --skip\" instead.
To restore the original branch and stop patching, run \"\$cmdline --abort\"."

    stop_here $1
}

go_next () {
	rm -f "$dotest/$msgnum" "$dotest/msg" "$dotest/msg-clean" \
		"$dotest/patch" "$dotest/info"
	echo "$next" >"$dotest/next"
	this=$next
}

cannot_fallback () {
	echo "$1"
	gettextln "Cannot fall back to three-way merge."
	exit 1
}

fall_back_3way () {
    O_OBJECT=$(cd "$GIT_OBJECT_DIRECTORY" && pwd)

    rm -fr "$dotest"/patch-merge-*
    mkdir "$dotest/patch-merge-tmp-dir"

    # First see if the patch records the index info that we can use.
    cmd="git apply $git_apply_opt --build-fake-ancestor" &&
    cmd="$cmd "'"$dotest/patch-merge-tmp-index" "$dotest/patch"' &&
    eval "$cmd" &&
    GIT_INDEX_FILE="$dotest/patch-merge-tmp-index" \
    git write-tree >"$dotest/patch-merge-base+" ||
    cannot_fallback "$(gettext "Repository lacks necessary blobs to fall back on 3-way merge.")"

    say "$(gettext "Using index info to reconstruct a base tree...")"

    cmd='GIT_INDEX_FILE="$dotest/patch-merge-tmp-index"'

    if test -z "$GIT_QUIET"
    then
	eval "$cmd git diff-index --cached --diff-filter=AM --name-status HEAD"
    fi

    cmd="$cmd git apply --cached $git_apply_opt"' <"$dotest/patch"'
    if eval "$cmd"
    then
	mv "$dotest/patch-merge-base+" "$dotest/patch-merge-base"
	mv "$dotest/patch-merge-tmp-index" "$dotest/patch-merge-index"
    else
	cannot_fallback "$(gettext "Did you hand edit your patch?
It does not apply to blobs recorded in its index.")"
    fi

    test -f "$dotest/patch-merge-index" &&
    his_tree=$(GIT_INDEX_FILE="$dotest/patch-merge-index" git write-tree) &&
    orig_tree=$(cat "$dotest/patch-merge-base") &&
    rm -fr "$dotest"/patch-merge-* || exit 1

    say "$(gettext "Falling back to patching base and 3-way merge...")"

    # This is not so wrong.  Depending on which base we picked,
    # orig_tree may be wildly different from ours, but his_tree
    # has the same set of wildly different changes in parts the
    # patch did not touch, so recursive ends up canceling them,
    # saying that we reverted all those changes.

    eval GITHEAD_$his_tree='"$FIRSTLINE"'
    export GITHEAD_$his_tree
    if test -n "$GIT_QUIET"
    then
	    GIT_MERGE_VERBOSITY=0 && export GIT_MERGE_VERBOSITY
    fi
    git-merge-recursive $orig_tree -- HEAD $his_tree || {
	    git rerere $allow_rerere_autoupdate
	    die "$(gettext "Failed to merge in the changes.")"
    }
    unset GITHEAD_$his_tree
}

clean_abort () {
	test $# = 0 || echo >&2 "$@"
	rm -fr "$dotest"
	exit 1
}

patch_format=

check_patch_format () {
	# early return if patch_format was set from the command line
	if test -n "$patch_format"
	then
		return 0
	fi

	# we default to mbox format if input is from stdin and for
	# directories
	if test $# = 0 || test "x$1" = "x-" || test -d "$1"
	then
		patch_format=mbox
		return 0
	fi

	# otherwise, check the first few non-blank lines of the first
	# patch to try to detect its format
	{
		# Start from first line containing non-whitespace
		l1=
		while test -z "$l1"
		do
			read l1 || break
		done
		read l2
		read l3
		case "$l1" in
		"From "* | "From: "*)
			patch_format=mbox
			;;
		'# This series applies on GIT commit'*)
			patch_format=stgit-series
			;;
		"# HG changeset patch")
			patch_format=hg
			;;
		*)
			# if the second line is empty and the third is
			# a From, Author or Date entry, this is very
			# likely an StGIT patch
			case "$l2,$l3" in
			,"From: "* | ,"Author: "* | ,"Date: "*)
				patch_format=stgit
				;;
			*)
				;;
			esac
			;;
		esac
		if test -z "$patch_format" &&
			test -n "$l1" &&
			test -n "$l2" &&
			test -n "$l3"
		then
			# This begins with three non-empty lines.  Is this a
			# piece of e-mail a-la RFC2822?  Grab all the headers,
			# discarding the indented remainder of folded lines,
			# and see if it looks like that they all begin with the
			# header field names...
			tr -d '\015' <"$1" |
			sed -n -e '/^$/q' -e '/^[ 	]/d' -e p |
			sane_egrep -v '^[!-9;-~]+:' >/dev/null ||
			patch_format=mbox
		fi
	} < "$1" || clean_abort
}

split_patches () {
	case "$patch_format" in
	mbox)
		if test t = "$keepcr"
		then
		    keep_cr=--keep-cr
		else
		    keep_cr=
		fi
		git mailsplit -d"$prec" -o"$dotest" -b $keep_cr -- "$@" > "$dotest/last" ||
		clean_abort
		;;
	stgit-series)
		if test $# -ne 1
		then
			clean_abort "$(gettext "Only one StGIT patch series can be applied at once")"
		fi
		series_dir=$(dirname "$1")
		series_file="$1"
		shift
		{
			set x
			while read filename
			do
				set "$@" "$series_dir/$filename"
			done
			# remove the safety x
			shift
			# remove the arg coming from the first-line comment
			shift
		} < "$series_file" || clean_abort
		# set the patch format appropriately
		patch_format=stgit
		# now handle the actual StGIT patches
		split_patches "$@"
		;;
	stgit)
		this=0
		for stgit in "$@"
		do
			this=$(expr "$this" + 1)
			msgnum=$(printf "%0${prec}d" $this)
			# Perl version of StGIT parse_patch. The first nonemptyline
			# not starting with Author, From or Date is the
			# subject, and the body starts with the next nonempty
			# line not starting with Author, From or Date
			@@PERL@@ -ne 'BEGIN { $subject = 0 }
				if ($subject > 1) { print ; }
				elsif (/^\s+$/) { next ; }
				elsif (/^Author:/) { s/Author/From/ ; print ;}
				elsif (/^(From|Date)/) { print ; }
				elsif ($subject) {
					$subject = 2 ;
					print "\n" ;
					print ;
				} else {
					print "Subject: ", $_ ;
					$subject = 1;
				}
			' < "$stgit" > "$dotest/$msgnum" || clean_abort
		done
		echo "$this" > "$dotest/last"
		this=
		msgnum=
		;;
	hg)
		this=0
		for hg in "$@"
		do
			this=$(( $this + 1 ))
			msgnum=$(printf "%0${prec}d" $this)
			# hg stores changeset metadata in #-commented lines preceding
			# the commit message and diff(s). The only metadata we care about
			# are the User and Date (Node ID and Parent are hashes which are
			# only relevant to the hg repository and thus not useful to us)
			# Since we cannot guarantee that the commit message is in
			# git-friendly format, we put no Subject: line and just consume
			# all of the message as the body
			LANG=C LC_ALL=C @@PERL@@ -M'POSIX qw(strftime)' -ne 'BEGIN { $subject = 0 }
				if ($subject) { print ; }
				elsif (/^\# User /) { s/\# User/From:/ ; print ; }
				elsif (/^\# Date /) {
					my ($hashsign, $str, $time, $tz) = split ;
					$tz = sprintf "%+05d", (0-$tz)/36;
					print "Date: " .
					      strftime("%a, %d %b %Y %H:%M:%S ",
						       localtime($time))
					      . "$tz\n";
				} elsif (/^\# /) { next ; }
				else {
					print "\n", $_ ;
					$subject = 1;
				}
			' <"$hg" >"$dotest/$msgnum" || clean_abort
		done
		echo "$this" >"$dotest/last"
		this=
		msgnum=
		;;
	*)
		if test -n "$patch_format"
		then
			clean_abort "$(eval_gettext "Patch format \$patch_format is not supported.")"
		else
			clean_abort "$(gettext "Patch format detection failed.")"
		fi
		;;
	esac
}

prec=4
dotest="$GIT_DIR/rebase-apply"
sign= utf8=t keep= keepcr= skip= interactive= resolved= rebasing= abort=
resolvemsg= resume= scissors= no_inbody_headers=
git_apply_opt=
committer_date_is_author_date=
ignore_date=
allow_rerere_autoupdate=
gpg_sign_opt=

if test "$(git config --bool --get am.keepcr)" = true
then
    keepcr=t
fi

while test $# != 0
do
	case "$1" in
	-i|--interactive)
		interactive=t ;;
	-b|--binary)
		gettextln >&2 "The -b/--binary option has been a no-op for long time, and
it will be removed. Please do not use it anymore."
		;;
	-3|--3way)
		threeway=t ;;
	-s|--signoff)
		sign=t ;;
	-u|--utf8)
		utf8=t ;; # this is now default
	--no-utf8)
		utf8= ;;
	-k|--keep)
		keep=t ;;
	--keep-non-patch)
		keep=b ;;
	-c|--scissors)
		scissors=t ;;
	--no-scissors)
		scissors=f ;;
	-r|--resolved|--continue)
		resolved=t ;;
	--skip)
		skip=t ;;
	--abort)
		abort=t ;;
	--rebasing)
		rebasing=t threeway=t ;;
	--resolvemsg=*)
		resolvemsg="${1#--resolvemsg=}" ;;
	--whitespace=*|--directory=*|--exclude=*|--include=*)
		git_apply_opt="$git_apply_opt $(sq "$1")" ;;
	-C*|-p*)
		git_apply_opt="$git_apply_opt $(sq "$1")" ;;
	--patch-format=*)
		patch_format="${1#--patch-format=}" ;;
	--reject|--ignore-whitespace|--ignore-space-change)
		git_apply_opt="$git_apply_opt $1" ;;
	--committer-date-is-author-date)
		committer_date_is_author_date=t ;;
	--ignore-date)
		ignore_date=t ;;
	--rerere-autoupdate|--no-rerere-autoupdate)
		allow_rerere_autoupdate="$1" ;;
	-q|--quiet)
		GIT_QUIET=t ;;
	--keep-cr)
		keepcr=t ;;
	--no-keep-cr)
		keepcr=f ;;
	--gpg-sign)
		gpg_sign_opt=-S ;;
	--gpg-sign=*)
		gpg_sign_opt="-S${1#--gpg-sign=}" ;;
	--)
		shift; break ;;
	*)
		usage ;;
	esac
	shift
done

# If the dotest directory exists, but we have finished applying all the
# patches in them, clear it out.
if test -d "$dotest" &&
   test -f "$dotest/last" &&
   test -f "$dotest/next" &&
   last=$(cat "$dotest/last") &&
   next=$(cat "$dotest/next") &&
   test $# != 0 &&
   test "$next" -gt "$last"
then
   rm -fr "$dotest"
fi

if test -d "$dotest" && test -f "$dotest/last" && test -f "$dotest/next"
then
	case "$#,$skip$resolved$abort" in
	0,*t*)
		# Explicit resume command and we do not have file, so
		# we are happy.
		: ;;
	0,)
		# No file input but without resume parameters; catch
		# user error to feed us a patch from standard input
		# when there is already $dotest.  This is somewhat
		# unreliable -- stdin could be /dev/null for example
		# and the caller did not intend to feed us a patch but
		# wanted to continue unattended.
		test -t 0
		;;
	*)
		false
		;;
	esac ||
	die "$(eval_gettext "previous rebase directory \$dotest still exists but mbox given.")"
	resume=yes

	case "$skip,$abort" in
	t,t)
		die "$(gettext "Please make up your mind. --skip or --abort?")"
		;;
	t,)
		git rerere clear
		git read-tree --reset -u HEAD HEAD
		orig_head=$(cat "$GIT_DIR/ORIG_HEAD")
		git reset HEAD
		git update-ref ORIG_HEAD $orig_head
		;;
	,t)
		if test -f "$dotest/rebasing"
		then
			exec git rebase --abort
		fi
		git rerere clear
		if safe_to_abort
		then
			git read-tree --reset -u HEAD ORIG_HEAD
			git reset ORIG_HEAD
		fi
		rm -fr "$dotest"
		exit ;;
	esac
	rm -f "$dotest/dirtyindex"
else
	# Possible stray $dotest directory in the independent-run
	# case; in the --rebasing case, it is upto the caller
	# (git-rebase--am) to take care of stray directories.
	if test -d "$dotest" && test -z "$rebasing"
	then
		case "$skip,$resolved,$abort" in
		,,t)
			rm -fr "$dotest"
			exit 0
			;;
		*)
			die "$(eval_gettext "Stray \$dotest directory found.
Use \"git am --abort\" to remove it.")"
			;;
		esac
	fi

	# Make sure we are not given --skip, --continue, or --abort
	test "$skip$resolved$abort" = "" ||
		die "$(gettext "Resolve operation not in progress, we are not resuming.")"

	# Start afresh.
	mkdir -p "$dotest" || exit

	if test -n "$prefix" && test $# != 0
	then
		first=t
		for arg
		do
			test -n "$first" && {
				set x
				first=
			}
			if is_absolute_path "$arg"
			then
				set "$@" "$arg"
			else
				set "$@" "$prefix$arg"
			fi
		done
		shift
	fi

	check_patch_format "$@"

	split_patches "$@"

	# -i can and must be given when resuming; everything
	# else is kept
	echo " $git_apply_opt" >"$dotest/apply-opt"
	echo "$threeway" >"$dotest/threeway"
	echo "$sign" >"$dotest/sign"
	echo "$utf8" >"$dotest/utf8"
	echo "$keep" >"$dotest/keep"
	echo "$scissors" >"$dotest/scissors"
	echo "$no_inbody_headers" >"$dotest/no_inbody_headers"
	echo "$GIT_QUIET" >"$dotest/quiet"
	echo 1 >"$dotest/next"
	if test -n "$rebasing"
	then
		: >"$dotest/rebasing"
	else
		: >"$dotest/applying"
		if test -n "$HAS_HEAD"
		then
			git update-ref ORIG_HEAD HEAD
		else
			git update-ref -d ORIG_HEAD >/dev/null 2>&1
		fi
	fi
fi

git update-index -q --refresh

case "$resolved" in
'')
	case "$HAS_HEAD" in
	'')
		files=$(git ls-files) ;;
	?*)
		files=$(git diff-index --cached --name-only HEAD --) ;;
	esac || exit
	if test "$files"
	then
		test -n "$HAS_HEAD" && : >"$dotest/dirtyindex"
		die "$(eval_gettext "Dirty index: cannot apply patches (dirty: \$files)")"
	fi
esac

# Now, decide what command line options we will give to the git
# commands we invoke, based on the result of parsing command line
# options and previous invocation state stored in $dotest/ files.

if test "$(cat "$dotest/utf8")" = t
then
	utf8=-u
else
	utf8=-n
fi
keep=$(cat "$dotest/keep")
case "$keep" in
t)
	keep=-k ;;
b)
	keep=-b ;;
*)
	keep= ;;
esac
case "$(cat "$dotest/scissors")" in
t)
	scissors=--scissors ;;
f)
	scissors=--no-scissors ;;
esac
if test "$(cat "$dotest/no_inbody_headers")" = t
then
	no_inbody_headers=--no-inbody-headers
else
	no_inbody_headers=
fi
if test "$(cat "$dotest/quiet")" = t
then
	GIT_QUIET=t
fi
if test "$(cat "$dotest/threeway")" = t
then
	threeway=t
fi
git_apply_opt=$(cat "$dotest/apply-opt")
if test "$(cat "$dotest/sign")" = t
then
	SIGNOFF=$(git var GIT_COMMITTER_IDENT | sed -e '
			s/>.*/>/
			s/^/Signed-off-by: /'
		)
else
	SIGNOFF=
fi

last=$(cat "$dotest/last")
this=$(cat "$dotest/next")
if test "$skip" = t
then
	this=$(expr "$this" + 1)
	resume=
fi

while test "$this" -le "$last"
do
	msgnum=$(printf "%0${prec}d" $this)
	next=$(expr "$this" + 1)
	test -f "$dotest/$msgnum" || {
		resume=
		go_next
		continue
	}

	# If we are not resuming, parse and extract the patch information
	# into separate files:
	#  - info records the authorship and title
	#  - msg is the rest of commit log message
	#  - patch is the patch body.
	#
	# When we are resuming, these files are either already prepared
	# by the user, or the user can tell us to do so by --continue flag.
	case "$resume" in
	'')
		if test -f "$dotest/rebasing"
		then
			commit=$(sed -e 's/^From \([0-9a-f]*\) .*/\1/' \
				-e q "$dotest/$msgnum") &&
			test "$(git cat-file -t "$commit")" = commit ||
				stop_here $this
			git cat-file commit "$commit" |
			sed -e '1,/^$/d' >"$dotest/msg-clean"
			echo "$commit" >"$dotest/original-commit"
			get_author_ident_from_commit "$commit" >"$dotest/author-script"
			git diff-tree --root --binary --full-index "$commit" >"$dotest/patch"
		else
			git mailinfo $keep $no_inbody_headers $scissors $utf8 "$dotest/msg" "$dotest/patch" \
				<"$dotest/$msgnum" >"$dotest/info" ||
				stop_here $this

			# skip pine's internal folder data
			sane_grep '^Author: Mail System Internal Data$' \
				<"$dotest"/info >/dev/null &&
				go_next && continue

			test -s "$dotest/patch" || {
				eval_gettextln "Patch is empty.  Was it split wrong?
If you would prefer to skip this patch, instead run \"\$cmdline --skip\".
To restore the original branch and stop patching run \"\$cmdline --abort\"."
				stop_here $this
			}
			rm -f "$dotest/original-commit" "$dotest/author-script"
			{
				sed -n '/^Subject/ s/Subject: //p' "$dotest/info"
				echo
				cat "$dotest/msg"
			} |
			git stripspace > "$dotest/msg-clean"
		fi
		;;
	esac

	if test -f "$dotest/author-script"
	then
		eval $(cat "$dotest/author-script")
	else
		GIT_AUTHOR_NAME="$(sed -n '/^Author/ s/Author: //p' "$dotest/info")"
		GIT_AUTHOR_EMAIL="$(sed -n '/^Email/ s/Email: //p' "$dotest/info")"
		GIT_AUTHOR_DATE="$(sed -n '/^Date/ s/Date: //p' "$dotest/info")"
	fi

	if test -z "$GIT_AUTHOR_EMAIL"
	then
		gettextln "Patch does not have a valid e-mail address."
		stop_here $this
	fi

	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE

	case "$resume" in
	'')
	    if test '' != "$SIGNOFF"
	    then
		LAST_SIGNED_OFF_BY=$(
		    sed -ne '/^Signed-off-by: /p' \
		    "$dotest/msg-clean" |
		    sed -ne '$p'
		)
		ADD_SIGNOFF=$(
		    test "$LAST_SIGNED_OFF_BY" = "$SIGNOFF" || {
		    test '' = "$LAST_SIGNED_OFF_BY" && echo
		    echo "$SIGNOFF"
		})
	    else
		ADD_SIGNOFF=
	    fi
	    {
		if test -s "$dotest/msg-clean"
		then
			cat "$dotest/msg-clean"
		fi
		if test '' != "$ADD_SIGNOFF"
		then
			echo "$ADD_SIGNOFF"
		fi
	    } >"$dotest/final-commit"
	    ;;
	*)
		case "$resolved$interactive" in
		tt)
			# This is used only for interactive view option.
			git diff-index -p --cached HEAD -- >"$dotest/patch"
			;;
		esac
	esac

	resume=
	if test "$interactive" = t
	then
	    test -t 0 ||
	    die "$(gettext "cannot be interactive without stdin connected to a terminal.")"
	    action=again
	    while test "$action" = again
	    do
		gettextln "Commit Body is:"
		echo "--------------------------"
		cat "$dotest/final-commit"
		echo "--------------------------"
		# TRANSLATORS: Make sure to include [y], [n], [e], [v] and [a]
		# in your translation. The program will only accept English
		# input at this point.
		gettext "Apply? [y]es/[n]o/[e]dit/[v]iew patch/[a]ccept all "
		read reply
		case "$reply" in
		[yY]*) action=yes ;;
		[aA]*) action=yes interactive= ;;
		[nN]*) action=skip ;;
		[eE]*) git_editor "$dotest/final-commit"
		       action=again ;;
		[vV]*) action=again
		       git_pager "$dotest/patch" ;;
		*)     action=again ;;
		esac
	    done
	else
	    action=yes
	fi

	if test $action = skip
	then
		go_next
		continue
	fi

	if test -x "$GIT_DIR"/hooks/applypatch-msg
	then
		"$GIT_DIR"/hooks/applypatch-msg "$dotest/final-commit" ||
		stop_here $this
	fi

	if test -f "$dotest/final-commit"
	then
		FIRSTLINE=$(sed 1q "$dotest/final-commit")
	else
		FIRSTLINE=""
	fi

	say "$(eval_gettext "Applying: \$FIRSTLINE")"

	case "$resolved" in
	'')
		# When we are allowed to fall back to 3-way later, don't give
		# false errors during the initial attempt.
		squelch=
		if test "$threeway" = t
		then
			squelch='>/dev/null 2>&1 '
		fi
		eval "git apply $squelch$git_apply_opt"' --index "$dotest/patch"'
		apply_status=$?
		;;
	t)
		# Resolved means the user did all the hard work, and
		# we do not have to do any patch application.  Just
		# trust what the user has in the index file and the
		# working tree.
		resolved=
		git diff-index --quiet --cached HEAD -- && {
			gettextln "No changes - did you forget to use 'git add'?
If there is nothing left to stage, chances are that something else
already introduced the same changes; you might want to skip this patch."
			stop_here_user_resolve $this
		}
		unmerged=$(git ls-files -u)
		if test -n "$unmerged"
		then
			gettextln "You still have unmerged paths in your index
did you forget to use 'git add'?"
			stop_here_user_resolve $this
		fi
		apply_status=0
		git rerere
		;;
	esac

	if test $apply_status != 0 && test "$threeway" = t
	then
		if (fall_back_3way)
		then
		    # Applying the patch to an earlier tree and merging the
		    # result may have produced the same tree as ours.
		    git diff-index --quiet --cached HEAD -- && {
			say "$(gettext "No changes -- Patch already applied.")"
			go_next
			continue
		    }
		    # clear apply_status -- we have successfully merged.
		    apply_status=0
		fi
	fi
	if test $apply_status != 0
	then
		eval_gettextln 'Patch failed at $msgnum $FIRSTLINE'
		if test "$(git config --bool advice.amworkdir)" != false
		then
			eval_gettextln 'The copy of the patch that failed is found in:
   $dotest/patch'
		fi
		stop_here_user_resolve $this
	fi

	if test -x "$GIT_DIR"/hooks/pre-applypatch
	then
		"$GIT_DIR"/hooks/pre-applypatch || stop_here $this
	fi

	tree=$(git write-tree) &&
	commit=$(
		if test -n "$ignore_date"
		then
			GIT_AUTHOR_DATE=
		fi
		parent=$(git rev-parse --verify -q HEAD) ||
		say >&2 "$(gettext "applying to an empty history")"

		if test -n "$committer_date_is_author_date"
		then
			GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"
			export GIT_COMMITTER_DATE
		fi &&
		git commit-tree ${parent:+-p} $parent ${gpg_sign_opt:+"$gpg_sign_opt"} $tree  \
			<"$dotest/final-commit"
	) &&
	git update-ref -m "$GIT_REFLOG_ACTION: $FIRSTLINE" HEAD $commit $parent ||
	stop_here $this

	if test -f "$dotest/original-commit"; then
		echo "$(cat "$dotest/original-commit") $commit" >> "$dotest/rewritten"
	fi

	if test -x "$GIT_DIR"/hooks/post-applypatch
	then
		"$GIT_DIR"/hooks/post-applypatch
	fi

	go_next
done

if test -s "$dotest"/rewritten; then
    git notes copy --for-rewrite=rebase < "$dotest"/rewritten
    if test -x "$GIT_DIR"/hooks/post-rewrite; then
	"$GIT_DIR"/hooks/post-rewrite rebase < "$dotest"/rewritten
    fi
fi

# If am was called with --rebasing (from git-rebase--am), it's up to
# the caller to take care of housekeeping.
if ! test -f "$dotest/rebasing"
then
	rm -fr "$dotest"
	git gc --auto
fi
