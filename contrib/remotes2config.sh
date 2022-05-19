#!/bin/sh

# Use this tool to rewrite your .but/remotes/ files into the config.

. but-sh-setup

if [ -d "$BUT_DIR"/remotes ]; then
	echo "Rewriting $BUT_DIR/remotes" >&2
	error=0
	# rewrite into config
	{
		cd "$BUT_DIR"/remotes
		ls | while read f; do
			name=$(printf "$f" | tr -c "A-Za-z0-9-" ".")
			sed -n \
			-e "s/^URL:[ 	]*\(.*\)$/remote.$name.url \1 ./p" \
			-e "s/^Pull:[ 	]*\(.*\)$/remote.$name.fetch \1 ^$ /p" \
			-e "s/^Push:[ 	]*\(.*\)$/remote.$name.push \1 ^$ /p" \
			< "$f"
		done
		echo done
	} | while read key value regex; do
		case $key in
		done)
			if [ $error = 0 ]; then
				mv "$BUT_DIR"/remotes "$BUT_DIR"/remotes.old
			fi ;;
		*)
			echo "but config $key "$value" $regex"
			but config $key "$value" $regex || error=1 ;;
		esac
	done
fi
