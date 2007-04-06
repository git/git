#!/bin/sh
USAGE='--dry-run --author <author> --patches </path/to/quilt/patch/directory>'
SUBDIRECTORY_ON=Yes
. git-sh-setup

dry_run=""
quilt_author=""
while case "$#" in 0) break;; esac
do
	case "$1" in
	--au=*|--aut=*|--auth=*|--autho=*|--author=*)
		quilt_author=$(expr "z$1" : 'z-[^=]*\(.*\)')
		shift
		;;

	--au|--aut|--auth|--autho|--author)
		case "$#" in 1) usage ;; esac
		shift
		quilt_author="$1"
		shift
		;;

	--dry-run)
		shift
		dry_run=1
		;;

	--pa=*|--pat=*|--patc=*|--patch=*|--patche=*|--patches=*)
		QUILT_PATCHES=$(expr "z$1" : 'z-[^=]*\(.*\)')
		shift
		;;

	--pa|--pat|--patc|--patch|--patche|--patches)
		case "$#" in 1) usage ;; esac
		shift
		QUILT_PATCHES="$1"
		shift
		;;

	*)
		break
		;;
	esac
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
tmp_dir=.dotest
tmp_msg="$tmp_dir/msg"
tmp_patch="$tmp_dir/patch"
tmp_info="$tmp_dir/info"


# Find the intial commit
commit=$(git-rev-parse HEAD)

mkdir $tmp_dir || exit 2
for patch_name in $(cat "$QUILT_PATCHES/series" | grep -v '^#'); do
	echo $patch_name
	(cat $QUILT_PATCHES/$patch_name | git-mailinfo "$tmp_msg" "$tmp_patch" > "$tmp_info") || exit 3
	test -s $dotest/patch || {
		echo "Patch is empty.  Was is split wrong?"
		stop_here $this
	}

	# Parse the author information
	export GIT_AUTHOR_NAME=$(sed -ne 's/Author: //p' "$tmp_info")
	export GIT_AUTHOR_EMAIL=$(sed -ne 's/Email: //p' "$tmp_info")
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
	export GIT_AUTHOR_DATE=$(sed -ne 's/Date: //p' "$tmp_info")
	export SUBJECT=$(sed -ne 's/Subject: //p' "$tmp_info")
	if [ -z "$SUBJECT" ] ; then
		SUBJECT=$(echo $patch_name | sed -e 's/.patch$//')
	fi

	if [ -z "$dry_run" ] ; then
		git-apply --index -C1 "$tmp_patch" &&
		tree=$(git-write-tree) &&
		commit=$( (echo "$SUBJECT"; echo; cat "$tmp_msg") | git-commit-tree $tree -p $commit) &&
		git-update-ref -m "quiltimport: $patch_name" HEAD $commit || exit 4
	fi
done
rm -rf $tmp_dir || exit 5
