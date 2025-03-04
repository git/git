#!/bin/sh

cat <<\EOF
Git Howto Index
===============

Here is a collection of mailing list postings made by various
people describing how they use Git in their workflow.

EOF

for adoc
do
	title=$(expr "$adoc" : '.*/\(.*\)\.adoc$')
	from=$(sed -ne '
	/^$/q
	/^From:[ 	]/{
		s///
		s/^[ 	]*//
		s/[ 	]*$//
		s/^/by /
		p
	}
	' "$adoc")

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
	}' "$adoc")

	if grep 'Content-type: text/asciidoc' >/dev/null $adoc
	then
		file=$(expr "$adoc" : '\(.*\)\.adoc$').html
	else
		file="$adoc"
	fi

	echo "* link:howto/$(basename "$file")[$title] $from
$abstract

"

done
