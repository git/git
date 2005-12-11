#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

USAGE=''
SUBDIRECTORY_OK='Yes'

. git-sh-setup

if [ "$#" != "0" ]
then
  usage
fi

report () {
  header="#
# $1:
#   ($2)
#
"
  trailer=""
  while read status name newname
  do
    printf '%s' "$header"
    header=""
    trailer="#
"
    case "$status" in
    M ) echo "#	modified: $name";;
    D*) echo "#	deleted:  $name";;
    T ) echo "#	typechange: $name";;
    C*) echo "#	copied: $name -> $newname";;
    R*) echo "#	renamed: $name -> $newname";;
    A*) echo "#	new file: $name";;
    U ) echo "#	unmerged: $name";;
    esac
  done
  printf '%s' "$trailer"
  [ "$header" ]
}

branch=$(GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD)
case "$branch" in
refs/heads/master) ;;
*)	echo "# On branch $branch" ;;
esac

git-update-index -q --unmerged --refresh || exit

if GIT_DIR="$GIT_DIR" git-rev-parse --verify HEAD >/dev/null 2>&1
then
	git-diff-index -M --cached --name-status --diff-filter=MDTCRA HEAD |
	sed -e '
		s/\\/\\\\/g
		s/ /\\ /g
	' |
	report "Updated but not checked in" "will commit"

	committable="$?"
else
	echo '#
# Initial commit
#'
	git-ls-files |
	sed -e '
		s/\\/\\\\/g
		s/ /\\ /g
		s/^/A /
	' |
	report "Updated but not checked in" "will commit"

	committable="$?"
fi

git-diff-files  --name-status |
sed -e '
	s/\\/\\\\/g
	s/ /\\ /g
' |
report "Changed but not updated" "use git-update-index to mark for commit"


if test -f "$GIT_DIR/info/exclude"
then
    git-ls-files -z --others \
	--exclude-from="$GIT_DIR/info/exclude" \
        --exclude-per-directory=.gitignore
else
    git-ls-files -z --others \
        --exclude-per-directory=.gitignore
fi |
perl -e '$/ = "\0";
	my $shown = 0;
	while (<>) {
		chomp;
		s|\\|\\\\|g;
		s|\t|\\t|g;
		s|\n|\\n|g;
		s/^/#	/;
		if (!$shown) {
			print "#\n# Untracked files:\n";
			print "#   (use \"git add\" to add to commit)\n#\n";
			$shown = 1;
		}
		print "$_\n";
	}
'

case "$committable" in
0)
	echo "nothing to commit"
	exit 1
esac
exit 0
