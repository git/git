#!/bin/sh
OPTIONS_KEEPDASHDASH=
OPTIONS_STUCKLONG=
OPTIONS_SPEC="\
git quiltimport [options]
--
n,dry-run     dry run
author=       author name and email address for patches without any
C=            minimum context (see git apply)
exact         allow no-fuzz
patches=      path to the quilt series and patches
"
SUBDIRECTORY_ON=Yes
. git-sh-setup

dry_run=""
quilt_author=""
cflag=
fuzz_specified=
while test $# != 0
do
	case "$1" in
	--author)
		shift
		quilt_author="$1"
		;;
	-C)
		shift
		# ensure numerical parameter
		case "$1" in
		''|*[!0-9]*) usage;;
		*) ;;
		esac
		cflag="-C$1"
		fuzz_specified=yes
		;;
	--exact)
		cflag=
		fuzz_specified=yes
		;;
	-n|--dry-run)
		dry_run=1
		;;
	--patches)
		shift
		QUILT_PATCHES="$1"
		;;
	--)
		shift
		break;;
	*)
		usage
		;;
	esac
	shift
done

# Quilt Author
if [ -n "$quilt_author" ] ; then
	quilt_author_name=$(expr "z$quilt_author" : 'z\(.*[^ ]\) *<.*') &&
	quilt_author_email=$(expr "z$quilt_author" : '.*<\([^>]*\)') &&
	test '' != "$quilt_author_name" &&
	test '' != "$quilt_author_email" ||
	die "malformed --author parameter"
fi

# Quilt patch directory
: ${QUILT_PATCHES:=patches}
if ! [ -d "$QUILT_PATCHES" ] ; then
	echo "The \"$QUILT_PATCHES\" directory does not exist."
	exit 1
fi

# Temporary directories
tmp_dir="$GIT_DIR"/rebase-apply
tmp_msg="$tmp_dir/msg"
tmp_patch="$tmp_dir/patch"
tmp_info="$tmp_dir/info"

# Helper to warn about -C$n option
do_apply () {
	if git apply --index ${cflag+"$cflag"} "$@"
	then
		return
	fi
	if test -z "$fuzz_specified" &&
	   git apply --check --index -C1 "$@" >/dev/null 2>&1
	then
		cat >&2 <<-\EOM
		'git quiltimport' by default no longer attempts to apply
		patches with reduced context lines to allow fuzz; if you
		want the old 'unsafe' behaviour, run the command with -C1
		option.
		EOM

	fi
	return 1
}

# Find the initial commit
commit=$(git rev-parse HEAD)

mkdir $tmp_dir || exit 2
while read patch_name level garbage <&3
do
	case "$patch_name" in ''|'#'*) continue;; esac
	case "$level" in
	-p*)	;;
	''|'#'*)
		level=;;
	*)
		echo "unable to parse patch level, ignoring it."
		level=;;
	esac
	case "$garbage" in
	''|'#'*);;
	*)
		echo "trailing garbage found in series file: $garbage"
		exit 1;;
	esac
	if ! [ -f "$QUILT_PATCHES/$patch_name" ] ; then
		echo "$patch_name doesn't exist. Skipping."
		continue
	fi
	echo $patch_name
	git mailinfo "$tmp_msg" "$tmp_patch" \
		<"$QUILT_PATCHES/$patch_name" >"$tmp_info" || exit 3
	test -s "$tmp_patch" || {
		echo "Patch is empty.  Was it split wrong?"
		exit 1
	}

	# Parse the author information
	GIT_AUTHOR_NAME=$(sed -ne 's/Author: //p' "$tmp_info")
	GIT_AUTHOR_EMAIL=$(sed -ne 's/Email: //p' "$tmp_info")
	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL
	while test -z "$GIT_AUTHOR_EMAIL" && test -z "$GIT_AUTHOR_NAME" ; do
		if [ -n "$quilt_author" ] ; then
			GIT_AUTHOR_NAME="$quilt_author_name";
			GIT_AUTHOR_EMAIL="$quilt_author_email";
		elif [ -n "$dry_run" ]; then
			echo "No author found in $patch_name" >&2;
			GIT_AUTHOR_NAME="dry-run-not-found";
			GIT_AUTHOR_EMAIL="dry-run-not-found";
		else
			echo "No author found in $patch_name" >&2;
			echo "---"
			cat $tmp_msg
			printf "Author: ";
			read patch_author

			echo "$patch_author"

			patch_author_name=$(expr "z$patch_author" : 'z\(.*[^ ]\) *<.*') &&
			patch_author_email=$(expr "z$patch_author" : '.*<\([^>]*\)') &&
			test '' != "$patch_author_name" &&
			test '' != "$patch_author_email" &&
			GIT_AUTHOR_NAME="$patch_author_name" &&
			GIT_AUTHOR_EMAIL="$patch_author_email"
		fi
	done
	GIT_AUTHOR_DATE=$(sed -ne 's/Date: //p' "$tmp_info")
	SUBJECT=$(sed -ne 's/Subject: //p' "$tmp_info")
	export GIT_AUTHOR_DATE SUBJECT
	if [ -z "$SUBJECT" ] ; then
		SUBJECT=$(echo $patch_name | sed -e 's/.patch$//')
	fi

	if [ -z "$dry_run" ] ; then
		do_apply ${level:+"$level"} "$tmp_patch" &&
		tree=$(git write-tree) &&
		commit=$( (echo "$SUBJECT"; echo; cat "$tmp_msg") | git commit-tree $tree -p $commit) &&
		git update-ref -m "quiltimport: $patch_name" HEAD $commit || exit 4
	fi
done 3<"$QUILT_PATCHES/series"
rm -rf $tmp_dir || exit 5
