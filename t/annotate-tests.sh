# This file isn't used as a test script directly, instead it is
# sourced from t8001-annotate.sh and t8002-blame.sh.

check_count () {
	head= &&
	file='file' &&
	options= &&
	while :
	do
		case "$1" in
		-h) head="$2"; shift; shift ;;
		-f) file="$2"; shift; shift ;;
		-*) options="$options $1"; shift ;;
		*) break ;;
		esac
	done &&
	echo "$PROG $options $file $head" >&4 &&
	$PROG $options $file $head >actual &&
	perl -e '
		my %expect = (@ARGV);
		my %count = map { $_ => 0 } keys %expect;
		while (<STDIN>) {
			if (/^[0-9a-f]+\t\(([^\t]+)\t/) {
				my $author = $1;
				for ($author) { s/^\s*//; s/\s*$//; }
				$count{$author}++;
			}
		}
		my $bad = 0;
		while (my ($author, $count) = each %count) {
			my $ok;
			my $value = 0;
			$value = $expect{$author} if defined $expect{$author};
			if ($value != $count) {
				$bad = 1;
				$ok = "bad";
			}
			else {
				$ok = "good";
			}
			print STDERR "Author $author (expected $value, attributed $count) $ok\n";
		}
		exit($bad);
	' "$@" <actual
}

test_expect_success 'setup A lines' '
	echo "1A quick brown fox jumps over the" >file &&
	echo "lazy dog" >>file &&
	git add file &&
	GIT_AUTHOR_NAME="A" GIT_AUTHOR_EMAIL="A@test.git" \
	git commit -a -m "Initial."
'

test_expect_success 'blame 1 author' '
	check_count A 2
'

test_expect_success 'setup B lines' '
	echo "2A quick brown fox jumps over the" >>file &&
	echo "lazy dog" >>file &&
	GIT_AUTHOR_NAME="B" GIT_AUTHOR_EMAIL="B@test.git" \
	git commit -a -m "Second."
'

test_expect_success 'blame 2 authors' '
	check_count A 2 B 2
'

test_expect_success 'setup B1 lines (branch1)' '
	git checkout -b branch1 master &&
	echo "3A slow green fox jumps into the" >>file &&
	echo "well." >>file &&
	GIT_AUTHOR_NAME="B1" GIT_AUTHOR_EMAIL="B1@test.git" \
	git commit -a -m "Branch1-1"
'

test_expect_success 'blame 2 authors + 1 branch1 author' '
	check_count A 2 B 2 B1 2
'

test_expect_success 'setup B2 lines (branch2)' '
	git checkout -b branch2 master &&
	sed -e "s/2A quick brown/4A quick brown lazy dog/" <file >file.new &&
	mv file.new file &&
	GIT_AUTHOR_NAME="B2" GIT_AUTHOR_EMAIL="B2@test.git" \
	git commit -a -m "Branch2-1"
'

test_expect_success 'blame 2 authors + 1 branch2 author' '
	check_count A 2 B 1 B2 1
'

test_expect_success 'merge branch1 & branch2' '
	git pull . branch1
'

test_expect_success 'blame 2 authors + 2 merged-in authors' '
	check_count A 2 B 1 B1 2 B2 1
'

test_expect_success 'blame ancestor' '
	check_count -h master A 2 B 2
'

test_expect_success 'blame great-ancestor' '
	check_count -h master^ A 2
'

test_expect_success 'setup evil merge' '
	echo "evil merge." >>file &&
	git commit -a --amend
'

test_expect_success 'blame evil merge' '
	check_count A 2 B 1 B1 2 B2 1 "A U Thor" 1
'

test_expect_success 'setup incomplete line' '
	echo "incomplete" | tr -d "\\012" >>file &&
	GIT_AUTHOR_NAME="C" GIT_AUTHOR_EMAIL="C@test.git" \
	git commit -a -m "Incomplete"
'

test_expect_success 'blame incomplete line' '
	check_count A 2 B 1 B1 2 B2 1 "A U Thor" 1 C 1
'

test_expect_success 'setup edits' '
	mv file file.orig &&
	{
		cat file.orig &&
		echo
	} | sed -e "s/^3A/99/" -e "/^1A/d" -e "/^incomplete/d" >file &&
	echo "incomplete" | tr -d "\\012" >>file &&
	GIT_AUTHOR_NAME="D" GIT_AUTHOR_EMAIL="D@test.git" \
	git commit -a -m "edit"
'

test_expect_success 'blame edits' '
	check_count A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1
'

test_expect_success 'setup obfuscated email' '
	echo "No robots allowed" >file.new &&
	cat file >>file.new &&
	mv file.new file &&
	GIT_AUTHOR_NAME="E" GIT_AUTHOR_EMAIL="E at test dot git" \
	git commit -a -m "norobots"
'

test_expect_success 'blame obfuscated email' '
	check_count A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1 E 1
'

test_expect_success 'blame -L 1 (all)' '
	check_count -L1 A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1 E 1
'

test_expect_success 'blame -L , (all)' '
	check_count -L, A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1 E 1
'

test_expect_success 'blame -L X (X to end)' '
	check_count -L5 B1 1 C 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L X, (X to end)' '
	check_count -L5, B1 1 C 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L ,Y (up to Y)' '
	check_count -L,3 A 1 B2 1 E 1
'

test_expect_success 'blame -L X,X' '
	check_count -L3,3 B2 1
'

test_expect_success 'blame -L X,Y' '
	check_count -L3,6 B 1 B1 1 B2 1 D 1
'

test_expect_success 'blame -L Y,X (undocumented)' '
	check_count -L6,3 B 1 B1 1 B2 1 D 1
'

test_expect_success 'blame -L X,+1' '
	check_count -L3,+1 B2 1
'

test_expect_success 'blame -L X,+N' '
	check_count -L3,+4 B 1 B1 1 B2 1 D 1
'

test_expect_success 'blame -L X,-1' '
	check_count -L3,-1 B2 1
'

test_expect_success 'blame -L X,-N' '
	check_count -L6,-4 B 1 B1 1 B2 1 D 1
'

test_expect_success 'blame -L /RE/ (RE to end)' '
	check_count -L/evil/ C 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/,/RE2/' '
	check_count -L/robot/,/green/ A 1 B 1 B2 1 D 1 E 1
'

test_expect_success 'blame -L X,/RE/' '
	check_count -L5,/evil/ B1 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/,Y' '
	check_count -L/99/,7 B1 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/,+N' '
	check_count -L/99/,+3 B1 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/,-N' '
	check_count -L/99/,-3 B 1 B2 1 D 1
'

test_expect_success 'blame -L X (X > nlines)' '
	test_must_fail $PROG -L12345 file
'

test_expect_success 'blame -L ,Y (Y > nlines)' '
	test_must_fail $PROG -L,12345 file
'

test_expect_success 'setup -L :regex' '
	tr Q "\\t" >hello.c <<-\EOF &&
	int main(int argc, const char *argv[])
	{
	Qputs("hello");
	}
	EOF
	git add hello.c &&
	GIT_AUTHOR_NAME="F" GIT_AUTHOR_EMAIL="F@test.git" \
	git commit -m "hello" &&

	mv hello.c hello.orig &&
	sed -e "/}/i\\
	Qputs(\"goodbye\");" <hello.orig | tr Q "\\t" >hello.c &&
	GIT_AUTHOR_NAME="G" GIT_AUTHOR_EMAIL="G@test.git" \
	git commit -a -m "goodbye" &&

	mv hello.c hello.orig &&
	echo "#include <stdio.h>" >hello.c &&
	cat hello.orig >>hello.c &&
	tr Q "\\t" >>hello.c <<-\EOF
	void mail()
	{
	Qputs("mail");
	}
	EOF
	GIT_AUTHOR_NAME="H" GIT_AUTHOR_EMAIL="H@test.git" \
	git commit -a -m "mail"
'

test_expect_success 'blame -L :literal' '
	check_count -f hello.c -L:main F 4 G 1
'

test_expect_success 'blame -L :regex' '
	check_count -f hello.c "-L:m[a-z][a-z]l" H 4
'

test_expect_success 'blame -L :nomatch' '
	test_must_fail $PROG -L:nomatch hello.c
'

test_expect_success 'blame -L bogus' '
	test_must_fail $PROG -L file &&
	test_must_fail $PROG -L1,+ file &&
	test_must_fail $PROG -L1,- file &&
	test_must_fail $PROG -LX file &&
	test_must_fail $PROG -L1,X file &&
	test_must_fail $PROG -L1,+N file &&
	test_must_fail $PROG -L1,-N file
'
