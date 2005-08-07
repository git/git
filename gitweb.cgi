#!/usr/bin/perl

# gitweb.pl - simple web interface to track changes in git repositories
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

my $version =		"064";
my $projectroot =	"/pub/scm";
my $defaultprojects =	"linux/kernel/git";
my $gitbin =		"/usr/bin";
my $gittmp =		"/tmp/gitweb";
my $giturl =		"/pub/software/scm/cogito";
my $my_url =		$cgi->url();
my $my_uri =		$cgi->url(-absolute => 1);

mkdir($gittmp, 0700);
my $project = $cgi->param('p');
my $action = $cgi->param('a');
my $hash = $cgi->param('h');
my $hash_parent = $cgi->param('hp');
my $file_name = $cgi->param('f');
my $time_back = $cgi->param('t');
$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$project/objects";

# validate input
if (defined($project) && $project =~ /(^|\/)(|\.|\.\.)($|\/)/) {
	die_error("", "Invalid project parameter.");
}
if (defined($file_name) && $file_name =~ /(^|\/)(|\.|\.\.)($|\/)/) {
	die_error("", "Invalid file parameter.");
}
if (defined($action) && !$action =~ m/^[0-9a-zA-Z\.\-]+$/) {
	die_error("", "Invalid action parameter.");
}
if (defined($hash) && !($hash =~ m/^[0-9a-fA-F]{40}$/)) {
	die_error("", "Invalid hash parameter.");
}
if (defined($hash_parent) && !($hash_parent =~ m/^[0-9a-fA-F]{40}$/)) {
	die_error("", "Invalid parent hash parameter.");
}
if (defined($time_back) && !($time_back =~ m/^[0-9]+$/)) {
	die_error("", "Invalid time parameter.");
}

sub git_header_html {
	my $status = shift || "200 OK";

	print $cgi->header(-type=>'text/html',  -charset => 'utf-8', -status=> $status);
	print <<EOF;
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
	<title>git - $project $action</title>
	<link rel="alternate" title="$project log" href="$my_uri?p=$project;a=rss" type="application/rss+xml"/>
	<style type="text/css">
	body { font-family: sans-serif; font-size: 12px; margin:0px; }
	a { color:#0000cc; }
	a:hover { color:#880000; }
	a:visited { color:#880000; }
	a:active { color:#880000; }
	div.page_header {
		margin:15px 25px 0px; height:25px; padding:8px;
		font-size:18px; clear:both; font-weight:bold; background-color: #d9d8d1;
	}
	div.page_header a:visited { color:#0000cc; }
	div.page_nav { margin:0px 25px; padding:8px; clear:both; border:solid #d9d8d1; border-width:0px 1px; }
	div.page_nav a:visited { color:#0000cc; }
	div.page_footer {
		margin:0px 25px 15px; height:17px; padding:4px; padding-left:8px;
		clear:both; background-color: #d9d8d1;
	}
	div.page_footer_text { float:left; color:#888888; font-size:10px;}
	div.page_body { margin:0px 25px; padding:8px; clear:both; border: solid #d9d8d1; border-width:0px 1px; }
	div.title {
		display:block; margin:0px 25px; padding:8px; clear:both;
		font-weight:bold; background-color: #d9d8d1; color:#000000;
	}
	a.log_title {
		display:block; margin:0px 25px; padding:8px; clear:both;
		font-weight:bold; background-color: #d9d8d1; text-decoration:none; color:#000000;
	}
	a.log_title:hover { background-color: #c9c8c1; }
	a.xml_logo { float:right; border:1px solid;
		line-height:15px;
		border-color:#fcc7a5 #7d3302 #3e1a01 #ff954e; width:35px;
		color:#ffffff; background-color:#ff6600;
		font-weight:bold; font-family:sans-serif; text-align:center;
		font-size:11px; display:block; text-decoration:none;
	}
	a.xml_logo:hover { background-color:#ee5500; }
	div.log_head {
		margin:0px 25px; min-height: 30px; padding:8px; clear:both;
		border: solid #d9d8d1; border-width:0px 1px; font-family:monospace;
		background-color: #edece6;
	}
	div.log_body {
		margin:0px 25px; padding:8px; padding-left:150px; clear:both;
		border:solid #d9d8d1; border-width:0px 1px;
	}
	span.log_age { position:relative; float:left; width:142px; }
	div.log_functions { font-size:10px; font-family:sans-serif; position:relative; float:left; width:142px; }
	div.signed_off { color: #a9a8a1; }
	</style>
</head>
<body>
EOF
	print "<div class=\"page_header\">\n" .
	      "<a href=\"$giturl\">" .
	      "<img src=\"$my_uri?a=git-logo.png\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/></a>";
	if ($defaultprojects ne "") {
		print $cgi->a({-href => "$my_uri"}, "projects") . " / ";
	}
	if ($project ne "") {
		print $cgi->a({-href => "$my_uri?p=$project;a=log"}, $project);
	}
	if ($action ne "") {
		print " / $action";
	}
	print "</div>\n";
}

sub git_footer_html {
	print "<div class=\"page_footer\">";
	print "<div class=\"page_footer_text\">version $version</div>";
	if ($project ne '') {
		print $cgi->a({-href => "$my_uri?p=$project;a=rss", -class => "xml_logo"}, "XML") . "\n";
	}
	print "</div>";
	print "</body>\n</html>";
}

sub die_error {
	my $status = shift || "403 Forbidden";
	my $error = shift || "Malformed query, file missing or permission denied"; 

	$project = "";
	$action = "";
	git_header_html($status);
	print "<div class=\"page_body\">\n" .
	      "<br/><br/>\n";
	print "$error\n";
	print "<br/></div>\n";
	git_footer_html();
	exit 0;
}

sub git_head {
	my $path = shift;
	open(my $fd, "$projectroot/$path/HEAD") || die_error("", "Invalid project directory.");;
	my $head = <$fd>;
	close $fd;
	chomp $head;
	return $head;
}

sub git_commit {
	my $commit = shift;
	my %co;
	my @parents;

	open my $fd, "-|", "$gitbin/cat-file commit $commit";
	while (my $line = <$fd>) {
		chomp($line);
		last if $line eq "";
		if ($line =~ m/^tree (.*)$/) {
			$co{'tree'} = $1;
		} elsif ($line =~ m/^parent (.*)$/) {
			push @parents, $1;
		} elsif ($line =~ m/^author (.*) ([0-9]+) (.*)$/) {
			$co{'author'} = $1;
			$co{'author_epoch'} = $2;
			$co{'author_tz'} = $3;
			$co{'author_name'} = $co{'author'};
			$co{'author_name'} =~ s/ <.*//;
		} elsif ($line =~ m/^committer (.*) ([0-9]+) (.*)$/) {
			$co{'committer'} = $1;
			$co{'committer_epoch'} = $2;
			$co{'committer_tz'} = $3;
			$co{'committer_name'} = $co{'committer'};
			$co{'committer_name'} =~ s/ <.*//;
		}
	}
	if (!defined($co{'tree'})) { die_error("", "Invalid commit object."); }
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];
	my (@comment) = map { chomp; $_ } <$fd>;
	$co{'comment'} = \@comment;
	$co{'title'} = $comment[0];
	close $fd;

	my $age = time - $co{'committer_epoch'};
	$co{'age'} = $age;
	if ($age > 60*60*24*365*2) {
		$co{'age_string'} = (int $age/60/60/24/365);
		$co{'age_string'} .= " years ago";
	} elsif ($age > 60*60*24*365/12*2) {
		$co{'age_string'} = int $age/60/60/24/365/12;
		$co{'age_string'} .= " months ago";
	} elsif ($age > 60*60*24*7*2) {
		$co{'age_string'} = int $age/60/60/24/7;
		$co{'age_string'} .= " weeks ago";
	} elsif ($age > 60*60*24*2) {
		$co{'age_string'} = int $age/60/60/24;
		$co{'age_string'} .= " days ago";
	} elsif ($age > 60*60*2) {
		$co{'age_string'} = int $age/60/60;
		$co{'age_string'} .= " hours ago";
	} elsif ($age > 60*2) {
		$co{'age_string'} = int $age/60;
		$co{'age_string'} .= " minutes ago";
	}
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

	# create tmp from-file
	if ($from ne "") {
		$from_tmp = "$gittmp/gitweb_" . $$ . "_from";
		open(my $fd2, "> $from_tmp");
		open my $fd, "-|", "$gitbin/cat-file blob $from";
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
		open my $fd, "-|", "$gitbin/cat-file blob $to";
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
		$to_label = "b/$to_name";
	}

	open my $fd, "-|", "/usr/bin/diff -u -p -L $from_label -L $to_label $from_tmp $to_tmp";
	print "<span style=\"color: #000099;\">===== ";
	if ($from ne "") {
		print $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$from"}, $from);
	} else {
		print $from_name;
	}
	print " vs ";
	if ($to ne "") {
		print $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to"}, $to);
	} else {
		print $to_name;
	}
	print " =====</span>\n";
	while (my $line = <$fd>) {
		my $char = substr($line,0,1);
		print '<span style="color: #008800;">' if $char eq '+';
		print '<span style="color: #CC0000;">' if $char eq '-';
		print '<span style="color: #990099;">' if $char eq '@';
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
	if ($perms & 040000) {
		$modestr .= 'drwxrwxr-x';
	} else {
		# git cares only about the executable bit
		if ($perms & 0100) {
			$modestr .= '-rwxrwxr-x';
		} else {
			$modestr .= '-rw-rw-r--';
		};
	}
	return $modestr;
}

sub date_str {
	my $epoch = shift;
	my $tz = shift || "-0000";

	my %date;
	my @months = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec");
	my @days = ("Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat");
	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($epoch);
	$date{'hour'} = $hour;
	$date{'minute'} = $min;
	$date{'mday'} = $mday;
	$date{'day'} = $days[$wday];
	$date{'month'} = $months[$mon];
	$date{'rfc2822'} = sprintf "%s, %d %s %4d %02d:%02d:%02d +0000", $days[$wday], $mday, $months[$mon], 1900+$year, $hour ,$min, $sec;
	$date{'mday-time'} = sprintf "%d %s %02d:%02d", $mday, $months[$mon], $hour ,$min;

	$tz =~ m/((-|\+)[0-9][0-9])([0-9][0-9])/;
	my $local = $epoch + (($1 + ($2/60)) * 3600);
	($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($local);
	$date{'hour_local'} = $hour;
	$date{'minute_local'} = $min;
	$date{'tz_local'} = $tz;
	return %date;
}

if ($action eq "git-logo.png") {
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
}

# show list of default projects
if ($project eq "") {
	opendir(my $fd, "$projectroot/$defaultprojects") || die_error("", "No projects found.");
	my (@users) = sort grep(!/^\./, readdir($fd));
	closedir($fd);
	git_header_html();
	print "<div class=\"page_body\">\n";
	print "<br/><br/>\n";
	foreach my $user (@users) {
		opendir($fd, "$projectroot/$defaultprojects/$user");
		my (@repos) = sort grep(/\.git$/, readdir($fd));
		closedir($fd);
		foreach my $repo (@repos) {
			if (-e "$projectroot/$defaultprojects/$user/$repo/HEAD") {
				print $cgi->a({-href => "$my_uri?p=$defaultprojects/$user/$repo;a=log"}, "$defaultprojects/$user/$repo") . "<br/>\n";
			}
		}
	}
	print "<br/></div>";
	git_footer_html();
	exit;
}

if (!defined($action)) {
	$action = "log";
}

if (!defined($time_back)) {
	$time_back = 1;
}

if ($action eq "blob") {
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div class=\"title\">$hash</div>\n";
	print "<div class=\"page_body\"><pre><br/><br/>\n";
	open(my $fd, "-|", "$gitbin/cat-file blob $hash");
	my $nr;
	while (my $line = <$fd>) {
		$nr++;
		printf "<span style =\"color: #999999;\">%4i\t</span>%s", $nr, escapeHTML($line);;
	}
	close $fd;
	print "<br/><br/></pre>\n";
	print "</div>";
	git_footer_html();
} elsif ($action eq "tree") {
	if ($hash eq "") {
		$hash = git_head($project);
	}
	open my $fd, "-|", "$gitbin/ls-tree $hash";
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd;
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div class=\"title\">$hash</div>\n";
	print "<div class=\"page_body\">\n";
	print "<br/><pre>\n";
	foreach my $line (@entries) {
		#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+)\t(.*)\t(.*)\t(.*)$/;
		my $t_mode = $1;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = $4;
		if ($t_type eq "blob") {
			print mode_str($t_mode). " $t_name (" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$t_hash"}, "view") . ")\n";
		} elsif ($t_type eq "tree") {
			print mode_str($t_mode). " $t_name (" . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$t_hash"}, "view") . ")\n";
		}
	}
	print "</pre>\n";
	print "<br/></div>";
	git_footer_html();
} elsif ($action eq "log" || $action eq "rss") {
	open my $fd, "-|", "$gitbin/rev-list " . git_head($project);
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	if ($action eq "log") {
		git_header_html();
		print "<div class=\"page_nav\">\n";
		print "view  ";
		print $cgi->a({-href => "$my_uri?p=$project;a=log"}, "last day") . " | \n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=log;t=7"}, "week") . " | \n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=log;t=31"}, "month") . " | \n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=log;t=365"}, "year") . " | \n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=log;t=0"}, "all") . "<br/>\n";
		print "<br/><br/>\n" .
		      "</div>\n";
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

	for (my $i = 0; $i <= $#revlist; $i++) {
		my $commit = $revlist[$i];
		my %co = git_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		if ($action eq "log") {
		if ($time_back > 0 && $co{'age'} > $time_back*60*60*24) {
				if ($i == 0) {
					print "<div class=\"page_body\"> Last change " . $co{'age_string'} . ".<br/><br/></div>\n";
				}
				last;
			}
			print "<div><a href=\"$my_uri?p=$project;a=commit;h=$commit\" class=\"log_title\">\n" .
			      "<span class=\"log_age\">" . $co{'age_string'} . "</span>\n" . escapeHTML($co{'title'}) . "</a>\n" .
			      "</div>\n";
			print "<div class=\"log_head\">\n" .
			      "<div class=\"log_functions\">\n" .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"}, "view commit") . "<br/>\n" .
			      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$commit"}, "view diff") . "<br/>\n" .
			      "</div>\n" .
			      escapeHTML($co{'author_name'}) .  " [" . $ad{'rfc2822'} . "]<br/>\n" .
			      "</div>\n" .
			      "<div class=\"log_body\">\n";
			my $comment = $co{'comment'};
			foreach my $line (@$comment) {
				if ($line =~ m/^(signed-off|acked)-by:/i) {
					print '<div class="signed_off">' . escapeHTML($line) . "<br/></div>\n";
				} else {
					print escapeHTML($line) . "<br/>\n";
				}
			}
			print "<br/><br/>\n" .
			      "</div>\n";
		} elsif ($action eq "rss") {
			last if ($i >= 20);
			print "<item>\n" .
			      "\t<title>" . sprintf("%d %s %02d:%02d", $ad{'mday'}, $ad{'month'}, $ad{'hour'}, $ad{'min'}) . " - " . escapeHTML($co{'title'}) . "</title>\n" .
			      "\t<link> " . $my_url . "?p=$project;a=commit;h=$commit</link>\n" .
			      "\t<description>";
			my $comment = $co{'comment'};
			foreach my $line (@$comment) {
				print escapeHTML($line) . "\n";
			}
			print "\t</description>\n" .
			      "</item>\n";
		}
	}
	if ($action eq "log") {
		git_footer_html();
	} elsif ($action eq "rss") {
		print "</channel></rss>";
	}
} elsif ($action eq "commit") {
	my %co = git_commit($hash);
	my %ad = date_str($co{'author_epoch'}, $co{'author_tz'});
	my %cd = date_str($co{'committer_epoch'}, $co{'committer_tz'});
	open my $fd, "-|", "$gitbin/diff-tree -r " . $co{'parent'} . " $hash";
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"page_nav\"> view\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "diffs") . "\n" .
	      "<br/><br/></div>\n";
	print "<a class=\"log_title\" href=\"$my_uri?p=$project;a=commitdiff;h=$hash\">$co{'title'}</a>\n";
	print "<div class=\"log_head\">\n";
	print "author &nbsp; &nbsp; &nbsp;" . escapeHTML($co{'author'}) . "<br/>\n";
	print "author-time " . $ad{'rfc2822'};
	if ($ad{'hour_local'} < 6) { print "<span style=\"color: #cc0000;\">"; }
	printf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	if ($ad{'hour_local'} < 6 ) { print "</span>"; }
	print "<br/>\n";
	print "committer &nbsp; " . escapeHTML($co{'committer'}) . "<br/>\n";
	print "commit-time " . $cd{'rfc2822'};
	printf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'});
	print "<br/>\n";
	print "commit &nbsp &nbsp; &nbsp;$hash<br/>\n";
	print "tree &nbsp; &nbsp; &nbsp; &nbsp" . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'}"}, $co{'tree'}) . "<br/>\n";
	my $parents  = $co{'parents'};
	foreach my $par (@$parents) {
		print "parent &nbsp; &nbsp &nbsp" . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$par"}, $par) . "<br/>\n";
	}
	print "</div>\n";
	print "<div class=\"page_body\">\n";
	my $comment = $co{'comment'};
	foreach my $line (@$comment) {
		if ($line =~ m/(signed-off|acked)-by:/i) {
			print '<div class="signed_off">' . escapeHTML($line) . "<br/></div>\n";
		} else {
			print escapeHTML($line) . "<br/>\n";
		}
	}
	print "<br/><br/>\n";
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
				print "$modestr $file" . "[new] " .
				      "(" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, "view") . ")\n";
			} elsif ($op eq "-") {
				print "$modestr $file" . "[removed] " .
				      "(" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, "view") . ")\n";
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $from = $1;
				my $to = $2;
				print "$modestr $file " .
				      "(" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to"}, "view") . ")" .
				      "(" . $cgi->a({-href => "$my_uri?p=$project;a=blobdiff;h=$to;hp=$from"}, "diff") . ")" .
				      "(" . $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash;f=$file"}, "history") . ")\n";
			}
		}
	}
	print "</pre>\n" .
	      "<br/></div>\n";
	git_footer_html();
} elsif ($action eq "blobdiff") {
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div class=\"title\">$hash vs $hash_parent</div>\n";
	print "<div class=\"page_body\"><br/><br/>\n" .
	      "<pre>\n";
	git_diff_html($hash_parent, $hash, $hash_parent, $hash);
	print "</pre>\n" .
	      "<br/></div>";
	git_footer_html();
} elsif ($action eq "commitdiff") {
	my %co = git_commit($hash);
	open my $fd, "-|", "$gitbin/diff-tree -r " . $co{'parent'} . " $hash";
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"page_nav\"> view\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "diffs") . "\n" .
	      "<br/><br/></div>\n";
	print $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "log_title"}, $co{'title'}) ."\n";
	print "<div class=\"page_body\">\n" .
	      "<pre>\n";
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
	print "<br/></pre>\n";
	print "</div>";
	git_footer_html();
} elsif ($action eq "history") {
	if (!(defined($hash))) {
		$hash = git_head($project);
	}
	open my $fd, "-|", "$gitbin/rev-list $hash";
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div class=\"title\">$file_name</div>\n";
	print "<div class=\"page_body\">\n" .
	      "<pre>\n";
	foreach my $rev (@revlist) {
		my %co = git_commit($rev);
		my $parents  = $co{'parents'};
		my $found = 0;
		foreach my $parent (@$parents) {
			open $fd, "-|", "$gitbin/diff-tree -r $parent $rev $file_name";
			my (@difftree) = map { chomp; $_ } <$fd>;
			close $fd;

			foreach my $line (@difftree) {
				$line =~ m/^(.)(.*)\t(.*)\t(.*)\t(.*)$/;
				my $file = $5;
				if ($file eq $file_name) {
					$found = 1;
					last;
				}
			}
		}
		if ($found) {
			print $co{'age_string'} . "\t " . $co{'author_name'} . "  - " . $co{'title'} .
			      " (" . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$rev"}, "view") .")\n";
		}
	}
	print "<br/></pre>\n";
	print "</div>";
	git_footer_html();
} else {
	die_error("", "unknown action");
}
