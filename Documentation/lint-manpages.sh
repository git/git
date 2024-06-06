#!/bin/sh

extract_variable () {
	(
		cat ../Makefile
		cat <<EOF
print_variable:
	@\$(foreach b,\$($1),echo XXX \$(b:\$X=) YYY;)
EOF
	) |
	make -C .. -f - print_variable 2>/dev/null |
	sed -n -e 's/.*XXX \(.*\) YYY.*/\1/p'
}

check_missing_docs () {
	for v in $ALL_COMMANDS
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
		esac

		if ! test -f "$v.txt"
		then
			echo "no doc: $v"
		fi

		if ! sed -e '1,/^### command list/d' -e '/^#/d' ../command-list.txt |
			grep -q "^$v[ 	]"
		then
			case "$v" in
			git)
				;;
			*)
				echo "no link: $v";;
			esac
		fi
	done
}

check_extraneous_docs () {
	(
		sed -e '1,/^### command list/d' \
		    -e '/^#/d' \
		    -e '/guide$/d' \
		    -e '/interfaces$/d' \
		    -e 's/[ 	].*//' \
		    -e 's/^/listed /' ../command-list.txt
		make print-man1 |
		grep '\.txt$' |
		sed -e 's|^|documented |' \
		    -e 's/\.txt//'
	) | (
		all_commands="$(printf "%s " "$ALL_COMMANDS" "$BUILT_INS" "$EXCLUDED_PROGRAMS" | tr '\n' ' ')"

		while read how cmd
		do
			case " $all_commands " in
			*" $cmd "*) ;;
			*)
				echo "removed but $how: $cmd";;
			esac
		done
	)
}

BUILT_INS="$(extract_variable BUILT_INS)"
ALL_COMMANDS="$(extract_variable ALL_COMMANDS)"
EXCLUDED_PROGRAMS="$(extract_variable EXCLUDED_PROGRAMS)"

{
	check_missing_docs
	check_extraneous_docs
} | sort
