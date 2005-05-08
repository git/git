#!/usr/bin/perl -w
use strict;

my $cmd;
my $name;

my $author;

while (<STDIN>) {
  if (/^NAME$/ || eof(STDIN)) {
    if ($cmd) {
      print PAGE $author if defined($author);
      print PAGE "Documentation\n--------------\nDocumentation by David Greaves, Junio C Hamano and the git-list <git\@vger.kernel.org>.\n\n";
      print PAGE "GIT\n---\nPart of the link:git.html[git] suite\n\n";

      if ($#ARGV || $ARGV[0] eq "-html") {
	system(qw(asciidoc -b css-embedded -d manpage), "$cmd.txt");
      } elsif ($ARGV[0] eq "-man") {
	system(qw(asciidoc -b docbook -d manpage), "$cmd.txt");
	system(qw(xmlto man), "$cmd.xml") if -e "$cmd.xml";
      }
    }
    exit if eof(STDIN);
    $_=<STDIN>;$_=<STDIN>; # discard underline and get command
    chomp;
    $name = $_;
    ($cmd) = split(' ',$_);
    print "$name\n";
    open(PAGE, "> $cmd.txt") or die;
    print PAGE "$cmd(1)\n==="."="x length($cmd);
    print PAGE "\nv0.1, May 2005\n\nNAME\n----\n$name\n\n";


    $author = "Author\n------\nWritten by Linus Torvalds <torvalds\@osdl.org>\n\n";

    next;
  }
  next unless $cmd;

  $author=undef if /^AUTHOR$/i; # don't use default for commands with an author

  print PAGE $_;

}
