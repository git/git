#!/bin/sh

# Use this tool to rewrite your .git/remotes/ files into the config.

. git-sh-setup

if [ -d "$GIT_DIR"/remotes ]; then
	echo "Rewriting $GIT_DIR/remotes" >&2
	error=0
	# rewrite into config
	{
		cd "$GIT_DIR"/remotes
		ls | while read f; do
			name=$(printf "$f" | tr -c "A-Za-z0-9" ".")
			sed -n \
			-e "s/^URL: \(.*\)$/remote.$name.url \1 ./p" \
			-e "s/^Pull: \(.*\)$/remote.$name.fetch \1 ^$ /p" \
			-e "s/^Push: \(.*\)$/remote.$name.push \1 ^$ /p" \
			< "$f"
		done
		echo done
	} | while read key value regex; do
		case $key in
		done)
			if [ $error = 0 ]; then
				mv "$GIT_DIR"/remotes "$GIT_DIR"/remotes.old
			fi ;;
		*)
			echo "git-config $key "$value" $regex"
			git-config $key "$value" $regex || error=1 ;;
		esac
	done
fi
