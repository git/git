#!/bin/sh

test_description='
The general idea is that we have a single file whose lines come from
multiple other files, and those individual files were modified in the same
commits. That means that we will see the same commit in multiple contexts,
and each one should be attributed to the correct file.

Note that we need to use "blame -C" to find the commit for all lines. We will
not bother testing that the non-C case fails to find it. That is how blame
behaves now, but it is not a property we want to make sure is retained.
'
. ./test-lib.sh

# help avoid typing and reading long strings of similar lines
# in the tests below
generate_expect () {
	while read nr data
	do
		i=0
		while test $i -lt $nr
		do
			echo $data
			i=$((i + 1))
		done
	done
}

test_expect_success 'setup split file case' '
	# use lines long enough to trigger content detection
	test_seq 1000 1010 >one &&
	test_seq 2000 2010 >two &&
	git add one two &&
	test_commit base &&

	sed "6s/^/modified /" <one >one.tmp &&
	mv one.tmp one &&
	sed "6s/^/modified /" <two >two.tmp &&
	mv two.tmp two &&
	git add -u &&
	test_commit modified &&

	cat one two >combined &&
	git add combined &&
	git rm one two &&
	test_commit combined
'

test_expect_success 'setup simulated porcelain' '
	# This just reads porcelain-ish output and tries
	# to output the value of a given field for each line (either by
	# reading the field that accompanies this line, or referencing
	# the information found last time the commit was mentioned).
	cat >read-porcelain.pl <<-\EOF
	my $field = shift;
	while (<>) {
		if (/^[0-9a-f]{40,} /) {
			flush();
			$hash = $&;
		} elsif (/^$field (.*)/) {
			$cache{$hash} = $1;
		}
	}
	flush();

	sub flush {
		return unless defined $hash;
		if (defined $cache{$hash}) {
			print "$cache{$hash}\n";
		} else {
			print "NONE\n";
		}
	}
	EOF
'

for output in porcelain line-porcelain
do
	test_expect_success "generate --$output output" '
		git blame --root -C --$output combined >output
	'

	test_expect_success "$output output finds correct commits" '
		generate_expect >expect <<-\EOF &&
		5 base
		1 modified
		10 base
		1 modified
		5 base
		EOF
		perl read-porcelain.pl summary <output >actual &&
		test_cmp expect actual
	'

	test_expect_success "$output output shows correct filenames" '
		generate_expect >expect <<-\EOF &&
		11 one
		11 two
		EOF
		perl read-porcelain.pl filename <output >actual &&
		test_cmp expect actual
	'

	test_expect_success "$output output shows correct previous pointer" '
		generate_expect >expect <<-EOF &&
		5 NONE
		1 $(git rev-parse modified^) one
		10 NONE
		1 $(git rev-parse modified^) two
		5 NONE
		EOF
		perl read-porcelain.pl previous <output >actual &&
		test_cmp expect actual
	'
done

test_done
