# This file isn't used as a test script directly, instead it is
# sourced from t8001-annotate.sh and t8002-blame.sh.

if test_have_prereq MINGW
then
  sanitize_L () {
	echo "$1" | sed 'sX\(^-L\|,\)\^\?/X&\\;*Xg'
  }
else
  sanitize_L () {
	echo "$1"
  }
fi

check_count () {
	head= &&
	file='file' &&
	options= &&
	while :
	do
		case "$1" in
		-h) head="$2"; shift; shift ;;
		-f) file="$2"; shift; shift ;;
		-L*) options="$options $(sanitize_L "$1")"; shift ;;
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

test_expect_success 'blame in a bare repo without starting commit' '
	git clone --bare . bare.git &&
	(
		cd bare.git &&
		check_count A 2
	)
'

test_expect_success 'blame by tag objects' '
	git tag -m "test tag" testTag &&
	git tag -m "test tag #2" testTag2 testTag &&
	check_count -h testTag A 2 &&
	check_count -h testTag2 A 2
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
	git merge branch1
'

test_expect_success 'blame 2 authors + 2 merged-in authors' '
	check_count A 2 B 1 B1 2 B2 1
'

test_expect_success 'blame --first-parent blames merge for branch1' '
	check_count --first-parent A 2 B 1 "A U Thor" 2 B2 1
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

test_expect_success 'blame huge graft' '
	test_when_finished "git checkout branch2" &&
	test_when_finished "rm -f .git/info/grafts" &&
	graft= &&
	for i in 0 1 2
	do
		for j in 0 1 2 3 4 5 6 7 8 9
		do
			git checkout --orphan "$i$j" &&
			printf "%s\n" "$i" "$j" >file &&
			test_tick &&
			GIT_AUTHOR_NAME=$i$j GIT_AUTHOR_EMAIL=$i$j@test.git \
			git commit -a -m "$i$j" &&
			commit=$(git rev-parse --verify HEAD) &&
			graft="$graft$commit "
		done
	done &&
	printf "%s " $graft >.git/info/grafts &&
	check_count -h 00 01 1 10 1
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

test_expect_success 'blame -L -X' '
	test_must_fail $PROG -L-1 file
'

test_expect_success 'blame -L 0' '
	test_must_fail $PROG -L0 file
'

test_expect_success 'blame -L ,0' '
	test_must_fail $PROG -L,0 file
'

test_expect_success 'blame -L ,+0' '
	test_must_fail $PROG -L,+0 file
'

test_expect_success 'blame -L X,+0' '
	test_must_fail $PROG -L1,+0 file
'

test_expect_success 'blame -L X,+1' '
	check_count -L3,+1 B2 1
'

test_expect_success 'blame -L X,+N' '
	check_count -L3,+4 B 1 B1 1 B2 1 D 1
'

test_expect_success 'blame -L ,-0' '
	test_must_fail $PROG -L,-0 file
'

test_expect_success 'blame -L X,-0' '
	test_must_fail $PROG -L1,-0 file
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

# 'file' ends with an incomplete line, so 'wc' reports one fewer lines than
# git-blame sees, hence the last line is actually $(wc...)+1.
test_expect_success 'blame -L X (X == nlines)' '
	n=$(expr $(wc -l <file) + 1) &&
	check_count -L$n C 1
'

test_expect_success 'blame -L X (X == nlines + 1)' '
	n=$(expr $(wc -l <file) + 2) &&
	test_must_fail $PROG -L$n file
'

test_expect_success 'blame -L X (X > nlines)' '
	test_must_fail $PROG -L12345 file
'

test_expect_success 'blame -L ,Y (Y == nlines)' '
	n=$(expr $(wc -l <file) + 1) &&
	check_count -L,$n A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1 E 1
'

test_expect_success 'blame -L ,Y (Y == nlines + 1)' '
	n=$(expr $(wc -l <file) + 2) &&
	check_count -L,$n A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1 E 1
'

test_expect_success 'blame -L ,Y (Y > nlines)' '
	check_count -L,12345 A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1 E 1
'

test_expect_success 'blame -L multiple (disjoint)' '
	check_count -L2,3 -L6,7 A 1 B1 1 B2 1 "A U Thor" 1
'

test_expect_success 'blame -L multiple (disjoint: unordered)' '
	check_count -L6,7 -L2,3 A 1 B1 1 B2 1 "A U Thor" 1
'

test_expect_success 'blame -L multiple (adjacent)' '
	check_count -L2,3 -L4,5 A 1 B 1 B2 1 D 1
'

test_expect_success 'blame -L multiple (adjacent: unordered)' '
	check_count -L4,5 -L2,3 A 1 B 1 B2 1 D 1
'

test_expect_success 'blame -L multiple (overlapping)' '
	check_count -L2,4 -L3,5 A 1 B 1 B2 1 D 1
'

test_expect_success 'blame -L multiple (overlapping: unordered)' '
	check_count -L3,5 -L2,4 A 1 B 1 B2 1 D 1
'

test_expect_success 'blame -L multiple (superset/subset)' '
	check_count -L2,8 -L3,5 A 1 B 1 B1 1 B2 1 C 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L multiple (superset/subset: unordered)' '
	check_count -L3,5 -L2,8 A 1 B 1 B1 1 B2 1 C 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/ (relative)' '
	check_count -L3,3 -L/fox/ B1 1 B2 1 C 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/ (relative: no preceding range)' '
	check_count -L/dog/ A 1 B 1 B1 1 B2 1 C 1 D 1 "A U Thor" 1
'

test_expect_success 'blame -L /RE/ (relative: adjacent)' '
	check_count -L1,1 -L/dog/,+1 A 1 E 1
'

test_expect_success 'blame -L /RE/ (relative: not found)' '
	test_must_fail $PROG -L4,4 -L/dog/ file
'

test_expect_success 'blame -L /RE/ (relative: end-of-file)' '
	test_must_fail $PROG -L, -L/$/ file
'

test_expect_success 'blame -L ^/RE/ (absolute)' '
	check_count -L3,3 -L^/dog/,+2 A 1 B2 1
'

test_expect_success 'blame -L ^/RE/ (absolute: no preceding range)' '
	check_count -L^/dog/,+2 A 1 B2 1
'

test_expect_success 'blame -L ^/RE/ (absolute: not found)' '
	test_must_fail $PROG -L4,4 -L^/tambourine/ file
'

test_expect_success 'blame -L ^/RE/ (absolute: end-of-file)' '
	n=$(expr $(wc -l <file) + 1) &&
	check_count -L$n -L^/$/,+2 A 1 C 1 E 1
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
	sed -e "/}/ {x; s/$/Qputs(\"goodbye\");/; G;}" <hello.orig |
	tr Q "\\t" >hello.c &&
	GIT_AUTHOR_NAME="G" GIT_AUTHOR_EMAIL="G@test.git" \
	git commit -a -m "goodbye" &&

	mv hello.c hello.orig &&
	echo "#include <stdio.h>" >hello.c &&
	cat hello.orig >>hello.c &&
	tr Q "\\t" >>hello.c <<-\EOF &&
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

test_expect_success 'blame -L :RE (relative)' '
	check_count -f hello.c -L3,3 -L:ma.. F 1 H 4
'

test_expect_success 'blame -L :RE (relative: no preceding range)' '
	check_count -f hello.c -L:ma.. F 4 G 1
'

test_expect_success 'blame -L :RE (relative: not found)' '
	test_must_fail $PROG -L3,3 -L:tambourine hello.c
'

test_expect_success 'blame -L :RE (relative: end-of-file)' '
	test_must_fail $PROG -L, -L:main hello.c
'

test_expect_success 'blame -L ^:RE (absolute)' '
	check_count -f hello.c -L3,3 -L^:ma.. F 4 G 1
'

test_expect_success 'blame -L ^:RE (absolute: no preceding range)' '
	check_count -f hello.c -L^:ma.. F 4 G 1
'

test_expect_success 'blame -L ^:RE (absolute: not found)' '
	test_must_fail $PROG -L4,4 -L^:tambourine hello.c
'

test_expect_success 'blame -L ^:RE (absolute: end-of-file)' '
	n=$(printf "%d" $(wc -l <hello.c)) &&
	check_count -f hello.c -L$n -L^:ma.. F 4 G 1 H 1
'

test_expect_success 'setup incremental' '
	(
	GIT_AUTHOR_NAME=I &&
	export GIT_AUTHOR_NAME &&
	GIT_AUTHOR_EMAIL=I@test.git &&
	export GIT_AUTHOR_EMAIL &&
	>incremental &&
	git add incremental &&
	git commit -m "step 0" &&
	printf "partial" >>incremental &&
	git commit -a -m "step 0.5" &&
	echo >>incremental &&
	git commit -a -m "step 1"
	)
'

test_expect_success 'blame empty' '
	check_count -h HEAD^^ -f incremental
'

test_expect_success 'blame -L 0 empty' '
	test_must_fail $PROG -L0 incremental HEAD^^
'

test_expect_success 'blame -L 1 empty' '
	test_must_fail $PROG -L1 incremental HEAD^^
'

test_expect_success 'blame -L 2 empty' '
	test_must_fail $PROG -L2 incremental HEAD^^
'

test_expect_success 'blame half' '
	check_count -h HEAD^ -f incremental I 1
'

test_expect_success 'blame -L 0 half' '
	test_must_fail $PROG -L0 incremental HEAD^
'

test_expect_success 'blame -L 1 half' '
	check_count -h HEAD^ -f incremental -L1 I 1
'

test_expect_success 'blame -L 2 half' '
	test_must_fail $PROG -L2 incremental HEAD^
'

test_expect_success 'blame -L 3 half' '
	test_must_fail $PROG -L3 incremental HEAD^
'

test_expect_success 'blame full' '
	check_count -f incremental I 1
'

test_expect_success 'blame -L 0 full' '
	test_must_fail $PROG -L0 incremental
'

test_expect_success 'blame -L 1 full' '
	check_count -f incremental -L1 I 1
'

test_expect_success 'blame -L 2 full' '
	test_must_fail $PROG -L2 incremental
'

test_expect_success 'blame -L 3 full' '
	test_must_fail $PROG -L3 incremental
'

test_expect_success 'blame -L' '
	test_must_fail $PROG -L file
'

test_expect_success 'blame -L X,+' '
	test_must_fail $PROG -L1,+ file
'

test_expect_success 'blame -L X,-' '
	test_must_fail $PROG -L1,- file
'

test_expect_success 'blame -L X (non-numeric X)' '
	test_must_fail $PROG -LX file
'

test_expect_success 'blame -L X,Y (non-numeric Y)' '
	test_must_fail $PROG -L1,Y file
'

test_expect_success 'blame -L X,+N (non-numeric N)' '
	test_must_fail $PROG -L1,+N file
'

test_expect_success 'blame -L X,-N (non-numeric N)' '
	test_must_fail $PROG -L1,-N file
'

test_expect_success 'blame -L ,^/RE/' '
	test_must_fail $PROG -L1,^/99/ file
'
