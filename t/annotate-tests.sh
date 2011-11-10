# This file isn't used as a test script directly, instead it is
# sourced from t8001-annotate.sh and t8001-blame.sh.

check_count () {
	head=
	case "$1" in -h) head="$2"; shift; shift ;; esac
	echo "$PROG file $head" >&4
	$PROG file $head >.result || return 1
	cat .result | perl -e '
		my %expect = (@ARGV);
		my %count = ();
		while (<STDIN>) {
			if (/^[0-9a-f]+\t\(([^\t]+)\t/) {
				my $author = $1;
				for ($author) { s/^\s*//; s/\s*$//; }
				if (exists $expect{$author}) {
					$count{$author}++;
				}
			}
		}
		my $bad = 0;
		while (my ($author, $count) = each %count) {
			my $ok;
			if ($expect{$author} != $count) {
				$bad = 1;
				$ok = "bad";
			}
			else {
				$ok = "good";
			}
			print STDERR "Author $author (expected $expect{$author}, attributed $count) $ok\n";
		}
		exit($bad);
	' "$@"
}

test_expect_success \
    'prepare reference tree' \
    'echo "1A quick brown fox jumps over the" >file &&
     echo "lazy dog" >>file &&
     git add file
     GIT_AUTHOR_NAME="A" git commit -a -m "Initial."'

test_expect_success \
    'check all lines blamed on A' \
    'check_count A 2'

test_expect_success \
    'Setup new lines blamed on B' \
    'echo "2A quick brown fox jumps over the" >>file &&
     echo "lazy dog" >> file &&
     GIT_AUTHOR_NAME="B" git commit -a -m "Second."'

test_expect_success \
    'Two lines blamed on A, two on B' \
    'check_count A 2 B 2'

test_expect_success \
    'merge-setup part 1' \
    'git checkout -b branch1 master &&
     echo "3A slow green fox jumps into the" >> file &&
     echo "well." >> file &&
     GIT_AUTHOR_NAME="B1" git commit -a -m "Branch1-1"'

test_expect_success \
    'Two lines blamed on A, two on B, two on B1' \
    'check_count A 2 B 2 B1 2'

test_expect_success \
    'merge-setup part 2' \
    'git checkout -b branch2 master &&
     sed -e "s/2A quick brown/4A quick brown lazy dog/" < file > file.new &&
     mv file.new file &&
     GIT_AUTHOR_NAME="B2" git commit -a -m "Branch2-1"'

test_expect_success \
    'Two lines blamed on A, one on B, one on B2' \
    'check_count A 2 B 1 B2 1'

test_expect_success \
    'merge-setup part 3' \
    'git pull . branch1'

test_expect_success \
    'Two lines blamed on A, one on B, two on B1, one on B2' \
    'check_count A 2 B 1 B1 2 B2 1'

test_expect_success \
    'Annotating an old revision works' \
    'check_count -h master A 2 B 2'

test_expect_success \
    'Annotating an old revision works' \
    'check_count -h master^ A 2'

test_expect_success \
    'merge-setup part 4' \
    'echo "evil merge." >>file &&
     git commit -a --amend'

test_expect_success \
    'Two lines blamed on A, one on B, two on B1, one on B2, one on A U Thor' \
    'check_count A 2 B 1 B1 2 B2 1 "A U Thor" 1'

test_expect_success \
    'an incomplete line added' \
    'echo "incomplete" | tr -d "\\012" >>file &&
    GIT_AUTHOR_NAME="C" git commit -a -m "Incomplete"'

test_expect_success \
    'With incomplete lines.' \
    'check_count A 2 B 1 B1 2 B2 1 "A U Thor" 1 C 1'

test_expect_success \
    'some edit' \
    'mv file file.orig &&
    {
	cat file.orig &&
	echo
    } | sed -e "s/^3A/99/" -e "/^1A/d" -e "/^incomplete/d" > file &&
    echo "incomplete" | tr -d "\\012" >>file &&
    GIT_AUTHOR_NAME="D" git commit -a -m "edit"'

test_expect_success \
    'some edit' \
    'check_count A 1 B 1 B1 1 B2 1 "A U Thor" 1 C 1 D 1'
