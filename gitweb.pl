#!/usr/bin/perl

# gitweb.pl - simple web interface to track changes in git repositories
#
# Version 031
#
# (C) 2005, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke <ch@gierke.de>
#
# This file is licensed under the GPL v2, or a later version

use strict;
use warnings;
use CGI qw(:standard :escapeHTML);
use CGI::Carp qw(fatalsToBrowser);

my $cgi = new CGI;

my $projectroot =	"/home/kay/public_html";
my $defaultprojects =	".";
my $gitbin =		"/home/kay/bin/git";
my $gittmp =		"/tmp";
my $my_url =		$cgi->url();
my $my_uri =		$cgi->url(-absolute => 1);
my $my_url_parm =	$cgi->url(-path => 1);
$my_url_parm =~ s/.*$my_uri//;

my $project = "";
my $action = "";
my $hash = "";
my $hash_parent = "";
my $view_back = 1;

# get values from url
if ($my_url_parm =~ m#/+(.+)/+commit/+([0-9a-fA-F]+)$#) {
	$project = $1;
	$action = "commit";
	$hash = $2;
} elsif ($my_url_parm =~ m#/+(.+)/+commitdiff/+([0-9a-fA-F]+)$#) {
	$project = $1;
	$action = "commitdiff";
	$hash = $2;
} elsif ($my_url_parm =~ m#/+(.+)/+blobdiff/+([0-9a-fA-F]+)/([0-9a-fA-F]+)$#) {
	$project = $1;
	$action = "blobdiff";
	$hash = $2;
	$hash_parent = $3;
} elsif ($my_url_parm =~ m#/+(.+)/+blob/+([0-9a-fA-F]+)$#) {
	$project = $1;
	$action = "blob";
	$hash = $2;
} elsif ($my_url_parm =~ m#/+(.+)/+tree/+([0-9a-fA-F]+)$#) {
	$project = $1;
	$action = "tree";
	$hash = $2;
} elsif ($my_url_parm =~ m#/+(.+)/+log/+([0-9]+)$#) {
	$project = $1;
	$action = "log";
	$view_back = $2;
} elsif ($my_url_parm =~ m#/+(.+)/+log$#) {
	$project = $1;
	$action = "log";
	$view_back = 1;
} elsif ($my_url_parm =~ m#/+(.+)/rss$#) {
	$project = $1;
	$action = "rss";
	$view_back = 1;
} elsif ($my_url_parm =~ m#/+git-logo.png$#) {
	print $cgi->header(-type => 'image/png', -expires => '+1d');
	print	"\211\120\116\107\015\012\032\012\000\000\000\015\111\110\104\122".
		"\000\000\000\110\000\000\000\033\004\003\000\000\000\055\331\324".
		"\055\000\000\000\030\120\114\124\105\377\377\377\140\140\135\260".
		"\257\252\000\200\000\316\315\307\300\000\000\350\350\346\367\367".
		"\366\225\014\247\107\000\000\000\163\111\104\101\124\050\317\143".
		"\110\147\040\004\112\134\030\012\010\052\142\123\141\040\002\010".
		"\015\151\105\254\241\241\001\060\014\223\140\066\046\122\221\261".
		"\001\021\326\341\125\144\154\154\314\154\154\014\242\014\160\052".
		"\142\006\052\301\142\035\263\001\002\123\244\010\350\000\003\030".
		"\046\126\021\324\341\040\227\033\340\264\016\065\044\161\051\202".
		"\231\060\270\223\012\021\271\105\210\301\215\240\242\104\041\006".
		"\047\101\202\100\205\301\105\211\040\160\001\000\244\075\041\305".
		"\022\034\232\376\000\000\000\000\111\105\116\104\256\102\140\202";
	exit;
} elsif ($my_url_parm =~ m#/(.+)$#) {
	$project = $1;
	$action = "log";
	$view_back = 1;
}

# sanitize input
$project =~ s#\/\.+##g;

$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$project/.git/objects";

sub git_header_html {
	print $cgi->header(-type => 'text/html', -charset => 'utf-8');
print <<EOF;
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
	<title>git - $project $action</title>
	<link rel="alternate" title="$project log" href="$my_uri/$project/rss" type="application/rss+xml"/>
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
		div.title { padding:8px; background-color: #D9D8D1; font-weight:bold; }
		div.title a { color:#000000; text-decoration:none; }
		div.title a:hover { color:#880000; text-decoration:underline; }
		div.title a:visited { color:#000000; }
		table { padding:0px; margin:0px; width:100%; }
		tr { vertical-align:top; }
		td { padding:8px; margin:0px; font-family: sans-serif; font-size: 12px; }
		td.head1 { background-color: #D9D8D1; font-weight:bold; }
		td.head1 a { color:#000000; text-decoration:none; }
		td.head1 a:hover { color:#880000; text-decoration:underline; }
		td.head1 a:visited { color:#000000; }
		td.head2 { background-color: #EDECE6; font-family: monospace; font-size:12px; }
		td.head3 { background-color: #EDECE6; font-size:10px; }
		div.signed_off { color: #a9a8a1; }
		a { color:#0000cc; }
		a:hover { color:#880000; }
		a:visited { color:#880000; }
		a:active { color:#880000; }
		pre { padding:8px; }
	</style>
</head>
<body>
EOF
	print "<div class=\"body\">\n";
	print "<div class=\"head1\">";
	print "<a href=\"http://kernel.org/pub/software/scm/git/\">" .
	      "<img src=\"$my_uri/git-logo.png\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/></a>";
	if ($defaultprojects ne "") {
		print $cgi->a({-href => "$my_uri"}, "projects") . " / ";
	}
	if ($project ne "") {
		print $cgi->a({-href => "$my_uri/$project/log"}, $project);
	}
	if ($action ne "") {
		print " / $action";
	}
	print "</div>\n";
}

sub git_footer_html {
	print "</div>\n";
	print "</body>\n</html>";
}

sub git_head {
	open my $fd, "$projectroot/$project/.git/HEAD";
	my $head = <$fd>;
	close $fd;
	chomp $head;
	return $head;
}

sub git_commit {
	my $commit = shift;
	my %co;
	my @parents;

	open my $fd, "-|", "$gitbin/cat-file", "commit", $commit;
	while (my $line = <$fd>) {
		chomp($line);
		last if $line eq "";
		if ($line =~ m/^tree (.*)$/) {
			$co{'tree'} = $1;
		} elsif ($line =~ m/^parent (.*)$/) {
			push @parents, $1;
		} elsif ($line =~ m/^committer (.*>) ([0-9]+) (.*)$/) {
			$co{'committer'} = $1;
			$co{'committer_time'} = $2;
			$co{'committer_timezone'} = $3;
		} elsif ($line =~ m/^author (.*>) ([0-9]+) (.*)$/) {
			$co{'author'} = $1;
			$co{'author_time'} = $2;
			$co{'author_timezone'} = $3;
		}
	}
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];
	my (@comment) = map { chomp; $_ } <$fd>;
	$co{'comment'} = \@comment;
	$co{'title'} = $comment[0];
	close $fd;
	return %co;
}

sub git_diff_html {
	my $from_name = shift || "/dev/null";
	my $to_name = shift || "/dev/null";
	my $from = shift;
	my $to = shift;

	my $from_tmp = "/dev/null";
	my $to_tmp = "/dev/null";
	my $from_label = "/dev/null";
	my $to_label = "/dev/null";
	my $pid = $$;

	# create temp from-file
	if ($from ne "") {
		$from_tmp = "$gittmp/gitweb_" . $$ . "_from";
		open my $fd2, "> $from_tmp";
		open my $fd, "-|", "$gitbin/cat-file", "blob", $from;
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
		$from_label = "a/$from_name";
	}

	# create tmp to-file
	if ($to ne "") {
		$to_tmp = "$gittmp/gitweb_" . $$ . "_to";
		open my $fd2, "> $to_tmp";
		open my $fd, "-|", "$gitbin/cat-file", "blob", $to;
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
		$to_label = "b/$to_name";
	}

	open my $fd, "-|", "/usr/bin/diff", "-L", $from_label, "-L", $to_label, "-u", "-p", $from_tmp, $to_tmp;
	print "<span style =\"color: #000099;\">===== ";
	if ($from ne "") {
		print $cgi->a({-href => "$my_uri/$project/blob/$from"}, $from);
	} else {
		print $from_name;
	}
	print " vs ";
	if ($to ne "") {
		print $cgi->a({-href => "$my_uri/$project/blob/$to"}, $to);
	} else {
		print $to_name;
	}
	print " =====</span>\n";
	while (my $line = <$fd>) {
		my $char = substr($line,0,1);
		print '<span style ="color: #008800;">' if $char eq '+';
		print '<span style ="color: #CC0000;">' if $char eq '-';
		print '<span style ="color: #990099;">' if $char eq '@';
		print escapeHTML($line);
		print '</span>' if $char eq '+' or $char eq '-' or $char eq '@';
	}
	close $fd;

	if ($from ne "") {
		unlink("$from_tmp");
	}
	if ($to ne "") {
		unlink("$to_tmp");
	}
}

sub mode_str {
	my $perms = oct shift;
	my $modestr;
	if ($perms & 040000) { $modestr .= 'd' } else { $modestr .= '-' };
	for (my $i = 0; $i < 3; $i++) {
		if ($perms & 0400) { $modestr .= 'r' } else { $modestr .= '-' };
		if ($perms & 0200) { $modestr .= 'w' } else { $modestr .= '-' };
		if ($perms & 0100) { $modestr .= 'x' } else { $modestr .= '-' };
		$perms <<= 3;
	}
	return $modestr;
}

# show list of default projects
if ($project eq "") {
	opendir(my $fd, "$projectroot/$defaultprojects");
	my (@path) = grep(!/^\./, readdir($fd));
	closedir($fd);
	git_header_html();
	print "<div class=\"head2\">\n";
	print "<br/><br/>\n";
	foreach my $line (@path) {
		if (-e "$projectroot/$defaultprojects/$line/.git/HEAD") {
			print $cgi->a({-href => "$my_uri/$defaultprojects/$line/log"}, $line) . "<br/>\n";
		}
	}
	print "</div><br/>";
	git_footer_html();
	exit;
}

if ($action eq "blob") {
	git_header_html();
	print "<br/><br/>\n";
	print "<pre>\n";
	open my $fd, "-|", "$gitbin/cat-file", "blob", $hash;
	my $nr;
	while (my $line = <$fd>) {
		$nr++;
		print "$nr\t" . escapeHTML($line);;
	}
	close $fd;
	print "</pre>\n";
	print "<br/>";
	git_footer_html();
} elsif ($action eq "tree") {
	if ($hash eq "") {
		$hash = git_head();
	}
	open my $fd, "-|", "$gitbin/ls-tree", $hash;
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd;
	git_header_html();
	print "<br/><br/>\n";
	print "<pre>\n";
	foreach my $line (@entries) {
		#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+)\t(.*)\t(.*)\t(.*)$/;
		my $t_mode = $1;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = $4;
		if ($t_type eq "blob") {
			print mode_str($t_mode). " " . $cgi->a({-href => "$my_uri/$project/blob/$t_hash"}, $t_name) . "\n";
		} elsif ($t_type eq "tree") {
			print mode_str($t_mode). " " . $cgi->a({-href => "$my_uri/$project/tree/$t_hash"}, $t_name) . "\n";
		}
	}
	print "</pre>\n";
	print "<br/>";
	git_footer_html();
} elsif ($action eq "log" || $action eq "rss") {
	open my $fd, "-|", "$gitbin/rev-list", git_head();
	my (@revtree) = map { chomp; $_ } <$fd>;
	close $fd;

	if ($action eq "log") {
		git_header_html();
		print "<div class=\"head2\">\n";
		print "view  ";
		print $cgi->a({-href => "$my_uri/$project/log"}, "last day") . " | ";
		print $cgi->a({-href => "$my_uri/$project/log/7"}, "week") . " | ";
		print $cgi->a({-href => "$my_uri/$project/log/31"}, "month") . " | ";
		print $cgi->a({-href => "$my_uri/$project/log/365"}, "year") . " | ";
		print $cgi->a({-href => "$my_uri/$project/log/0"}, "all") . "<br/>\n";
		print "<br/><br/>\n";
		print "</div>\n";
		print "<table cellspacing=\"0\" class=\"log\">\n";
	} elsif ($action eq "rss") {
		print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
		print "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n".
		      "<rss version=\"0.91\">\n";
		print "<channel>\n";
		print "<title>$project</title>\n".
		      "<link> " . $my_url . "/$project/log</link>\n".
		      "<description>$project log</description>\n".
		      "<language>en</language>\n";
	}

	for (my $i = 0; $i <= $#revtree; $i++) {
		my $commit = $revtree[$i];

		my %co = git_commit($commit);
		my $age = time - $co{'committer_time'};
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
		if ($action eq "log") {
			if ($view_back > 0 && $age > $view_back*60*60*24) {
				if ($i == 0) {
					print "<tr>\n";
					print "<td class=\"head1\"> Last change $age_string. </td>\n";
					print "</tr>\n";
				}
				last;
			}
			print "<tr>\n";
			print "<td class=\"head1\">" . $age_string . "</td>\n";
			print "<td class=\"head1\">" . $cgi->a({-href => "$my_uri/$project/commit/$commit"}, $co{'title'}) . "</td>";
			print "</tr>\n";
			print "<tr>\n";
			print "<td class=\"head3\">";
			print $cgi->a({-href => "$my_uri/$project/commit/$commit"}, "view commit") . "<br/>\n";
			print $cgi->a({-href => "$my_uri/$project/commitdiff/$commit"}, "view diff") . "<br/>\n";
			print "</td>\n";
			print "<td class=\"head2\">\n";
			print "author &nbsp; &nbsp;" . escapeHTML($co{'author'}) . " [" . gmtime($co{'author_time'}) . " " . $co{'author_timezone'} . "]<br/>\n";
			print "committer " . escapeHTML($co{'committer'}) . " [" . gmtime($co{'committer_time'}) . " " . $co{'committer_timezone'} . "]<br/>\n";
			print "</td>";
			print "</tr>\n";
			print "<tr>\n";
			print "<td></td>\n";
			print "<td>\n";
			my $comment = $co{'comment'};
			foreach my $line (@$comment) {
				if ($line =~ m/^(signed-off|acked)-by:/i) {
					print '<div class="signed_off">' . escapeHTML($line) . "<br/></div>\n";
				} else {
					print escapeHTML($line) . "<br/>\n";
				}
			}
			print "<br/><br/>\n";
			print "</td>";
			print "</tr>\n";
		} elsif ($action eq "rss") {
			last if ($i >= 20);
			my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday) = gmtime($co{'author_time'});
			my @months = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec");
			print "<item>\n\t<title>";
			printf("%s %02d, %02d:%02d - ", $months[$mon], $mday, $hour, $min);
			print escapeHTML($co{'title'}) . "</title>\n";
			print "\t<link> " . $my_url . "/$project/commit/$commit</link>\n";
			print "\t<description>";
			my $comment = $co{'comment'};
			foreach my $line (@$comment) {
				print escapeHTML($line) . "\n";
			}
			print "\t</description>\n";
			print "</item>\n";
		}
	}
	if ($action eq "log") {
		print "</table>\n";
		git_footer_html();
	} elsif ($action eq "rss") {
		print "</channel></rss>";
	}
} elsif ($action eq "commit") {
	my %co = git_commit($hash);
	open my $fd, "-|", "$gitbin/diff-tree", "-r", $co{'parent'}, $hash;
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"head2\"> view\n";
	print $cgi->a({-href => "$my_uri/$project/commit/$hash"}, "commit") . " | ";
	print $cgi->a({-href => "$my_uri/$project/commitdiff/$hash"}, "diff");
	print "</div><br/><br/>\n";
	print "<div class=\"title\">" . $cgi->a({-href => "$my_uri/$project/commitdiff/$hash"}, $co{'title'}) . "<br/></div>\n";
	print "<table cellspacing=\"0\" class=\"log\">\n";
	print "<tr>\n";
	print "<td class=\"head2\">";
	print "author &nbsp; &nbsp;" . escapeHTML($co{'author'}) . " [" . gmtime($co{'author_time'}) . " " . $co{'author_timezone'} . "]<br/>\n";
	print "committer " . escapeHTML($co{'committer'}) . " [" . gmtime($co{'committer_time'}) . " " . $co{'committer_timezone'} . "]<br/>\n";
	print "commit &nbsp; &nbsp;$hash<br/>\n";
	print "tree &nbsp; &nbsp; &nbsp;" . $cgi->a({-href => "$my_uri/$project/tree/$co{'tree'}"}, $co{'tree'}) . "<br/>\n";
	my $parents  = $co{'parents'};
	foreach my $par (@$parents) {
		print "parent &nbsp; &nbsp;" . $cgi->a({-href => "$my_uri/$project/tree/$par"}, $par) . "<br/>\n";
	}
	print "</td>";
	print "</tr>\n";
	print "<tr>\n";
	print "<td>\n";
	my $comment = $co{'comment'};
	foreach my $line (@$comment) {
		if ($line =~ m/(signed-off|acked)-by:/i) {
			print '<div class="signed_off">' . escapeHTML($line) . "<br/></div>\n";
		} else {
			print escapeHTML($line) . "<br/>\n";
		}
	}
	print "<br/><br/>\n";
	print "</td>";
	print "</tr>\n";
	print "</table>";

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
		$mode =~ m/^([0-7]{6})/;
		my $modestr = mode_str($1);
		if ($type eq "blob") {
			if ($op eq "+") {
				print "added\t$modestr " . $cgi->a({-href => "$my_uri/$project/blob/$id"}, $file) . "\n";
			} elsif ($op eq "-") {
				print "removed\t$modestr " . $cgi->a({-href => "$my_uri/$project/blob/$id"}, $file) . "\n";
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $old = $1;
				my $new = $2;
				print "changed\t$modestr " . $cgi->a({-href => "$my_uri/$project/blobdiff/$old/$new"}, $file) . "\n";
			}
		}
	}
	print "</pre>\n";
	print "<br/>";
	git_footer_html();
} elsif ($action eq "blobdiff") {
	git_header_html();
	print "<br/><br/>\n";
	print "<pre>\n";
	git_diff_html($hash, $hash_parent, $hash, $hash_parent);
	print "</pre>\n";
	print "<br/>";
	git_footer_html();
} elsif ($action eq "commitdiff") {
	my %co = git_commit($hash);
	open my $fd, "-|", "$gitbin/diff-tree", "-r", $co{'parent'}, $hash;
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"head2\"> view\n";
	print $cgi->a({-href => "$my_uri/$project/commit/$hash"}, "commit") . " | ";
	print $cgi->a({-href => "$my_uri/$project/commitdiff/$hash"}, "diff");
	print "</div><br/><br/>\n";
	print "<div class=\"title\">" . $cgi->a({-href => "$my_uri/$project/commit/$hash"}, $co{'title'}) . "<br/></div>\n";
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
				git_diff_html("", $file, "", $id);
			} elsif ($op eq "-") {
				git_diff_html($file, "", $id, "");
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				git_diff_html($file, $file, $1, $2);
			}
		}
	}
	print "</pre>\n";
	print "<br/>";
	git_footer_html();
}
