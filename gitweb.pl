#!/usr/bin/perl

# gitweb.pl - simple web interface to track changes in git repositories
#
# Version 004
#
# This file is licensed under the GPL v2, or a later version
# (C) 2005, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke <ch@gierke.de>

use strict;
use warnings;
use CGI qw(:standard :escapeHTML);
use CGI::Carp qw(fatalsToBrowser);

my $cgi = new CGI;
my $gitbin = "/home/kay/bin/git";
my $gitroot = "/home/kay/public_html";
my $gittmp = "/tmp";
my $myself = $cgi->url(-relative => 1);

my $project = $cgi->param("project") || "";
my $action = $cgi->param("action") || "";
my $hash = $cgi->param("hash") || "";
my $parent = $cgi->param("parent") || "";
my $view_back = $cgi->param("view_back") || 60*60*24;
my $projectroot = "$gitroot/$project";
$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/.git/objects";

$hash =~ s/[^0-9a-fA-F]//g;
$parent =~ s/[^0-9a-fA-F]//g;
$project =~ s/[^0-9a-zA-Z\-\._]//g;

sub git_header {
	print $cgi->header(-type => 'text/html; charset: utf-8');
print <<EOF;
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
	<title>git - $project $action</title>
	<style type="text/css">
		body { font-family: sans-serif; font-size: 12px; margin:25px; }
		div.body { border-width:1px; border-style:solid; border-color:#D9D8D1; }
		div.head1 { font-size:20px; padding:8px; background-color: #D9D8D1; font-weight:bold; }
		div.head1 a:visited { color:#0000cc; }
		div.head1 a:hover { color:#880000; }
		div.head1 a:active { color:#880000; }
		div.head2 { padding:8px; }
		div.head2 a:visited { color:#0000cc; }
		div.head2 a:hover { color:#880000; }
		div.head2 a:active { color:#880000; }
		div.main { padding:8px; font-family: sans-serif; font-size: 12px; }
		table { padding:0px; margin:0px; width:100%; }
		tr { vertical-align:top; }
		td { padding:8px; margin:0px; font-family: sans-serif; font-size: 12px; }
		td.head1 { background-color: #D9D8D1; font-weight:bold; }
		td.head1 a { color:#000000; text-decoration:none; }
		td.head1 a:hover { color:#880000; text-decoration:underline; }
		td.head1 a:visited { color:#000000; }
		td.head2 { background-color: #EDECE6; font-family: monospace; font-size:12px; }
		td.head3 { background-color: #EDECE6; font-size:10px; }
		div.add { color: #008800; }
		div.subtract { color: #CC0000; }
		div.diff_head { color: #000099; }
		div.diff_head a:visited { color:#0000cc; }
		div.diff_line { color: #990099; }
		a { color:#0000cc; }
		a:hover { color:#880000; }
		a:visited { color:#880000; }
		a:active { color:#880000; }
	</style>
</head>
<body>
EOF
	print "<div class=\"body\">\n";
	print "<div class=\"head1\">";
	print "<a href=\"http://kernel.org/pub/software/scm/git/\"><img src=\"git_logo.png\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/></a>";
	print $cgi->a({-href => "$myself"}, "projects");
	if ($project ne "") {
		print " / " . $cgi->a({-href => "$myself?project=$project;action=log;view_back=" . 60*60*24}, $project);
	}
	if ($action ne "") {
		print " / $action";
	}
	print "</div>\n";
}

sub git_footer {
	print "</div>";
	print $cgi->end_html();
}

sub git_diff {
	my $old_name = shift || "/dev/null";
	my $new_name = shift || "/dev/null";
	my $old = shift;
	my $new = shift;

	my $tmp_old = "/dev/null";
	my $tmp_new = "/dev/null";
	my $old_label = "/dev/null";
	my $new_label = "/dev/null";

	# create temp from-file
	if ($old ne "") {
		open my $fd2, "> $gittmp/$old";
		open my $fd, "-|", "$gitbin/cat-file", "blob", $old;
		while (my $line = <$fd>) {
			print $fd2 $line;
		}
		close $fd2;
		close $fd;
		$tmp_old = "$gittmp/$old";
		$old_label = "a/$old_name";
	}

	# create tmp to-file
	if ($new ne "") {
		open my $fd2, "> $gittmp/$new";
		open my $fd, "-|", "$gitbin/cat-file", "blob", $new;
		while (my $line = <$fd>) {
			print $fd2 $line;
		}
		close $fd2;
		close $fd;
		$tmp_new = "$gittmp/$new";
		$new_label = "b/$new_name";
	}

	open my $fd, "-|", "/usr/bin/diff", "-L", $old_label, "-L", $new_label, "-u", "-p", $tmp_old, $tmp_new;
	print '<div class="diff_head">===== ';
	if ($old ne "") {
		print $cgi->a({-href => "$myself?project=$project;action=blob;hash=$old"}, $old);
	} else {
		print $old_name;
	}
	print " vs ";
	if ($new ne "") {
		print $cgi->a({-href => "$myself?project=$project;action=blob;hash=$new"}, $new);
	} else {
		print $new_name;
	}
	print ' =====</div>';
	while (my $line = <$fd>) {
		my $char = substr($line,0,1);
		print '<div class="add">' if $char eq '+';
		print '<div class="subtract">' if $char eq '-';
		print '<div class="diff_line">' if $char eq '@';
		print escapeHTML($line);
		print '</div>' if $char eq '+' or $char eq '-' or $char eq '@';
	}
	close $fd;
	unlink("$gittmp/$new");
	unlink("$gittmp/$old");
}

if ($project eq "") {
	open my $fd, "-|", "ls", "-1", $gitroot;
	my (@path) = map { chomp; $_ } <$fd>;
	close $fd;
	git_header();
	print "<br/><br/><div class=\"main\">\n";
	foreach my $line (@path) {
		if (-e "$gitroot/$line/.git/HEAD") {
			print $cgi->a({-href => "$myself?project=$line"}, $line) . "<br/>\n";
		}
	}
	print "<br/></div>";
	git_footer();
	exit;
}

if ($action eq "") {
	print $cgi->redirect("$myself?project=$project;action=log;view_back=$view_back");
	exit;
}

if ($action eq "blob") {
	git_header();
	print "<br/><br/><div class=\"main\">\n";
	print "<pre>\n";
	open my $fd, "-|", "$gitbin/cat-file", "blob", $hash;
	my $nr;
	while (my $line = <$fd>) {
		$nr++;
		print "$nr\t" . escapeHTML($line);;
	}
	close $fd;
	print "</pre>\n";
	print "<br/></div>";
	git_footer();
} elsif ($action eq "tree") {
	if ($hash eq "") {
		open my $fd, "$projectroot/.git/HEAD";
		my $head = <$fd>;
		chomp $head;
		close $fd;
		$hash = $head;
	}
	open my $fd, "-|", "$gitbin/ls-tree", $hash;
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd;
	git_header();
	print "<br/><br/><div class=\"main\">\n";
	print "<pre>\n";
	foreach my $line (@entries) {
		#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+)\t(.*)\t(.*)\t(.*)$/;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = $4;
		if ($t_type eq "blob") {
			print "BLOB\t" . $cgi->a({-href => "$myself?project=$project;action=blob;hash=$3"}, $4) . "\n";
		} elsif ($t_type eq "tree") {
			print "TREE\t" . $cgi->a({-href => "$myself?project=$project;action=tree;hash=$3"}, $4) . "\n";
		}
	}
	print "</pre>\n";
	print "<br/></div>";
	git_footer();
} elsif ($action eq "log" || $action eq "show_log" ) {
	open my $fd, "$projectroot/.git/HEAD";
	my $head = <$fd>;
	chomp $head;
	close $fd;
	open $fd, "-|", "$gitbin/rev-tree", $head;
	my (@revtree) = map { chomp; $_ } <$fd>;
	close $fd;
	git_header();
	print "<div class=\"head2\">\n";
	print "view  ";
	print $cgi->a({-href => "$myself?project=$project;action=log;view_back=" . 60*60*24}, "last day") . " | ";
	print $cgi->a({-href => "$myself?project=$project;action=log;view_back=" . 60*60*24*7}, "week") . " | ";
	print $cgi->a({-href => "$myself?project=$project;action=log;view_back=" . 60*60*24*30}, "month") . " | ";
	print $cgi->a({-href => "$myself?project=$project;action=log;view_back=" . 60*60*24*365}, "year") . " | ";
	print $cgi->a({-href => "$myself?project=$project;action=log;view_back=-1"}, "all") . "<br/>\n";
	print "<br/><br/>\n";
	print "</div>\n";
	print "<table cellspacing=\"0\" class=\"log\">\n";
	foreach my $rev (reverse sort @revtree) {
		# '1114106118 755e3010ee10dadf42a8a80770e1b115fb038d9b:1 2af17b4854036a1c2ec6c101d93c8dd1ed80d24e:1'
		last if !($rev =~ m/^([0-9]+) ([0-9a-fA-F]+).* ([0-9a-fA-F]+)/);
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
			last if $line eq "";
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
		my $age = time-$committer_time;
		last if ($view_back > 0 && $age > $view_back);

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
		print "<td class=\"head1\">" . $cgi->a({-href => "$myself?project=$project;action=commit;hash=$commit;parent=$parent"}, $shortlog) . "</td>";
		print "</tr>\n";
		print "<tr>\n";
		print "<td class=\"head3\">";
		print $cgi->a({-href => "$myself?project=$project;action=treediff;hash=$commit;parent=$parent"}, "view diff") . "<br/>\n";
		print $cgi->a({-href => "$myself?project=$project;action=commit;hash=$commit;parent=$parent"}, "view commit") . "<br/>\n";
		print $cgi->a({-href => "$myself?project=$project;action=tree;hash=$tree"}, "view tree") . "<br/>\n";
		print "</td>\n";
		print "<td class=\"head2\">\n";
		print "author &nbsp; &nbsp;" . escapeHTML($author) . " [" . gmtime($author_time) . " " . $author_timezone . "]<br/>\n";
		print "committer " . escapeHTML($committer) . " [" . gmtime($committer_time) . " " . $committer_timezone . "]<br/>\n";
		print "commit &nbsp; &nbsp;$commit<br/>\n";
		print "tree &nbsp; &nbsp; &nbsp;$tree<br/>\n";
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
	git_footer();
} elsif ($action eq "commit") {
	open my $fd, "-|", "$gitbin/diff-tree", "-r", $parent, $hash;
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header();
	print "<div class=\"head2\">\n";
	print "view " . $cgi->a({-href => "$myself?project=$project;action=treediff;hash=$hash;parent=$parent"}, "diff") . "<br/><br/>\n";
	print "<pre>\n";
	foreach my $line (@difftree) {
		# '*100644->100644	blob	9f91a116d91926df3ba936a80f020a6ab1084d2b->bb90a0c3a91eb52020d0db0e8b4f94d30e02d596	net/ipv4/route.c'
		# '+100644	blob	4a83ab6cd565d21ab0385bac6643826b83c2fcd4	arch/arm/lib/bitops.h'
		$line =~ m/^(.)(.*)\t(.*)\t(.*)\t(.*)$/;
		my $op = $1;
		my $mode = $2;
		my $type = $3;
		my $id = $4;
		my $file = $5;
		if ($type eq "blob") {
			if ($op eq "+") {
				print "NEW\t" . $cgi->a({-href => "$myself?project=$project;action=blob;hash=$id"}, $file) . "\n";
			} elsif ($op eq "-") {
				print "DEL\t" . $cgi->a({-href => "$myself?project=$project;action=blob;hash=$id"}, $file) . "\n";
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $old = $1;
				my $new = $2;
				print "CHANGED\t" . $cgi->a({-href => "$myself?project=$project;action=diff;hash=$old;parent=$new"}, $file) . "\n";
			}
		}
	}
	print "</pre>\n";
	print "<br/></div>";
	git_footer();
} elsif ($action eq "diff") {
	git_header();
	print "<br/><br/><div class=\"main\">\n";
	print "<pre>\n";
	git_diff($hash, $parent, $hash, $parent);
	print "</pre>\n";
	print "<br/></div>";
	git_footer();
} elsif ($action eq "treediff") {
	open my $fd, "-|", "$gitbin/diff-tree", "-r", $parent, $hash;
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header();
	print "<div class=\"head2\">\n";
	print "view " . $cgi->a({-href => "$myself?project=$project;action=commit;hash=$hash;parent=$parent"}, "commit") . "<br/><br/>\n";
	print "<pre>\n";
	foreach my $line (@difftree) {
		# '*100644->100644	blob	8e5f9bbdf4de94a1bc4b4da8cb06677ce0a57716->8da3a306d0c0c070d87048d14a033df02f40a154	Makefile'
		$line =~ m/^(.)(.*)\t(.*)\t(.*)\t(.*)$/;
		my $op = $1;
		my $mode = $2;
		my $type = $3;
		my $id = $4;
		my $file = $5;
		if ($type eq "blob") {
			if ($op eq "+") {
				git_diff("", $file, "", $id);
			} elsif ($op eq "-") {
				git_diff($file, "", $id, "");
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				git_diff($file, $file, $1, $2);
			}
		}
	}
	print "</pre>\n";
	print "<br/></div>";
	git_footer();
} else {
	git_header();
	print "<br/><br/><div class=\"main\">\n";
	print "unknown action\n";
	print "<br/></div>";
	git_footer();
}
