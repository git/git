#!/usr/bin/perl
use lib (split(/:/, $ENV{GITPERLLIB}));

use 5.006002;
use warnings;
use strict;

use Test::More qw(no_plan);

use Cwd;
use File::Basename;
use File::Temp;

BEGIN { use_ok('Git') }

# set up
our $repo_dir = "trash directory";
our $abs_repo_dir = Cwd->cwd;
die "this must be run by calling the t/t97* shell script(s)\n"
    if basename(Cwd->cwd) ne $repo_dir;
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
our $ansi_green = "\x1b[32m";
is($r->get_color("color.test.slot1", "red"), $ansi_green, "get_color");
# Cannot test $r->get_colorbool("color.foo")) because we do not
# control whether our STDOUT is a terminal.

# Failure cases for config:
# Save and restore STDERR; we will probably extract this into a
# "dies_ok" method and possibly move the STDERR handling to Git.pm.
open our $tmpstderr, ">&", STDERR or die "cannot save STDERR"; close STDERR;
eval { $r->config("test.dupstring") };
ok($@, "config: duplicate entry in scalar context fails");
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
our $tmpfile = File::Temp->new;
is($r->cat_blob($file1hash, $tmpfile), 15, "cat_blob: size");
our $blobcontents;
{ local $/; seek $tmpfile, 0, 0; $blobcontents = <$tmpfile>; }
is($blobcontents, "changed file 1\n", "cat_blob: data");
seek $tmpfile, 0, 0;
is(Git::hash_object("blob", $tmpfile), $file1hash, "hash_object: roundtrip");
$tmpfile = File::Temp->new();
print $tmpfile my $test_text = "test blob, to be inserted\n";
like(our $newhash = $r->hash_and_insert_object($tmpfile), qr/[0-9a-fA-F]{40}/,
     "hash_and_insert_object: returns hash");
$tmpfile = File::Temp->new;
is($r->cat_blob($newhash, $tmpfile), length $test_text, "cat_blob: roundtrip size");
{ local $/; seek $tmpfile, 0, 0; $blobcontents = <$tmpfile>; }
is($blobcontents, $test_text, "cat_blob: roundtrip data");

# paths
is($r->repo_path, "./.git", "repo_path");
is($r->wc_path, $abs_repo_dir . "/", "wc_path");
is($r->wc_subdir, "", "wc_subdir initial");
$r->wc_chdir("directory1");
is($r->wc_subdir, "directory1", "wc_subdir after wc_chdir");
TODO: {
	local $TODO = "commands do not work after wc_chdir";
	# Failure output is active even in non-verbose mode and thus
	# annoying.  Hence we skip these tests as long as they fail.
	todo_skip 'config after wc_chdir', 1;
	is($r->config("color.string"), "value", "config after wc_chdir");
}
