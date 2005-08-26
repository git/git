#!/bin/sh

cat <<\EOF
GIT Howto Index
===============

Here is a collection of mailing list postings made by various
people describing how they use git in their workflow.

EOF

for txt
do
	title=`expr "$txt" : '.*/\(.*\)\.txt$'`
	from=`sed -ne '
	/^$/q
	/^From:[ 	]/{
		s///
		s/^[ 	]*//
		s/[ 	]*$//
		s/^/by /
		p
	}
	' "$txt"`

	abstract=`sed -ne '
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
	}' "$txt"`

	echo "* link:$txt[$title] $from
$abstract

"

done
