#!/bin/sh

extract_variable () {
	file="$1"
	directory="$(dirname "$file")"
	shift
	for i in "$@"
	do
		(
			cat "$file"
			cat <<EOF
print_variable:
	@\$(foreach b,\$($1),echo XXX \$(b:\$X=) YYY;)
EOF
		) |
		make -C "$directory" -f - print_variable 2>/dev/null |
		sed -n -e 's/.*XXX \(.*\) YYY.*/\1/p'
	done
}

check_missing_docs () (
	ret=0

	for v in $ALL_COMMANDS $MAN_GUIDES
	do
		case "$v" in
		git-merge-octopus) continue;;
		git-merge-ours) continue;;
		git-merge-recursive) continue;;
		git-merge-resolve) continue;;
		git-merge-subtree) continue;;
		git-fsck-objects) continue;;
		git-init-db) continue;;
		git-remote-*) continue;;
		git-stage) continue;;
		git-legacy-*) continue;;
		git-?*--?* ) continue ;;
		gittutorial-2) continue ;;
		gitweb.conf) continue ;;
		esac

		if ! test -f "$v.adoc"
		then
			echo "no doc: $v"
			ret=1
		fi

		if ! sed -e '1,/^### command list/d' -e '/^#/d' ../command-list.txt |
			grep -q "^$v[ 	]"
		then
			case "$v" in
			git)
				;;
			*)
				echo "no link: $v"
				ret=1
				;;
			esac
		fi
	done

	exit $ret
)

check_extraneous_docs () {
	(
		sed -e '1,/^### command list/d' \
		    -e '/^#/d' \
		    -e '/guide$/d' \
		    -e '/interfaces$/d' \
		    -e 's/[ 	].*//' \
		    -e 's/^/listed /' ../command-list.txt
		make print-man1 |
		grep '\.adoc$' |
		sed -e 's|^|documented |' \
		    -e 's/\.adoc//'
	) | (
		all_commands="$(printf "%s " "$ALL_COMMANDS" "$BUILT_INS" "$EXCLUDED_PROGRAMS" | tr '\n' ' ')"
		ret=0

		while read how cmd
		do
			case " $all_commands " in
			*" $cmd "*) ;;
			*)
				echo "removed but $how: $cmd"
				ret=1;;
			esac
		done

		exit $ret
	)
}

BUILT_INS="$(extract_variable ../Makefile BUILT_INS)"
ALL_COMMANDS="$(extract_variable ../Makefile ALL_COMMANDS)"
EXCLUDED_PROGRAMS="$(extract_variable ../Makefile EXCLUDED_PROGRAMS)"
MAN_GUIDES="$(extract_variable Makefile MAN5_TXT MAN7_TXT | sed -e 's/\.adoc//')"

findings=$(
	if ! check_missing_docs
	then
		ret=1
	fi

	if ! check_extraneous_docs
	then
		ret=1
	fi

	exit $ret
)
ret=$?

printf "%s" "$findings" | sort

exit $ret
