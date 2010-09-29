#!/bin/sh

if tty -s
then
	echo "Run 'help' for help, or 'exit' to leave.  Available commands:"
else
	echo "Run 'help' for help.  Available commands:"
fi

cd "$(dirname "$0")"

for cmd in *
do
	case "$cmd" in
	help) ;;
	*) [ -f "$cmd" ] && [ -x "$cmd" ] && echo "$cmd" ;;
	esac
done
