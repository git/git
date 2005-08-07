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

# begin config
my $projectroot =	"/pub/scm";
$projectroot =	"/home/kay/public_html/pub/scm";
my $home_link =		"/git";
$home_link =		"/~kay/git";
my $gitbin =		"/usr/bin";
my $gittmp =		"/tmp/gitweb";
my $logo_link =		"/pub/software/scm/cogito";
$logo_link =		"/~kay/pub/software/scm/cogito";
# end config

my $version =		"089";
my $my_url =		$cgi->url();
my $my_uri =		$cgi->url(-absolute => 1);
my $rss_link = "";

my $project = $cgi->param('p');
if (defined($project)) {
	if ($project =~ /(^|\/)(|\.|\.\.)($|\/)/) {
		$project = "";
		die_error("", "Invalid project parameter.");
	}
	if (!(-d "$projectroot/$project")) {
		$project = "";
		die_error("", "No such project.");
	}
	$rss_link = "<link rel=\"alternate\" title=\"$project log\" href=\"$my_uri?p=$project;a=rss\" type=\"application/rss+xml\"/>";
	$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$project/objects";
}

my $file_name = $cgi->param('f');
if (defined($file_name) && $file_name =~ /(^|\/)(|\.|\.\.)($|\/)/) {
	$file_name = "";
	die_error("", "Invalid file parameter.");
}

my $action = $cgi->param('a');
if (defined($action) && $action =~ m/[^0-9a-zA-Z\.\-]+$/) {
	$action = "";
	die_error("", "Invalid action parameter.");
}

my $hash = $cgi->param('h');
if (defined($hash) && !($hash =~ m/^[0-9a-fA-F]{40}$/)) {
	$hash = "";
	die_error("", "Invalid hash parameter.");
}

my $hash_parent = $cgi->param('hp');
if (defined($hash_parent) && !($hash_parent =~ m/^[0-9a-fA-F]{40}$/)) {
	$hash_parent = "";
	die_error("", "Invalid parent hash parameter.");
}

my $time_back = $cgi->param('t');
if (defined($time_back) && !($time_back =~ m/^[0-9]+$/)) {
	$time_back = "";
	die_error("", "Invalid time parameter.");
}

mkdir($gittmp, 0700);

sub git_header_html {
	my $status = shift || "200 OK";

	print $cgi->header(-type=>'text/html',  -charset => 'utf-8', -status=> $status);
	print <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US" lang="en-US">
<!-- git web interface v$version, (C) 2005, Kay Sievers <kay.sievers\@vrfy.org>, Christian Gierke <ch\@gierke.de> -->
<head>
<title>git - $project</title>
$rss_link
<style type="text/css">
body { font-family: sans-serif; font-size: 12px; margin:0px; }
a { color:#0000cc; }
a:hover { color:#880000; }
a:visited { color:#880000; }
a:active { color:#880000; }
div.page_header\{
	margin:15px 25px 0px; height:25px; padding:8px;
	font-size:18px; font-weight:bold; background-color:#d9d8d1;
}
div.page_header a:visited { color:#0000cc; }
div.page_header a:hover { color:#880000; }
div.page_nav { margin:0px 25px; padding:8px; border:solid #d9d8d1; border-width:0px 1px; }
div.page_nav a:visited { color:#0000cc; }
div.page_footer { margin:0px 25px 15px; height:17px; padding:4px; padding-left:8px; background-color: #d9d8d1; }
div.page_footer_text { float:left; color:#555555; font-style:italic; }
div.page_body { margin:0px 25px; padding:8px; border:solid #d9d8d1; border-width:0px 1px; }
div.title, a.title {
	display:block; margin:0px 25px; padding:6px 8px;
	font-weight:bold; background-color:#edece6; text-decoration:none; color:#000000;
}
a.title:hover { background-color: #d9d8d1; }
div.title_text { margin:0px 25px; padding:6px 8px; border: solid #d9d8d1; border-width:0px 1px 1px; }
div.log_body { margin:0px 25px; padding:8px; padding-left:150px; border:solid #d9d8d1; border-width:0px 1px; }
span.log_age { position:relative; float:left; width:142px; font-style:italic; }
div.log_link { font-size:10px; font-family:sans-serif; font-style:normal; position:relative; float:left; width:142px; }
div.list {
	display:block; margin:0px 25px; padding:4px 6px 2px; border:solid #d9d8d1; border-width:1px 1px 0px;
	font-weight:bold;
}
div.list_head {
	display:block; margin:0px 25px; padding:4px 6px 4px; border:solid #d9d8d1; border-width:1px 1px 0px;
	font-style:italic;
}
div.list a { text-decoration:none; color:#000000; }
div.list a:hover { color:#880000; }
div.link {
	margin:0px 25px; padding:0px 6px 8px; border:solid #d9d8d1; border-width:0px 1px;
	font-family:sans-serif; font-size:10px;
}
td.key { padding-right:10px;  }
span.diff_info { color:#000099; background-color:#eeeeee; font-style:italic; }
a.rss_logo { float:right; border:1px solid;
	line-height:15px;
	border-color:#fcc7a5 #7d3302 #3e1a01 #ff954e; width:35px;
	color:#ffffff; background-color:#ff6600;
	font-weight:bold; font-family:sans-serif; text-align:center;
	font-size:10px; display:block; text-decoration:none;
}
a.rss_logo:hover { background-color:#ee5500; }
</style>
</head>
<body>
EOF
	print "<div class=\"page_header\">\n" .
	      "<a href=\"$logo_link\">" .
	      "<img src=\"$my_uri?a=git-logo.png\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/></a>";
	print $cgi->a({-href => "$my_uri"}, "projects") . " / ";
	if ($project ne "") {
		print $cgi->a({-href => "$my_uri?p=$project;a=log"}, escapeHTML($project));
	}
	if ($action ne "") {
		print " / $action";
	}
	print "</div>\n";
}

sub git_footer_html {
	print "<div class=\"page_footer\">\n";
	if ($project ne "") {
		if (-e "$projectroot/$project/description") {
			open(my $fd, "$projectroot/$project/description");
			my $descr = <$fd>;
			print "<div class=\"page_footer_text\">" . escapeHTML($descr) . "</div>\n";
			close $fd;
		}
		print $cgi->a({-href => "$my_uri?p=$project;a=rss", -class => "rss_logo"}, "RSS") . "\n";
	}
	print "</div>\n" .
	      "</body>\n" .
	      "</html>";
}

sub die_error {
	my $status = shift || "403 Forbidden";
	my $error = shift || "Malformed query, file missing or permission denied"; 

	git_header_html($status);
	print "<div class=\"page_body\">\n" .
	      "<br/><br/>\n";
	print "$status - $error\n";
	print "<br/></div>\n";
	git_footer_html();
	exit 0;
}

sub git_head {
	my $path = shift;
	open(my $fd, "$projectroot/$path/HEAD") || die_error("", "Invalid project directory.");
	my $head = <$fd>;
	close $fd;
	chomp $head;
	return $head;
}

sub git_commit {
	my $commit = shift;
	my %co;
	my @parents;

	open my $fd, "-|", "$gitbin/git-cat-file commit $commit";
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
	if (!defined($co{'tree'})) {
		return;
	}
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
	my $from = shift;
	my $from_name = shift;
	my $to = shift;
	my $to_name = shift;

	my $from_tmp = "/dev/null";
	my $to_tmp = "/dev/null";
	my $pid = $$;

	# create tmp from-file
	if ($from ne "") {
		$from_tmp = "$gittmp/gitweb_" . $$ . "_from";
		open(my $fd2, "> $from_tmp");
		open my $fd, "-|", "$gitbin/git-cat-file blob $from";
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	# create tmp to-file
	if ($to ne "") {
		$to_tmp = "$gittmp/gitweb_" . $$ . "_to";
		open my $fd2, "> $to_tmp";
		open my $fd, "-|", "$gitbin/git-cat-file blob $to";
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	open my $fd, "-|", "/usr/bin/diff -u -p -L $from_name -L $to_name $from_tmp $to_tmp";
	while (my $line = <$fd>) {
		my $char = substr($line,0,1);
		# skip errors
		next if $char eq '\\';
		# color the diff
		print '<span style="color: #008800;">' if $char eq '+';
		print '<span style="color: #CC0000;">' if $char eq '-';
		print '<span style="color: #990099;">' if $char eq '@';
		print escapeHTML($line);
		print '</span>' if $char eq '+' or $char eq '-' or $char eq '@';
	}
	close $fd;

	if ($from ne "") {
		unlink($from_tmp);
	}
	if ($to ne "") {
		unlink($to_tmp);
	}
}

sub mode_str {
	my $mode = oct shift;

	my $modestr;
	if (($mode & 00170000) == 0040000 ) {
		$modestr = 'drwxr-xr-x';
	} elsif (($mode & 00170000) == 0120000 ) {
		$modestr = 'lrwxrwxrwx';
	} elsif (($mode & 00170000) == 0100000 ) {
		# git cares only about the executable bit
		if ($mode & 0100) {
			$modestr = '-rwxr-xr-x';
		} else {
			$modestr = '-rw-r--r--';
		};
	}
	return $modestr;
}

sub file_type {
	my $mode = oct shift;

	if (($mode & 0170000) == 0040000 ) {
		return "directory";
	} elsif (($mode & 0170000) == 0120000 ) {
		return "symlink";
	} elsif (($mode & 0170000) == 0100000 ) {
		if ($mode & 0100) {
			return "executable file";
		} else {
			return "file";
		}
	} else {
		return "unknown";
	}
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

	$tz =~ m/^([+\-][0-9][0-9])([0-9][0-9])$/;
	my $local = $epoch + ((int $1 + ($2/60)) * 3600);
	($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($local);
	$date{'hour_local'} = $hour;
	$date{'minute_local'} = $min;
	$date{'tz_local'} = $tz;
	return %date;
}

if (defined($action) && $action eq "git-logo.png") {
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

if (!defined($project)) {
	print $cgi->redirect($home_link);
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
	print "<div class=\"page_body\"><pre>\n";
	open(my $fd, "-|", "$gitbin/git-cat-file blob $hash");
	my $nr;
	while (my $line = <$fd>) {
		$nr++;
		printf "<span style =\"color: #999999;\">%4i\t</span>%s", $nr, escapeHTML($line);;
	}
	close $fd;
	print "<br/></pre>\n";
	print "</div>";
	git_footer_html();
} elsif ($action eq "tree") {
	if ($hash eq "") {
		$hash = git_head($project);
	}
	open my $fd, "-|", "$gitbin/git-ls-tree $hash";
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	my %co = git_commit($hash);
	if (%co) {
		print "<div class=\"page_nav\"> view\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | " .
		      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "diffs") . " | " .
		      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash"}, "tree") .
		      "<br/><br/>\n" .
		      "</div>\n";
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n";
		print "<br/><br/></div>\n";
		print "<div class=\"title\">$hash</div>\n";
	}
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
			print mode_str($t_mode). " " . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$t_hash"}, $t_name);
			if (((oct $t_mode) & 0170000) == 0120000) {
				open my $fd, "-|", "$gitbin/git-cat-file blob $t_hash";
				my $target = <$fd>;
				close $fd;
				print "\t -> $target";
			}
			print "\n";
		} elsif ($t_type eq "tree") {
			print mode_str($t_mode). " " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$t_hash"}, $t_name) . "\n";
		}
	}
	print "</pre>\n";
	print "<br/></div>";
	git_footer_html();
} elsif ($action eq "rss") {
	open my $fd, "-|", "$gitbin/git-rev-list --max-count=20 " . git_head($project);
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n".
	      "<rss version=\"0.91\">\n";
	print "<channel>\n";
	print "<title>$project</title>\n".
	      "<link> " . $my_url . "/$project/log</link>\n".
	      "<description>$project log</description>\n".
	      "<language>en</language>\n";

	foreach my $commit (@revlist) {
		my %co = git_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		print "<item>\n" .
		      "\t<title>" . sprintf("%d %s %02d:%02d", $ad{'mday'}, $ad{'month'}, $ad{'hour'}, $ad{'min'}) . " - " . escapeHTML($co{'title'}) . "</title>\n" .
		      "\t<link> " . $my_url . "?p=$project;a=commit;h=$commit</link>\n" .
		      "\t<description>";
		my $comment = $co{'comment'};
		foreach my $line (@$comment) {
			print escapeHTML($line) . "<br/>\n";
		}
		print "\t</description>\n" .
		      "</item>\n";
	}
	print "</channel></rss>";
} elsif ($action eq "log") {
	my $date = 0;
	if ($time_back > 0) {
		$date = time - $time_back*24*60*60;
	}
	my $head = git_head($project);
	open my $fd, "-|", "$gitbin/git-rev-list --max-age=$date $head";
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "view  ";
	print $cgi->a({-href => "$my_uri?p=$project;a=log"}, "last day") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=7"}, "week") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=31"}, "month") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=365"}, "year") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=0"}, "all") . "<br/>\n";
	print "<br/><br/>\n" .
	      "</div>\n";

	if (!(@revlist)) {
		my %co = git_commit($head);
		print "<div class=\"page_body\"> Last change " . $co{'age_string'} . ".<br/><br/></div>\n";
	}

	foreach my $commit (@revlist) {
		my %co = git_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit", -class => "title"}, 
		      "<span class=\"log_age\">" . $co{'age_string'} . "</span>" . escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
		print "<div class=\"title_text\">\n" .
		      "<div class=\"log_link\">\n" .
		      "view " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"}, "commit") . " | " .
		      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$commit"}, "diff") . "<br/>\n" .
		      "</div>\n" .
		      "<i>" . escapeHTML($co{'author_name'}) .  " [" . $ad{'rfc2822'} . "]</i><br/>\n" .
		      "</div>\n" .
		      "<div class=\"log_body\">\n";
		my $comment = $co{'comment'};
		foreach my $line (@$comment) {
			last if ($line =~ m/^(signed-off|acked)-by:/i);
				print escapeHTML($line) . "<br/>\n";
		}
		print "<br/>\n" .
		      "</div>\n";
	}
	git_footer_html();
} elsif ($action eq "commit") {
	my %co = git_commit($hash);
	if (!%co) {
		die_error("", "Unknown commit object.");
	}
	my %ad = date_str($co{'author_epoch'}, $co{'author_tz'});
	my %cd = date_str($co{'committer_epoch'}, $co{'committer_tz'});

	my @difftree;
	if (defined($co{'parent'})) {
		open my $fd, "-|", "$gitbin/git-diff-tree -r " . $co{'parent'} . " $hash";
		@difftree = map { chomp; $_ } <$fd>;
		close $fd;
	} else {
		# fake git-diff-tree output for initial revision
		open my $fd, "-|", "$gitbin/git-ls-tree -r $hash";
		@difftree = map { chomp;  "+" . $_ } <$fd>;
		close $fd;
	}
	git_header_html();
	print "<div class=\"page_nav\"> view\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "diffs") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash"}, "tree") . "\n" .
	      "<br/><br/></div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
	      "</div>\n";
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">";
	print "<tr><td class=\"key\">author</td><td>" . escapeHTML($co{'author'}) . "</td><tr><td></td><td>" .
	      " " . $ad{'rfc2822'};
	if ($ad{'hour_local'} < 6) {
		print "<span style=\"color: #cc0000;\">";
	}
	printf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	if ($ad{'hour_local'} < 6 ) {
		print "</span>";
	}
	print "</i>\n" .
	      "</td></tr>\n";
	print "<tr><td class=\"key\">committer</td><td>" . escapeHTML($co{'committer'}) . "</td><tr><td></td><td>" .
	      " " . $cd{'rfc2822'} . sprintf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'}) . "</i>\n" .
	      "</td></tr>\n";
	print "<tr><td class=\"key\">commit</td><td style=\"font-family: monospace;\">$hash</td></tr>\n";
	print "<tr><td class=\"key\">tree</td><td style=\"font-family: monospace;\">" . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash"}, $co{'tree'}) . "</td></tr>\n";
	my $parents  = $co{'parents'};
	foreach my $par (@$parents) {
		print "<tr><td class=\"key\">parent</td><td style=\"font-family: monospace;\">" . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$par"}, $par) . "</td></tr>\n";
	}
	print "</table></div>\n";
	print "<div class=\"page_body\">\n";
	my $comment = $co{'comment'};
	foreach my $line (@$comment) {
		if ($line =~ m/(signed-off|acked)-by:/i) {
			print "<span style=\"color: #888888\">" . escapeHTML($line) . "</span><br/>\n";
		} else {
			print escapeHTML($line) . "<br/>\n";
		}
	}
	print "</div>\n";
	if ($#difftree > 10) {
		print "<div class=\"list_head\">" . ($#difftree + 1) . " files changed:<br/></div>\n";
	}
	foreach my $line (@difftree) {
		# '*100644->100644	blob	9f91a116d91926df3ba936a80f020a6ab1084d2b->bb90a0c3a91eb52020d0db0e8b4f94d30e02d596	net/ipv4/route.c'
		# '+100644	blob	4a83ab6cd565d21ab0385bac6643826b83c2fcd4	arch/arm/lib/bitops.h'
		# '*100664->100644	blob	b1a8e3dd5556b61dd771d32307c6ee5d7150fa43->b1a8e3dd5556b61dd771d32307c6ee5d7150fa43	show-files.c'
		# '*100664->100644	blob	d08e895238bac36d8220586fdc28c27e1a7a76d3->d08e895238bac36d8220586fdc28c27e1a7a76d3	update-cache.c'
		$line =~ m/^(.)(.*)\t(.*)\t(.*)\t(.*)$/;
		my $op = $1;
		my $mode = $2;
		my $type = $3;
		my $id = $4;
		my $file = $5;
		my $mode_chng = "";
		if ($type eq "blob") {
			if ($op eq "+") {
				print "<div class=\"list\">\n" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"},
				      escapeHTML($file) . " <span style=\"color: #008000;\">[new " . file_type($mode) . "]</span>") . "\n" .
				      "</div>";
				print "<div class=\"link\">\n" .
				      "view " .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, "file") . "<br/>\n" .
				      "</div>\n";
			} elsif ($op eq "-") {
				print "<div class=\"list\">\n" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"},
				      escapeHTML($file) .  " <span style=\"color: #c00000;\">[deleted " . file_type($mode) . "]</span>") . "\n" .
				      "</div>";
				print "<div class=\"link\">\n" .
				      "view " .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, "file") . " | " .
				      $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash;f=$file"}, "history") . "<br/>\n" .
				      "</div>\n";
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $from_id = $1;
				my $to_id = $2;
				$mode =~ m/^([0-7]{6})->([0-7]{6})$/;
				my $from_mode = $1;
				my $to_mode = $2;
				my $mode_chnge = "";
				if (((oct $from_mode) & 0170100) != ((oct $to_mode) & 0170100)) {
					$mode_chnge = " <span style=\"color: #888888;\">[changed from " . file_type($from_mode) . " to " . file_type($to_mode) . "]</span>\n";
				}
				print "<div class=\"list\">\n";
				if ($to_id ne $from_id) {
					print $cgi->a({-href => "$my_uri?p=$project;a=blobdiff;h=$to_id;hp=$from_id"},
					      escapeHTML($file) . $mode_chnge) . "\n" .
					      "</div>\n";
				} else {
					print $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to_id"},
					      escapeHTML($file) . $mode_chnge) . "\n" .
					      "</div>\n";
				}
				print "<div class=\"link\">\n" .
				      "view ";
				if ($to_id ne $from_id) {
					print $cgi->a({-href => "$my_uri?p=$project;a=blobdiff;h=$to_id;hp=$from_id"}, "diff") . " | ";
				}
				print $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to_id"}, "file") . " | " .
				      $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash;f=$file"}, "history") . "<br/>\n" .
				      "</div>\n";
			}
		}
	}
	git_footer_html();
} elsif ($action eq "blobdiff") {
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div class=\"title\">$hash vs $hash_parent</div>\n";
	print "<div class=\"page_body\">\n" .
	      "<pre>\n";
	print "<span class=\"diff_info\">blob:" .
	      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$hash_parent"}, $hash_parent) .
	      " -> blob:" .
	      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$hash"}, $hash) .
	      "</span>\n";
	git_diff_html($hash_parent, $hash_parent, $hash, $hash);
	print "</pre>\n" .
	      "<br/></div>";
	git_footer_html();
} elsif ($action eq "commitdiff") {
	my %co = git_commit($hash);
	if (!%co) {
		die_error("", "Unknown commit object.");
	}
	open my $fd, "-|", "$gitbin/git-diff-tree -r " . $co{'parent'} . " $hash";
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"page_nav\"> view\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "diffs") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash"}, "tree") . "\n" .
	      "<br/><br/></div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
	      "</div>\n";
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
				print "<span class=\"diff_info\">new " .  file_type($mode) . ":" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, $id) .
				      "</span>\n";
				git_diff_html("", "/dev/null", $id, "b/$file");
			} elsif ($op eq "-") {
				print "<span class=\"diff_info\">deleted " . file_type($mode) . ":" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, $id) .
				      "</span>\n";
				git_diff_html($id, "a/$file", "", "/dev/null");
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $from_id = $1;
				my $to_id = $2;
				$mode =~ m/([0-7]+)->([0-7]+)/;
				my $from_mode = $1;
				my $to_mode = $2;
				if ($from_id ne $to_id) {
					print "<span class=\"diff_info\">" .
					      file_type($from_mode) . ":" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$from_id"}, $from_id) .
					      " -> " .
					      file_type($to_mode) . ":" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to_id"}, $to_id);
					print "</span>\n";
					git_diff_html($from_id, "a/$file",  $to_id, "b/$file");
				}
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
	open my $fd, "-|", "$gitbin/git-rev-list $hash";
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "title"}, escapeHTML($file_name)) . "\n" .
	      "</div>\n";
	foreach my $rev (@revlist) {
		my %co = git_commit($rev);
		my $parents  = $co{'parents'};
		my $found = 0;
		foreach my $parent (@$parents) {
			open $fd, "-|", "$gitbin/git-diff-tree -r $parent $rev $file_name";
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
			print "<div class=\"list\">\n" .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$rev"},
			      "<span class=\"log_age\">" . $co{'age_string'} . "</span>" . escapeHTML($co{'title'})) . "\n" .
			      "</div>\n";
			print "<div class=\"link\">\n" .
			      "view " .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$rev"}, "commit") . " | " .
			      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$rev"}, "tree") . "<br/><br/>\n" .
			      "</div>\n";
		}
	}
	git_footer_html();
} else {
	$action = "";
	die_error("", "Unknown action.");
}
