#!/usr/bin/perl

# gitweb.pl - simple web interface to track changes in git repositories
#
# (C) 2005, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke <ch@gierke.de>
#
# This program is licensed under the GPL v2, or a later version

use strict;
use warnings;
use CGI qw(:standard :escapeHTML);
use CGI::Carp qw(fatalsToBrowser);
use Fcntl ':mode';

my $cgi = new CGI;
my $version =		"107";
my $my_url =		$cgi->url();
my $my_uri =		$cgi->url(-absolute => 1);
my $rss_link = "";

# absolute fs-path which will be prepended to the project path
my $projectroot =	"/pub/scm";

# location of the git-core binaries
my $gitbin =		"/usr/bin";

# location for temporary files needed for diffs
my $gittmp =		"/tmp/gitweb";

# target of the home link on top of all pages
my $home_link =		$my_uri;
$home_link =		"/git";

# handler to return the list of projects
sub get_projects_list {
	my @list;

	# search in directory
#	my $dir = $projectroot;
#	opendir my $dh, $dir || return undef;
#	while (my $dir = readdir($dh)) {
#		if (-e "$projectroot/$dir/HEAD") {
#			push @list, $dir;
#		}
#	}
#	closedir($dh);

	# read from file
	my $file = "index/index.txt";
	open my $fd , $file || return undef;
	while (my $line = <$fd>) {
		chomp $line;
		if (-e "$projectroot/$line/HEAD") {
			push @list, $line;
		}
	}
	close $fd;

	@list = sort @list;
	return \@list;
}

# input validation
my $project = $cgi->param('p');
if (defined $project) {
	if ($project =~ m/(^|\/)(|\.|\.\.)($|\/)/) {
		undef $project;
		die_error("", "Non-canonical project parameter.");
	}
	if ($project =~ m/[^a-zA-Z0-9_\.\/\-\+\#\~]/) {
		undef $project;
		die_error("", "Invalid character in project parameter.");
	}
	if (!(-d "$projectroot/$project")) {
		undef $project;
		die_error("", "No such directory.");
	}
	if (!(-e "$projectroot/$project/HEAD")) {
		undef $project;
		die_error("", "No such project.");
	}
	$rss_link = "<link rel=\"alternate\" title=\"$project log\" href=\"$my_uri?p=$project;a=rss\" type=\"application/rss+xml\"/>";
	$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$project/objects";
}

my $file_name = $cgi->param('f');
if (defined $file_name) {
	if ($file_name =~ m/(^|\/)(|\.|\.\.)($|\/)/) {
		undef $file_name;
		die_error("", "Non-canonical file parameter.");
	}
	if ($file_name =~ m/[^a-zA-Z0-9_\.\/\-\+\#\~]/) {
		undef $file_name;
		die_error("", "Invalid character in file parameter.");
	}
}

my $action = $cgi->param('a');
if (defined $action) {
	if ($action =~ m/[^0-9a-zA-Z\.\-]+/) {
		undef $action;
		die_error("", "Invalid action parameter.");
	}
} else {
	$action = "log";
}

my $hash = $cgi->param('h');
if (defined $hash && !($hash =~ m/^[0-9a-fA-F]{40}$/)) {
	undef $hash;
	die_error("", "Invalid hash parameter.");
}

my $hash_parent = $cgi->param('hp');
if (defined $hash_parent && !($hash_parent =~ m/^[0-9a-fA-F]{40}$/)) {
	undef $hash_parent;
	die_error("", "Invalid parent hash parameter.");
}

my $time_back = $cgi->param('t');
if (defined $time_back) {
	if ($time_back =~ m/^[^0-9]+$/) {
		undef $time_back;
		die_error("", "Invalid time parameter.");
	}
}

sub git_header_html {
	my $status = shift || "200 OK";

	my $title = "git";
	if (defined $project) {
		$title .= " - $project";
		if (defined $action) {
			$title .= "/$action";
		}
	}
	print $cgi->header(-type=>'text/html',  -charset => 'utf-8', -status=> $status);
	print <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US" lang="en-US">
<!-- git web interface v$version, (C) 2005, Kay Sievers <kay.sievers\@vrfy.org>, Christian Gierke <ch\@gierke.de> -->
<head>
<title>$title</title>
$rss_link
<style type="text/css">
body { font-family: sans-serif; font-size: 12px; margin:0px; }
a { color:#0000cc; }
a:hover { color:#880000; }
a:visited { color:#880000; }
a:active { color:#880000; }
div.page_header {
	margin:15px 15px 0px; height:25px; padding:8px;
	font-size:18px; font-weight:bold; background-color:#d9d8d1;
}
div.page_header a:visited { color:#0000cc; }
div.page_header a:hover { color:#880000; }
div.page_nav { margin:0px 15px; padding:8px; border:solid #d9d8d1; border-width:0px 1px; }
div.page_nav a:visited { color:#0000cc; }
div.page_footer { margin:0px 15px 15px; height:17px; padding:4px; padding-left:8px; background-color: #d9d8d1; }
div.page_footer_text { float:left; color:#555555; font-style:italic; }
div.page_body { margin:0px 15px; padding:8px; border:solid #d9d8d1; border-width:0px 1px; }
div.title, a.title {
	display:block; margin:0px 15px; padding:6px 8px;
	font-weight:bold; background-color:#edece6; text-decoration:none; color:#000000;
}
a.title:hover { background-color: #d9d8d1; }
div.title_text { margin:0px 15px; padding:6px 8px; border: solid #d9d8d1; border-width:0px 1px 1px; }
div.log_body { margin:0px 15px; padding:8px; padding-left:150px; border:solid #d9d8d1; border-width:0px 1px; }
span.log_age { position:relative; float:left; width:142px; font-style:italic; }
div.log_link { font-size:10px; font-family:sans-serif; font-style:normal; position:relative; float:left; width:142px; }
div.list {
	display:block; margin:0px 15px; padding:4px 6px 2px; border:solid #d9d8d1; border-width:1px 1px 0px;
	font-weight:bold;
}
div.list_head {
	display:block; margin:0px 15px; padding:4px 6px 4px; border:solid #d9d8d1; border-width:1px 1px 0px;
	font-style:italic;
}
div.list a { text-decoration:none; color:#000000; }
div.list a:hover { color:#880000; }
div.link {
	margin:0px 15px; padding:0px 6px 8px; border:solid #d9d8d1; border-width:0px 1px;
	font-family:sans-serif; font-size:10px;
}
td { padding:5px 15px 0px 0px; font-size:12px; }
th { padding-right:10px; font-size:12px; text-align:left; }
span.diff_info { color:#000099; background-color:#edece6; font-style:italic; }
a.rss_logo { float:right; border:1px solid; line-height:15px;
	border-color:#fcc7a5 #7d3302 #3e1a01 #ff954e; width:35px;
	color:#ffffff; background-color:#ff6600;
	font-weight:bold; font-family:sans-serif; text-align:center; vertical-align:middle;
	font-size:10px; display:block; text-decoration:none;
}
a.rss_logo:hover { background-color:#ee5500; }
</style>
</head>
<body>
EOF
	print "<div class=\"page_header\">\n" .
	      "<a href=\"http://kernel.org/pub/software/scm/cogito\">" .
	      "<img src=\"$my_uri?a=git-logo.png\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/></a>";
	print $cgi->a({-href => $home_link}, "projects") . " / ";
	if (defined $project) {
		print $cgi->a({-href => "$my_uri?p=$project;a=log"}, escapeHTML($project));
		if (defined $action) {
			print " / $action";
		}
	}
	print "</div>\n";
}

sub git_footer_html {
	print "<div class=\"page_footer\">\n";
	if (defined $project) {
		my $descr = git_description($project);
		if (defined $descr) {
			print "<div class=\"page_footer_text\">" . escapeHTML($descr) . "</div>\n";
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
	open my $fd, "$projectroot/$path/HEAD" || return undef;
	my $head = <$fd>;
	close $fd;
	chomp $head;
	if ($head =~ m/^[0-9a-fA-F]{40}$/) {
		return $head;
	} else {
		return undef;
	}
}

sub git_description {
	my $path = shift;
	open my $fd, "$projectroot/$path/description" || return undef;
	my $descr = <$fd>;
	close $fd;
	chomp $descr;
	return $descr;
}

sub git_commit {
	my $commit = shift;
	my %co;
	my @parents;

	open my $fd, "-|", "$gitbin/git-cat-file commit $commit" || return;
	while (my $line = <$fd>) {
		last if $line eq "\n";
		chomp $line;
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
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];
	my (@comment) = map { chomp; $_ } <$fd>;
	$co{'comment'} = \@comment;
	$co{'title'} = $comment[0];
	close $fd || return;
	if (!defined $co{'tree'}) {
		return undef
	};

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
	} elsif ($age > 2) {
		$co{'age_string'} = int $age;
		$co{'age_string'} .= " seconds ago";
	} else {
		$co{'age_string'} .= " right now";
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
	if (defined $from) {
		$from_tmp = "$gittmp/gitweb_" . $$ . "_from";
		open my $fd2, "> $from_tmp";
		open my $fd, "-|", "$gitbin/git-cat-file blob $from";
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	# create tmp to-file
	if (defined $to) {
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

	if (defined $from) {
		unlink($from_tmp);
	}
	if (defined $to) {
		unlink($to_tmp);
	}
}

sub mode_str {
	my $mode = oct shift;

	if (S_ISDIR($mode & S_IFMT)) {
		return 'drwxr-xr-x';
	} elsif (S_ISLNK($mode)) {
		return 'lrwxrwxrwx';
	} elsif (S_ISREG($mode)) {
		# git cares only about the executable bit
		if ($mode & S_IXUSR) {
			return '-rwxr-xr-x';
		} else {
			return '-rw-r--r--';
		};
	} else {
		return '----------';
	}
}

sub file_type {
	my $mode = oct shift;

	if (S_ISDIR($mode & S_IFMT)) {
		return "directory";
	} elsif (S_ISLNK($mode)) {
		return "symlink";
	} elsif (S_ISREG($mode)) {
		return "file";
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

# git-logo (cached in browser for one day)
if (defined $action && $action eq "git-logo.png") {
	print $cgi->header(-type => 'image/png', -expires => '+1d');
	# cat git-logo.png | hexdump -e '16/1 " %02x"  "\n"' | sed 's/ /\\x/g'
	print	"\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52" .
		"\x00\x00\x00\x48\x00\x00\x00\x1b\x04\x03\x00\x00\x00\x2d\xd9\xd4" .
		"\x2d\x00\x00\x00\x18\x50\x4c\x54\x45\xff\xff\xff\x60\x60\x5d\xb0" .
		"\xaf\xaa\x00\x80\x00\xce\xcd\xc7\xc0\x00\x00\xe8\xe8\xe6\xf7\xf7" .
		"\xf6\x95\x0c\xa7\x47\x00\x00\x00\x73\x49\x44\x41\x54\x28\xcf\x63" .
		"\x48\x67\x20\x04\x4a\x5c\x18\x0a\x08\x2a\x62\x53\x61\x20\x02\x08" .
		"\x0d\x69\x45\xac\xa1\xa1\x01\x30\x0c\x93\x60\x36\x26\x52\x91\xb1" .
		"\x01\x11\xd6\xe1\x55\x64\x6c\x6c\xcc\x6c\x6c\x0c\xa2\x0c\x70\x2a" .
		"\x62\x06\x2a\xc1\x62\x1d\xb3\x01\x02\x53\xa4\x08\xe8\x00\x03\x18" .
		"\x26\x56\x11\xd4\xe1\x20\x97\x1b\xe0\xb4\x0e\x35\x24\x71\x29\x82" .
		"\x99\x30\xb8\x93\x0a\x11\xb9\x45\x88\xc1\x8d\xa0\xa2\x44\x21\x06" .
		"\x27\x41\x82\x40\x85\xc1\x45\x89\x20\x70\x01\x00\xa4\x3d\x21\xc5" .
		"\x12\x1c\x9a\xfe\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
	exit;
}

# project browser
if (!defined $project) {
	my $projects = get_projects_list();
	git_header_html();
	print "<div class=\"page_body\">\n";
	print "<table cellspacing=\"0\">\n";
	print "<tr>\n" .
	      "<th>Project</th>\n" .
	      "<th>Description</th>\n" .
	      "<th>Owner</th>\n" .
	      "<th>last change</th>\n" .
	      "</tr>\n" .
	      "<br/>";
	foreach my $proj (@$projects) {
		my $head = git_head($proj);
		if (!defined $head) {
			next;
		}
		$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$proj/objects";
		my %co = git_commit($head);
		if (!%co) {
			next;
		}
		my $descr = git_description($proj) || "";
		my $owner = "";
		my ($dev, $ino, $mode, $nlink, $st_uid, $st_gid, $rdev, $size) = stat("$projectroot/$proj");
		my ($name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell) = getpwuid($st_uid);
		if (defined $gcos) {
			$owner = $gcos;
			$owner =~ s/[,;].*$//;
		}
		print "<tr>\n" .
		      "<td>" . $cgi->a({-href => "$my_uri?p=$proj;a=log"}, escapeHTML($proj)) . "</td>\n" .
		      "<td>$descr</td>\n" .
		      "<td><i>$owner</i></td>\n";
		if ($co{'age'} < 60*60*2) {
			print "<td><span style =\"color: #009900;\"><b><i>" . $co{'age_string'} . "</i></b></span></td>\n";
		} elsif ($co{'age'} < 60*60*24*2) {
			print "<td><span style =\"color: #009900;\"><i>" . $co{'age_string'} . "</i></span></td>\n";
		} else {
			print "<td><i>" . $co{'age_string'} . "</i></td>\n";
		}
		print "</tr>\n";
		undef %co;
	}
	print "</table>\n" .
	      "<br/>\n" .
	      "</div>\n";
	git_footer_html();
	exit;
}

# action dispatch
if ($action eq "blob") {
	open my $fd, "-|", "$gitbin/git-cat-file blob $hash" || die_error("", "Open failed.");
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div class=\"title\">$hash</div>\n";
	print "<div class=\"page_body\"><pre>\n";
	my $nr;
	while (my $line = <$fd>) {
		$nr++;
		printf "<span style =\"color: #999999;\">%4i\t</span>%s", $nr, escapeHTML($line);;
	}
	close $fd || print "Reading blob failed.\n";
	print "</pre><br/>\n";
	print "</div>";
	git_footer_html();
} elsif ($action eq "tree") {
	if (!defined $hash) {
		$hash = git_head($project);
	}
	open my $fd, "-|", "$gitbin/git-ls-tree $hash" || die_error("", "Open failed.");
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd || die_error("", "Reading tree failed.");

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
			if (S_ISLNK(oct $t_mode)) {
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
	open my $fd, "-|", "$gitbin/git-rev-list --max-count=20 " . git_head($project) || die_error("", "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd || die_error("", "Reading rev-list failed.");

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
		undef %ad;
		undef %co;
	}
	print "</channel></rss>";
} elsif ($action eq "log") {
	my $head = git_head($project);
	my $limit_option = "";
	if (!defined $time_back) {
		$limit_option = "--max-count=10";
	} elsif ($time_back > 0) {
		my $date = time - $time_back*24*60*60;
		$limit_option = "--max-age=$date";
	}
	open my $fd, "-|", "$gitbin/git-rev-list $limit_option $head" || die_error("", "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd || die_error("", "Reading rev-list failed.");

	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "view  ";
	print $cgi->a({-href => "$my_uri?p=$project;a=log"}, "last 10") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=1"}, "day") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=7"}, "week") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=31"}, "month") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=365"}, "year") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=log;t=0"}, "all") . "<br/>\n";
	print "<br/><br/>\n" .
	      "</div>\n";

	if (!@revlist) {
		my %co = git_commit($head);
		print "<div class=\"page_body\"> Last change " . $co{'age_string'} . ".<br/><br/></div>\n";
	}

	foreach my $commit (@revlist) {
		my %co = git_commit($commit);
		next if !%co;
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
		undef %ad;
		undef %co;
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
	if (defined $co{'parent'}) {
		open my $fd, "-|", "$gitbin/git-diff-tree -r " . $co{'parent'} . " $hash" || die_error("", "Open failed.");
		@difftree = map { chomp; $_ } <$fd>;
		close $fd || die_error("", "Reading diff-tree failed.");
	} else {
		# fake git-diff-tree output for initial revision
		open my $fd, "-|", "$gitbin/git-ls-tree -r $hash" || die_error("", "Open failed.");
		@difftree = map { chomp;  "+" . $_ } <$fd>;
		close $fd || die_error("", "Reading ls-tree failed.");
	}
	git_header_html();
	print "<div class=\"page_nav\"> view\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "diffs") . " | \n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash"}, "tree") . "\n" .
	      "<br/><br/></div>\n";
	if (defined $co{'parent'}) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	} else {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	}
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">\n";
	print "<tr><td>author</td><td>" . escapeHTML($co{'author'}) . "</td></tr>\n".
	      "<tr><td></td><td> " . $ad{'rfc2822'};
	if ($ad{'hour_local'} < 6) {
		printf(" (<span style=\"color: #cc0000;\">%02d:%02d</span> %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	} else {
		printf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	}
	print "</td></tr>\n";
	print "<tr><td>committer</td><td>" . escapeHTML($co{'committer'}) . "</td></tr>\n";
	print "<tr><td></td><td> " . $cd{'rfc2822'} .
	      sprintf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'}) . "</td></tr>\n";
	print "<tr><td>commit</td><td style=\"font-family: monospace;\">$hash</td></tr>\n";
	print "<tr><td>tree</td><td style=\"font-family: monospace;\">" .
	      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$hash"}, $co{'tree'}) . "</td></tr>\n";
	my $parents  = $co{'parents'};
	foreach my $par (@$parents) {
		print "<tr><td>parent</td><td style=\"font-family: monospace;\">" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$par"}, $par) . "</td></tr>\n";
	}
	print "</table>". 
	      "</div>\n";
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
		if ($type eq "blob") {
			if ($op eq "+") {
				my $mode_chng = "";
				if (S_ISREG(oct $mode)) {
					$mode_chng = sprintf(" with mode: %04o", (oct $mode) & 0777);
				}
				print "<div class=\"list\">\n" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"},
				      escapeHTML($file) . " <span style=\"color: #008000;\">[new " . file_type($mode) . $mode_chng . "]</span>") . "\n" .
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
				if ($from_mode != $to_mode) {
					$mode_chnge = " <span style=\"color: #888888;\">[changed";
					if (((oct $from_mode) & S_IFMT) != ((oct $to_mode) & S_IFMT)) {
						$mode_chnge .= " from " . file_type($from_mode) . " to " . file_type($to_mode);
					}
					if (((oct $from_mode) & 0777) != ((oct $to_mode) & 0777)) {
						if (S_ISREG($from_mode) && S_ISREG($to_mode)) {
							$mode_chnge .= sprintf(" mode: %04o->%04o", (oct $from_mode) & 0777, (oct $to_mode) & 0777);
						} elsif (S_ISREG($to_mode)) {
							$mode_chnge .= sprintf(" mode: %04o", (oct $to_mode) & 0777);
						}
					}
					$mode_chnge .= "]</span>\n";
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
	mkdir($gittmp, 0700);
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
	mkdir($gittmp, 0700);
	my %co = git_commit($hash);
	if (!%co) {
		die_error("", "Unknown commit object.");
	}
	open my $fd, "-|", "$gitbin/git-diff-tree -r " . $co{'parent'} . " $hash" || die_error("", "Open failed.");
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd || die_error("", "Reading diff-tree failed.");

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
				print "<span class=\"diff_info\">" .  file_type($mode) . ":" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, $id) . "(new)" .
				      "</span>\n";
				git_diff_html(undef, "/dev/null", $id, "b/$file");
			} elsif ($op eq "-") {
				print "<span class=\"diff_info\">" . file_type($mode) . ":" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id"}, $id) . "(deleted)" .
				      "</span>\n";
				git_diff_html($id, "a/$file", undef, "/dev/null");
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
	print "</pre><br/>\n";
	print "</div>";
	git_footer_html();
} elsif ($action eq "history") {
	if (!defined $hash) {
		$hash = git_head($project);
	}
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print "<br/><br/></div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "title"}, escapeHTML($file_name)) . "\n" .
	      "</div>\n";
	open my $fd, "-|", "$gitbin/git-rev-list $hash | $gitbin/git-diff-tree -r --stdin $file_name";
	my $commit;
	while (my $line = <$fd>) {
		if ($line =~ m/^([0-9a-fA-F]{40}) /){
			$commit = $1;
			next;
		}
		if ($line =~ m/^(.)(.*)\t(.*)\t(.*)\t(.*)$/ && (defined $commit)) {
			my $type = $3;
			my $file = $5;
			if ($file ne $file_name || $type ne "blob") {
				next;
			}
			my %co = git_commit($commit);
			if (!%co) {
				next;
			}
			print "<div class=\"list\">\n" .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"},
			      "<span class=\"log_age\">" . $co{'age_string'} . "</span>" . escapeHTML($co{'title'})) . "\n" .
			      "</div>\n";
			print "<div class=\"link\">\n" .
			      "view " .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"}, "commit") . " | " .
			      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$commit"}, "tree") . "<br/><br/>\n" .
			      "</div>\n";
			undef %co;
			undef $commit;
		}
	}
	close $fd;
	git_footer_html();
} else {
	undef $action;
	die_error("", "Unknown action.");
}
