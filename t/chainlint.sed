#------------------------------------------------------------------------------
# Detect broken &&-chains in tests.
#
# At present, only &&-chains in subshells are examined by this linter;
# top-level &&-chains are instead checked directly by the test framework. Like
# the top-level &&-chain linter, the subshell linter (intentionally) does not
# check &&-chains within {...} blocks.
#
# Checking for &&-chain breakage is done line-by-line by pure textual
# inspection.
#
# Incomplete lines (those ending with "\") are stitched together with following
# lines to simplify processing, particularly of "one-liner" statements.
# Top-level here-docs are swallowed to avoid false positives within the
# here-doc body, although the statement to which the here-doc is attached is
# retained.
#
# Heuristics are used to detect end-of-subshell when the closing ")" is cuddled
# with the final subshell statement on the same line:
#
#    (cd foo &&
#        bar)
#
# in order to avoid misinterpreting the ")" in constructs such as "x=$(...)"
# and "case $x in *)" as ending the subshell.
#
# Lines missing a final "&&" are flagged with "?!AMP?!", as are lines which
# chain commands with ";" internally rather than "&&". A line may be flagged
# for both violations.
#
# Detection of a missing &&-link in a multi-line subshell is complicated by the
# fact that the last statement before the closing ")" must not end with "&&".
# Since processing is line-by-line, it is not known whether a missing "&&" is
# legitimate or not until the _next_ line is seen. To accommodate this, within
# multi-line subshells, each line is stored in sed's "hold" area until after
# the next line is seen and processed. If the next line is a stand-alone ")",
# then a missing "&&" on the previous line is legitimate; otherwise a missing
# "&&" is a break in the &&-chain.
#
#    (
#         cd foo &&
#         bar
#    )
#
# In practical terms, when "bar" is encountered, it is flagged with "?!AMP?!",
# but when the stand-alone ")" line is seen which closes the subshell, the
# "?!AMP?!" violation is removed from the "bar" line (retrieved from the "hold"
# area) since the final statement of a subshell must not end with "&&". The
# final line of a subshell may still break the &&-chain by using ";" internally
# to chain commands together rather than "&&", but an internal "?!AMP?!" is
# never removed from a line even though a line-ending "?!AMP?!" might be.
#
# Care is taken to recognize the last _statement_ of a multi-line subshell, not
# necessarily the last textual _line_ within the subshell, since &&-chaining
# applies to statements, not to lines. Consequently, blank lines, comment
# lines, and here-docs are swallowed (but not the command to which the here-doc
# is attached), leaving the last statement in the "hold" area, not the last
# line, thus simplifying &&-link checking.
#
# The final statement before "done" in for- and while-loops, and before "elif",
# "else", and "fi" in if-then-else likewise must not end with "&&", thus
# receives similar treatment.
#
# Swallowing here-docs with arbitrary tags requires a bit of finesse. When a
# line such as "cat <<EOF" is seen, the here-doc tag is copied to the front of
# the line enclosed in angle brackets as a sentinel, giving "<EOF>cat <<EOF".
# As each subsequent line is read, it is appended to the target line and a
# (whitespace-loose) back-reference match /^<(.*)>\n\1$/ is attempted to see if
# the content inside "<...>" matches the entirety of the newly-read line. For
# instance, if the next line read is "some data", when concatenated with the
# target line, it becomes "<EOF>cat <<EOF\nsome data", and a match is attempted
# to see if "EOF" matches "some data". Since it doesn't, the next line is
# attempted. When a line consisting of only "EOF" (and possible whitespace) is
# encountered, it is appended to the target line giving "<EOF>cat <<EOF\nEOF",
# in which case the "EOF" inside "<...>" does match the text following the
# newline, thus the closing here-doc tag has been found. The closing tag line
# and the "<...>" prefix on the target line are then discarded, leaving just
# the target line "cat <<EOF".
#------------------------------------------------------------------------------

# incomplete line -- slurp up next line
:squash
/\\$/ {
	N
	s/\\\n//
	bsquash
}

# here-doc -- swallow it to avoid false hits within its body (but keep the
# command to which it was attached)
/<<-*[ 	]*[\\'"]*[A-Za-z0-9_]/ {
	/"[^"]*<<[^"]*"/bnotdoc
	s/^\(.*<<-*[ 	]*\)[\\'"]*\([A-Za-z0-9_][A-Za-z0-9_]*\)['"]*/<\2>\1\2/
	:hered
	N
	/^<\([^>]*\)>.*\n[ 	]*\1[ 	]*$/!{
		s/\n.*$//
		bhered
	}
	s/^<[^>]*>//
	s/\n.*$//
}
:notdoc

# one-liner "(...) &&"
/^[ 	]*!*[ 	]*(..*)[ 	]*&&[ 	]*$/boneline

# same as above but without trailing "&&"
/^[ 	]*!*[ 	]*(..*)[ 	]*$/boneline

# one-liner "(...) >x" (or "2>x" or "<x" or "|x" or "&"
/^[ 	]*!*[ 	]*(..*)[ 	]*[0-9]*[<>|&]/boneline

# multi-line "(...\n...)"
/^[ 	]*(/bsubsh

# innocuous line -- print it and advance to next line
b

# found one-liner "(...)" -- mark suspect if it uses ";" internally rather than
# "&&" (but not ";" in a string)
:oneline
/;/{
	/"[^"]*;[^"]*"/!s/;/; ?!AMP?!/
}
b

:subsh
# bare "(" line? -- stash for later printing
/^[ 	]*([	]*$/ {
	h
	bnextln
}
# "(..." line -- "(" opening subshell cuddled with command; temporarily replace
# "(" with sentinel "^" and process the line as if "(" had been seen solo on
# the preceding line; this temporary replacement prevents several rules from
# accidentally thinking "(" introduces a nested subshell; "^" is changed back
# to "(" at output time
x
s/.*//
x
s/(/^/
bslurp

:nextln
N
s/.*\n//

:slurp
# incomplete line "...\"
/\\$/bicmplte
# multi-line quoted string "...\n..."?
/"/bdqstr
# multi-line quoted string '...\n...'? (but not contraction in string "it's")
/'/{
	/"[^'"]*'[^'"]*"/!bsqstr
}
:folded
# here-doc -- swallow it (but not "<<" in a string)
/<<-*[ 	]*[\\'"]*[A-Za-z0-9_]/{
	/"[^"]*<<[^"]*"/!bheredoc
}
# comment or empty line -- discard since final non-comment, non-empty line
# before closing ")", "done", "elsif", "else", or "fi" will need to be
# re-visited to drop "suspect" marking since final line of those constructs
# legitimately lacks "&&", so "suspect" mark must be removed
/^[ 	]*#/bnextln
/^[ 	]*$/bnextln
# in-line comment -- strip it (but not "#" in a string, Bash ${#...} array
# length, or Perforce "//depot/path#42" revision in filespec)
/[ 	]#/{
	/"[^"]*#[^"]*"/!s/[ 	]#.*$//
}
# one-liner "case ... esac"
/^[ 	^]*case[ 	]*..*esac/bchkchn
# multi-line "case ... esac"
/^[ 	^]*case[ 	]..*[ 	]in/bcase
# multi-line "for ... done" or "while ... done"
/^[ 	^]*for[ 	]..*[ 	]in/bcont
/^[ 	^]*while[ 	]/bcont
/^[ 	]*do[ 	]/bcont
/^[ 	]*do[ 	]*$/bcont
/;[ 	]*do/bcont
/^[ 	]*done[ 	]*&&[ 	]*$/bdone
/^[ 	]*done[ 	]*$/bdone
/^[ 	]*done[ 	]*[<>|]/bdone
/^[ 	]*done[ 	]*)/bdone
/||[ 	]*exit[ 	]/bcont
/||[ 	]*exit[ 	]*$/bcont
# multi-line "if...elsif...else...fi"
/^[ 	^]*if[ 	]/bcont
/^[ 	]*then[ 	]/bcont
/^[ 	]*then[ 	]*$/bcont
/;[ 	]*then/bcont
/^[ 	]*elif[ 	]/belse
/^[ 	]*elif[ 	]*$/belse
/^[ 	]*else[ 	]/belse
/^[ 	]*else[ 	]*$/belse
/^[ 	]*fi[ 	]*&&[ 	]*$/bdone
/^[ 	]*fi[ 	]*$/bdone
/^[ 	]*fi[ 	]*[<>|]/bdone
/^[ 	]*fi[ 	]*)/bdone
# nested one-liner "(...) &&"
/^[ 	^]*(.*)[ 	]*&&[ 	]*$/bchkchn
# nested one-liner "(...)"
/^[ 	^]*(.*)[ 	]*$/bchkchn
# nested one-liner "(...) >x" (or "2>x" or "<x" or "|x")
/^[ 	^]*(.*)[ 	]*[0-9]*[<>|]/bchkchn
# nested multi-line "(...\n...)"
/^[ 	^]*(/bnest
# multi-line "{...\n...}"
/^[ 	^]*{/bblock
# closing ")" on own line -- exit subshell
/^[ 	]*)/bclssolo
# "$((...))" -- arithmetic expansion; not closing ")"
/\$(([^)][^)]*))[^)]*$/bchkchn
# "$(...)" -- command substitution; not closing ")"
/\$([^)][^)]*)[^)]*$/bchkchn
# multi-line "$(...\n...)" -- command substitution; treat as nested subshell
/\$([^)]*$/bnest
# "=(...)" -- Bash array assignment; not closing ")"
/=(/bchkchn
# closing "...) &&"
/)[ 	]*&&[ 	]*$/bclose
# closing "...)"
/)[ 	]*$/bclose
# closing "...) >x" (or "2>x" or "<x" or "|x")
/)[ 	]*[<>|]/bclose
:chkchn
# mark suspect if line uses ";" internally rather than "&&" (but not ";" in a
# string and not ";;" in one-liner "case...esac")
/;/{
	/;;/!{
		/"[^"]*;[^"]*"/!s/;/; ?!AMP?!/
	}
}
# line ends with pipe "...|" -- valid; not missing "&&"
/|[ 	]*$/bcont
# missing end-of-line "&&" -- mark suspect
/&&[ 	]*$/!s/$/ ?!AMP?!/
:cont
# retrieve and print previous line
x
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
n
bslurp

# found incomplete line "...\" -- slurp up next line
:icmplte
N
s/\\\n//
bslurp

# check for multi-line double-quoted string "...\n..." -- fold to one line
:dqstr
# remove all quote pairs
s/"\([^"]*\)"/@!\1@!/g
# done if no dangling quote
/"/!bdqdone
# otherwise, slurp next line and try again
N
s/\n//
bdqstr
:dqdone
s/@!/"/g
bfolded

# check for multi-line single-quoted string '...\n...' -- fold to one line
:sqstr
# remove all quote pairs
s/'\([^']*\)'/@!\1@!/g
# done if no dangling quote
/'/!bsqdone
# otherwise, slurp next line and try again
N
s/\n//
bsqstr
:sqdone
s/@!/'/g
bfolded

# found here-doc -- swallow it to avoid false hits within its body (but keep
# the command to which it was attached)
:heredoc
s/^\(.*\)<<\(-*[ 	]*\)[\\'"]*\([A-Za-z0-9_][A-Za-z0-9_]*\)['"]*/<\3>\1?!HERE?!\2\3/
:hdocsub
N
/^<\([^>]*\)>.*\n[ 	]*\1[ 	]*$/!{
	s/\n.*$//
	bhdocsub
}
s/^<[^>]*>//
s/\n.*$//
bfolded

# found "case ... in" -- pass through untouched
:case
x
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
n
:cascom
/^[ 	]*#/{
	N
	s/.*\n//
	bcascom
}
/^[ 	]*esac/bslurp
bcase

# found "else" or "elif" -- drop "suspect" from final line before "else" since
# that line legitimately lacks "&&"
:else
x
s/\( ?!AMP?!\)* ?!AMP?!$//
x
bcont

# found "done" closing for-loop or while-loop, or "fi" closing if-then -- drop
# "suspect" from final contained line since that line legitimately lacks "&&"
:done
x
s/\( ?!AMP?!\)* ?!AMP?!$//
x
# is 'done' or 'fi' cuddled with ")" to close subshell?
/done.*)/bclose
/fi.*)/bclose
bchkchn

# found nested multi-line "(...\n...)" -- pass through untouched
:nest
x
:nstslrp
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
n
:nstcom
# comment -- not closing ")" if in comment
/^[ 	]*#/{
	N
	s/.*\n//
	bnstcom
}
# closing ")" on own line -- stop nested slurp
/^[ 	]*)/bnstcl
# "$((...))" -- arithmetic expansion; not closing ")"
/\$(([^)][^)]*))[^)]*$/bnstcnt
# "$(...)" -- command substitution; not closing ")"
/\$([^)][^)]*)[^)]*$/bnstcnt
# closing "...)" -- stop nested slurp
/)/bnstcl
:nstcnt
x
bnstslrp
:nstcl
# is it "))" which closes nested and parent subshells?
/)[ 	]*)/bslurp
bchkchn

# found multi-line "{...\n...}" block -- pass through untouched
:block
x
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
n
:blkcom
/^[ 	]*#/{
	N
	s/.*\n//
	bblkcom
}
# closing "}" -- stop block slurp
/}/bchkchn
bblock

# found closing ")" on own line -- drop "suspect" from final line of subshell
# since that line legitimately lacks "&&" and exit subshell loop
:clssolo
x
s/\( ?!AMP?!\)* ?!AMP?!$//
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
p
x
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
b

# found closing "...)" -- exit subshell loop
:close
x
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
p
x
s/^\([ 	]*\)^/\1(/
s/?!HERE?!/<</g
b
