#!/bin/sh

cat <<\EOF
Git Howto Index
===============

Here is a collection of mailing list postings made by various
people describing how they use Git in their workflow.

EOF

for txt
do
	title=$(expr "$txt" : '.*/\(.*\)\.txt$')
	from=$(sed -ne '
	/^$/q
	/^From:[ 	]/{
		s///
		s/^[ 	]*//
		s/[ 	]*$//
		s/^/by /
		p
	}
	' "$txt")

	abstract=$(sed -ne '
	/^Abstract:[ 	]/{
		s/^[^ 	]*//
		x
		s/.*//
		x
		: again
		/^[ 	]/{
			s/^[ 	]*//
			H
			n
			b again
		}
		x
		p
		q
	}' "$txt")

	if grep 'Content-type: text/asciidoc' >/dev/null $txt
	then
		file=$(expr "$txt" : '\(.*\)\.txt$').html
	else
		file="$txt"
	fi

	echo "* link:howto/$(basename "$file")[$title] $from
$abstract

"

done
