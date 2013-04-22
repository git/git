#!/usr/bin/perl
use lib (split(/:/, $ENV{GITPERLLIB}));

use 5.008;
use warnings;
use strict;

use Test::More qw(no_plan);

BEGIN {
	# t9700-perl-git.sh kicks off our testing, so we have to go from
	# there.
	Test::More->builder->current_test(1);
	Test::More->builder->no_ending(1);
}

use Cwd;
use File::Basename;

BEGIN { use_ok('Git') }

# set up
our $abs_repo_dir = cwd();
ok(our $r = Git->repository(Directory => "."), "open repository");

# config
is($r->config("test.string"), "value", "config scalar: string");
is_deeply([$r->config("test.dupstring")], ["value1", "value2"],
	  "config array: string");
is($r->config("test.nonexistent"), undef, "config scalar: nonexistent");
is_deeply([$r->config("test.nonexistent")], [], "config array: nonexistent");
is($r->config_int("test.int"), 2048, "config_int: integer");
is($r->config_int("test.nonexistent"), undef, "config_int: nonexistent");
ok($r->config_bool("test.booltrue"), "config_bool: true");
ok(!$r->config_bool("test.boolfalse"), "config_bool: false");
is($r->config_path("test.path"), $r->config("test.pathexpanded"),
   "config_path: ~/foo expansion");
is_deeply([$r->config_path("test.pathmulti")], ["foo", "bar"],
   "config_path: multiple values");
our $ansi_green = "\x1b[32m";
is($r->get_color("color.test.slot1", "red"), $ansi_green, "get_color");
# Cannot test $r->get_colorbool("color.foo")) because we do not
# control whether our STDOUT is a terminal.

# Failure cases for config:
# Save and restore STDERR; we will probably extract this into a
# "dies_ok" method and possibly move the STDERR handling to Git.pm.
open our $tmpstderr, ">&STDERR" or die "cannot save STDERR";
open STDERR, ">", "/dev/null" or die "cannot redirect STDERR to /dev/null";
is($r->config("test.dupstring"), "value2", "config: multivar");
eval { $r->config_bool("test.boolother") };
ok($@, "config_bool: non-boolean values fail");
open STDERR, ">&", $tmpstderr or die "cannot restore STDERR";

# ident
like($r->ident("aUthor"), qr/^A U Thor <author\@example.com> [0-9]+ \+0000$/,
     "ident scalar: author (type)");
like($r->ident("cOmmitter"), qr/^C O Mitter <committer\@example.com> [0-9]+ \+0000$/,
     "ident scalar: committer (type)");
is($r->ident("invalid"), "invalid", "ident scalar: invalid ident string (no parsing)");
my ($name, $email, $time_tz) = $r->ident('author');
is_deeply([$name, $email], ["A U Thor", "author\@example.com"],
	 "ident array: author");
like($time_tz, qr/[0-9]+ \+0000/, "ident array: author");
is_deeply([$r->ident("Name <email> 123 +0000")], ["Name", "email", "123 +0000"],
	  "ident array: ident string");
is_deeply([$r->ident("invalid")], [], "ident array: invalid ident string");

# ident_person
is($r->ident_person("aUthor"), "A U Thor <author\@example.com>",
   "ident_person: author (type)");
is($r->ident_person("Name <email> 123 +0000"), "Name <email>",
   "ident_person: ident string");
is($r->ident_person("Name", "email", "123 +0000"), "Name <email>",
   "ident_person: array");

# objects and hashes
ok(our $file1hash = $r->command_oneline('rev-parse', "HEAD:file1"), "(get file hash)");
my $tmpfile = "file.tmp";
open TEMPFILE, "+>$tmpfile" or die "Can't open $tmpfile: $!";
is($r->cat_blob($file1hash, \*TEMPFILE), 15, "cat_blob: size");
our $blobcontents;
{ local $/; seek TEMPFILE, 0, 0; $blobcontents = <TEMPFILE>; }
is($blobcontents, "changed file 1\n", "cat_blob: data");
close TEMPFILE or die "Failed writing to $tmpfile: $!";
is(Git::hash_object("blob", $tmpfile), $file1hash, "hash_object: roundtrip");
open TEMPFILE, ">$tmpfile" or die "Can't open $tmpfile: $!";
print TEMPFILE my $test_text = "test blob, to be inserted\n";
close TEMPFILE or die "Failed writing to $tmpfile: $!";
like(our $newhash = $r->hash_and_insert_object($tmpfile), qr/[0-9a-fA-F]{40}/,
     "hash_and_insert_object: returns hash");
open TEMPFILE, "+>$tmpfile" or die "Can't open $tmpfile: $!";
is($r->cat_blob($newhash, \*TEMPFILE), length $test_text, "cat_blob: roundtrip size");
{ local $/; seek TEMPFILE, 0, 0; $blobcontents = <TEMPFILE>; }
is($blobcontents, $test_text, "cat_blob: roundtrip data");
close TEMPFILE;
unlink $tmpfile;

# paths
is($r->repo_path, $abs_repo_dir . "/.git", "repo_path");
is($r->wc_path, $abs_repo_dir . "/", "wc_path");
is($r->wc_subdir, "", "wc_subdir initial");
$r->wc_chdir("directory1");
is($r->wc_subdir, "directory1", "wc_subdir after wc_chdir");
is($r->config("test.string"), "value", "config after wc_chdir");

# Object generation in sub directory
chdir("directory2");
my $r2 = Git->repository();
is($r2->repo_path, $abs_repo_dir . "/.git", "repo_path (2)");
is($r2->wc_path, $abs_repo_dir . "/", "wc_path (2)");
is($r2->wc_subdir, "directory2/", "wc_subdir initial (2)");

# commands in sub directory
my $last_commit = $r2->command_oneline(qw(rev-parse --verify HEAD));
like($last_commit, qr/^[0-9a-fA-F]{40}$/, 'rev-parse returned hash');
my $dir_commit = $r2->command_oneline('log', '-n1', '--pretty=format:%H', '.');
isnt($last_commit, $dir_commit, 'log . does not show last commit');

# commands outside working tree
chdir($abs_repo_dir . '/..');
my $r3 = Git->repository(Directory => $abs_repo_dir);
my $tmpfile3 = "$abs_repo_dir/file3.tmp";
open TEMPFILE3, "+>$tmpfile3" or die "Can't open $tmpfile3: $!";
is($r3->cat_blob($file1hash, \*TEMPFILE3), 15, "cat_blob(outside): size");
close TEMPFILE3;
unlink $tmpfile3;
chdir($abs_repo_dir);

printf "1..%d\n", Test::More->builder->current_test;

my $is_passing = eval { Test::More->is_passing };
exit($is_passing ? 0 : 1) unless $@ =~ /Can't locate object method/;
