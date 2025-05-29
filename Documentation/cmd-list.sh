#!/bin/sh

set -e

format_one () {
	source_dir="$1"
	command="$2"
	attributes="$3"

	path="$source_dir/Documentation/$command.adoc"
	if ! test -f "$path"
	then
		echo >&2 "No such file $path"
		exit 1
	fi

	state=0
	while read line
	do
		case "$state" in
		0)
			case "$line" in
			git*\(*\)|scalar*\(*\))
				mansection="${line##*\(}"
				mansection="${mansection%\)}"
				;;
			NAME)
				state=1;;
			esac
			;;
		1)
			if test "$line" = "----"
			then
				state=2
			fi
			;;
		2)
			description="$line"
			break
			;;
		esac
	done <"$path"

	if test -z "$mansection"
	then
		echo "No man section found in $path" >&2
		exit 1
	fi

	if test -z "$description"
	then
		echo >&2 "No description found in $path"
		exit 1
	fi

	case "$description" in
	"$command - "*)
		text="${description#$command - }"

		printf "linkgit:%s[%s]::\n\t" "$command" "$mansection"
		case "$attributes" in
		*" deprecated "*)
			printf "(deprecated) "
			;;
		esac
		printf "$text.\n\n"
		;;
	*)
		echo >&2 "Description does not match $command: $description"
		exit 1
		;;
	esac
}

source_dir="$1"
build_dir="$2"
shift 2

for out
do
	category="${out#cmds-}"
	category="${category%.adoc}"
	path="$build_dir/$out"

	while read command command_category attributes
	do
		case "$command" in
		"#"*)
			continue;;
		esac

		case "$command_category" in
		"$category")
			format_one "$source_dir" "$command" " $attributes ";;
		esac
	done <"$source_dir/command-list.txt" >"$build_dir/$out+"

	if cmp "$build_dir/$out+" "$build_dir/$out" >/dev/null 2>&1
	then
		rm "$build_dir/$out+"
	else
		mv "$build_dir/$out+" "$build_dir/$out"
	fi
done
