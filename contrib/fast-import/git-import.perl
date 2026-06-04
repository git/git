#!/usr/bin/perl
#
# Performs an initial import of a directory. This is the equivalent
# of doing 'git init; git add .; git commit'. It's a little slower,
# but is meant to be a simple fast-import example.

use strict;
use File::Find;

my $USAGE = 'usage: git-import branch import-message';
my $branch = shift or die "$USAGE\n";
my $message = shift or die "$USAGE\n";

chomp(my $username = `git config user.name`);
chomp(my $email = `git config user.email`);
die 'You need to set user name and email'
  unless $username && $email;

system('git init');
open(my $fi, '|-', qw(git fast-import --date-format=now))
  or die "unable to spawn fast-import: $!";

print $fi <<EOF;
commit refs/heads/$branch
committer $username <$email> now
data <<MSGEOF
$message
MSGEOF

EOF

find(
  sub {
    if($File::Find::name eq './.git') {
      $File::Find::prune = 1;
      return;
    }
    return unless -f $_;

    my $fn = $File::Find::name;
    $fn =~ s#^.\/##;

    open(my $in, '<', $_)
      or die "unable to open $fn: $!";
    my @st = stat($in)
      or die "unable to stat $fn: $!";
    my $len = $st[7];

    print $fi "M 644 inline $fn\n";
    print $fi "data $len\n";
    while($len > 0) {
      my $r = read($in, my $buf, $len < 4096 ? $len : 4096);
      defined($r) or die "read error from $fn: $!";
      $r > 0 or die "premature EOF from $fn: $!";
      print $fi $buf;
      $len -= $r;
    }
    print $fi "\n";

  }, '.'
);

close($fi);
exit $?;
