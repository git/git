#!/bin/sh

print_if_bare_repo='
	if "$(git --git-dir="$1" rev-parse --is-bare-repository)" = true
	then
		printf "%s\n" "${1#./}"
	fi
'

find -type d -name "*.git" -exec sh -c "$print_if_bare_repo" -- \{} \; -prune 2>/dev/null
