#!/usr/bin/perl

# This file is licensed under the GPL v2, or a later version
# (C) 2005, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke <ch@gierke.de>

use strict;
use warnings;
use CGI qw(:standard :escapeHTML);
use CGI::Carp qw(fatalsToBrowser);

my $cgi = new CGI;
my $gitbin = "/home/kay/bin";
my $gitroot = "/home/kay/public_html";
my $gittmp = "/tmp";
my $myself = $cgi->url(-relative => 1);

my $project = $cgi->param("project") || "";
my $action = $cgi->param("action") || "";
my $hash = $cgi->param("hash") || "";
my $parent = $cgi->param("parent") || "";
my $view_back = $cgi->param("view_back") || 60*60*24*2;
my $projectroot = "$gitroot/$project";
$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/.git/objects";

$hash =~ s/[^0-9a-fA-F]//g;
$parent =~ s/[^0-9a-fA-F]//g;
$project =~ s/[^0-9a-zA-Z\-\._]//g;

sub header {
	print $cgi->header(-type => 'text/html; charset: utf-8');
print <<EOF;
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
	<title>GIT</title>
	<style type="text/css">
		body { font-family: sans-serif; font-size: 12px; margin:25px; }
    div.main { border-width:1px; border-style:solid; border-color:#D9D8D1; }
		div.head1 { font-size:20px; padding:8px; background-color: #D9D8D1; font-weight:bold; }
		div.head2 { padding:8px;  }
		td { padding:8px; margin:0px; font-family: sans-serif; font-size: 12px; }
		td.head1 { background-color: #D9D8D1; font-weight:bold; }
		td.head2 { background-color: #EDECE6; font-family: monospace; font-size:12px; }
		table.log { padding:0px; margin:0px; width:100%; }
		tr { vertical-align:top; }
    a { color:#0000cc; }
    a:hover { color:#880000; }
    a:visited { color:#880000; }
    a:active { color:#880000; }
	</style>
</head>
<body>
EOF
	if ($project ne "") {
		print "<div class=\"main\">\n";
		print "<div class=\"head1\">" . $project . "</div>\n";
		print "<div class=\"head2\">\n";
		print $cgi->a({-href => "$myself?project=$project&action=show_tree"}, "Browse Project") . "<br/>\n";
		print "Show Log ";
		print $cgi->a({-href => "$myself?project=$project&action=show_log&view_back=" . 60*60*24}, "day") . "\n";
		print $cgi->a({-href => "$myself?project=$project&action=show_log&view_back=" . 60*60*24*7}, "week") . "\n";
		print $cgi->a({-href => "$myself?project=$project&action=show_log&view_back=" . 60*60*24*30}, "month") . "\n";
		print $cgi->a({-href => "$myself?project=$project&action=show_log&view_back=" . 60*60*24*365}, "year") . "<br/>\n";
		print "</div>\n";
		print "<br/><br/>\n";
	}
}

sub footer {
	print "</div>";
	print $cgi->end_html();
}

if ($project eq "") {
	open my $fd, "-|", "ls", "-1", $gitroot;
	my (@path) = map { chomp; $_ } <$fd>;
	close $fd;
	header();
	print "Projects:<br/><br/>\n";
	foreach my $line (@path) {
		if (-e "$gitroot/$line/.git/HEAD") {
			print $cgi->a({-href => "$myself?project=$line"}, $line) . "<br/>\n";
		}
	}
	footer();
	exit;
}

if ($action eq "") {
	print $cgi->redirect("$myself?project=$project&action=show_log&view_back=$view_back");
	exit;
}

if ($action eq "show_file") {
	header();
	print "<pre>\n";
	open my $fd, "-|", "$gitbin/cat-file", "blob", $hash;
	my $nr;
	while (my $line = <$fd>) {
		$nr++;
		print "$nr\t" . escapeHTML($line);;
	}
	close $fd;
	print "</pre>\n";
	footer();
} elsif ($action eq "show_tree") {
	if ($hash eq "") {
		open my $fd, "$projectroot/.git/HEAD";
		my $head = <$fd>;
		chomp $head;
		close $fd;

		open $fd, "-|", "$gitbin/cat-file", "commit", $head;
		my $tree = <$fd>;
		chomp $tree;
		$tree =~ s/tree //;
		close $fd;
		$hash = $tree;
	}
	open my $fd, "-|", "$gitbin/ls-tree", $hash;
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd;
	header();
	print "<pre>\n";
	foreach my $line (@entries) {
		$line =~ m/^([0-9]+)\t(.*)\t(.*)\t(.*)$/;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = $4;
		if ($t_type eq "blob") {
			print "FILE\t" . $cgi->a({-href => "$myself?project=$project&action=show_file&hash=$3"}, $4) . "\n";
		} elsif ($t_type eq "tree") {
			print "DIR\t" . $cgi->a({-href => "$myself?project=$project&action=show_tree&hash=$3"}, $4) . "\n";
		}
	}
	print "</pre>\n";
	footer();
} elsif ($action eq "show_log") {
	open my $fd, "$projectroot/.git/HEAD";
	my $head = <$fd>;
	chomp $head;
	close $fd;
	open my $fd, "-|", "$gitbin/rev-tree", $head;
	my (@revtree) = map { chomp; $_ } <$fd>;
	close $fd;
	header();
	print "<table cellspacing=\"0\" class=\"log\">\n";
	foreach my $rev (reverse sort @revtree) {
		if (!($rev =~ m/^([0-9]+) ([0-9a-fA-F]+).* ([0-9a-fA-F]+)/)) {
			last;
		}
		my $time = $1;
		my $commit = $2;
		my $parent = $3;
		my @parents;
		my $author;
		my $author_name;
		my $author_time;
		my $author_timezone;
		my $committer;
		my $committer_time;
		my $committer_timezone;
		my $tree;
		my $comment;
		my $shortlog;
		open my $fd, "-|", "$gitbin/cat-file", "commit", $commit;
		while (my $line = <$fd>) {
			chomp($line);
			if ($line eq "") {
				last;
			}
			if ($line =~ m/^tree (.*)$/) {
				$tree = $1;
			} elsif ($line =~ m/^parent (.*)$/) {
				push @parents, $1;
			} elsif ($line =~ m/^committer (.*>) ([0-9]+) (.*)$/) {
				$committer = $1;
				$committer_time = $2;
				$committer_timezone = $3;
			} elsif ($line =~ m/^author (.*>) ([0-9]+) (.*)$/) {
				$author = $1;
				$author_time = $2;
				$author_timezone = $3;
				$author =~ m/^(.*) </;
				$author_name = $1;
			}
		}
		$shortlog = <$fd>;
		$shortlog = escapeHTML($shortlog);
		$comment = $shortlog . "<br/>";
		while (my $line = <$fd>) {
				chomp($line);
				$comment .= escapeHTML($line) . "<br/>\n";
		}
		close $fd;
		my $age = time-$author_time;
		if ($view_back > 0 && $age > $view_back) {
			last;
		}

		my $age_string;
		if ($age > 60*60*24*365*2) {
			$age_string = int $age/60/60/24/365;
			$age_string .= " years ago";
		} elsif ($age > 60*60*24*365/12*2) {
			$age_string = int $age/60/60/24/365/12;
			$age_string .= " months ago";
		} elsif ($age > 60*60*24*7*2) {
			$age_string = int $age/60/60/24/7;
			$age_string .= " weeks ago";
		} elsif ($age > 60*60*24*2) {
			$age_string = int $age/60/60/24;
			$age_string .= " days ago";
		} elsif ($age > 60*60*2) {
			$age_string = int $age/60/60;
			$age_string .= " hours ago";
		} elsif ($age > 60*2) {
			$age_string = int $age/60;
			$age_string .= " minutes ago";
		}
		print "<tr>\n";
		print "<td class=\"head1\">" . $age_string . "</td>\n";
		print "<td class=\"head1\"><a href=\"$myself?project=$project&amp;action=show_cset&amp;hash=$commit&amp;parent=$parent\">" . $shortlog . "</a></td>";
		print "</tr>\n";
		print "<tr>\n";
		print "<td class=\"head2\"></td>\n";
		print "<td class=\"head2\">\n";
		print "author &nbsp; &nbsp;" . escapeHTML($author) . " [" . gmtime($author_time) . " " . $author_timezone . "]<br/>\n";
		print "committer " . escapeHTML($committer) . " [" . gmtime($committer_time) . " " . $committer_timezone . "]<br/>\n";
		print "commit &nbsp; &nbsp;<a href=\"$myself?project=$project&amp;action=show_cset&amp;hash=$commit&amp;parent=$parent\">$commit</a><br/>\n";
		print "tree &nbsp; &nbsp; &nbsp;<a href=\"$myself?project=$project&amp;action=show_tree&amp;hash=$tree\">$tree</a><br/>\n";
		foreach my $par (@parents) {
			print "parent &nbsp; &nbsp;$par<br/>\n";
		}
		print "</td>";
		print "</tr>\n";
		print "<tr>\n";
		print "<td></td>\n";
		print "<td>\n";
		print "$comment<br/><br/>\n";
		print "</td>";
		print "</tr>\n";
	}
	print "</table>\n";
	footer();
} elsif ($action eq "show_cset") {
	open my $fd, "-|", "$gitbin/cat-file", "commit", $hash;
	my $tree = <$fd>;
	chomp $tree;
	$tree =~ s/tree //;
	close $fd;

	open my $fd, "-|", "$gitbin/cat-file", "commit", $parent;
	my $parent_tree = <$fd>;
	chomp $parent_tree;
	$parent_tree =~ s/tree //;
	close $fd;

	open my $fd, "-|", "$gitbin/diff-tree", "-r", $parent_tree, $tree;
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	header();
	print "<pre>\n";
	foreach my $line (@difftree) {
		$line =~ m/^(.)(.*)\t(.*)\t(.*)\t(.*)$/;
		my $op = $1;
		my $mode = $2;
		my $type = $3;
		my $id = $4;
		my $file = $5;
		if ($type eq "blob") {
			if ($op eq "+") {
				print "NEW\t" . $cgi->a({-href => "$myself?project=$project&action=show_file&hash=$id"}, $file) . "\n";
			} elsif ($op eq "-") {
				print "DEL\t" . $cgi->a({-href => "$myself?project=$project&action=show_file&hash=$id"}, $file) . "\n";
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $old = $1;
				my $new = $2;
				print "DIFF\t" . $cgi->a({-href => "$myself?project=$project&action=show_diff&hash=$old&parent=$new"}, $file) . "\n";
			}
		}
	}
	print "</pre>\n";
	footer();
} elsif ($action eq "show_diff") {
	open my $fd2, "> $gittmp/$hash";
	open my $fd, "-|", "$gitbin/cat-file", "blob", $hash;
	while (my $line = <$fd>) {
		print $fd2 $line;
	}
	close $fd2;
	close $fd;

	open my $fd2, "> $gittmp/$parent";
	open my $fd, "-|", "$gitbin/cat-file", "blob", $parent;
	while (my $line = <$fd>) {
		print $fd2 $line;
	}
	close $fd2;
	close $fd;

	header();
	print "<pre>\n";
	open my $fd, "-|", "/usr/bin/diff", "-L", "$hash", "-L", "$parent", "-u", "-p", "$gittmp/$hash", "$gittmp/$parent";
	while (my $line = <$fd>) {
		print escapeHTML($line);
	}
	close $fd;
	unlink("$gittmp/$hash");
	unlink("$gittmp/$parent");
	print "</pre>\n";
	footer();
} else {
	header();
	print "unknown action\n";
	footer();
}
