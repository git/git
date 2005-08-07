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
use CGI::Util qw(unescape);
use CGI::Carp qw(fatalsToBrowser);
use Fcntl ':mode';

my $cgi = new CGI;
my $version =		"165";
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

# html text to include at home page
my $home_text =		"indextext.html";

# source of projects list
#my $projects_list = $projectroot;
my $projects_list = "index/index.aux";

# input validation and dispatch
my $action = $cgi->param('a');
if (defined $action) {
	if ($action =~ m/[^0-9a-zA-Z\.\-]+/) {
		undef $action;
		die_error(undef, "Invalid action parameter.");
	}
	if ($action eq "git-logo.png") {
		git_logo();
		exit;
	}
} else {
	$action = "summary";
}

my $project = $cgi->param('p');
if (defined $project) {
	if ($project =~ m/(^|\/)(|\.|\.\.)($|\/)/) {
		undef $project;
		die_error(undef, "Non-canonical project parameter.");
	}
	if ($project =~ m/[^a-zA-Z0-9_\.\/\-\+\#\~]/) {
		undef $project;
		die_error(undef, "Invalid character in project parameter.");
	}
	if (!(-d "$projectroot/$project")) {
		undef $project;
		die_error(undef, "No such directory.");
	}
	if (!(-e "$projectroot/$project/HEAD")) {
		undef $project;
		die_error(undef, "No such project.");
	}
	$rss_link = "<link rel=\"alternate\" title=\"$project log\" href=\"$my_uri?p=$project;a=rss\" type=\"application/rss+xml\"/>";
	$ENV{'GIT_OBJECT_DIRECTORY'} = "$projectroot/$project/objects";
	$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$project/objects";
} else {
	git_project_list();
	exit;
}

my $file_name = $cgi->param('f');
if (defined $file_name) {
	if ($file_name =~ m/(^|\/)(|\.|\.\.)($|\/)/) {
		undef $file_name;
		die_error(undef, "Non-canonical file parameter.");
	}
	if ($file_name =~ m/[^a-zA-Z0-9_\.\/\-\+\#\~\:\!]/) {
		undef $file_name;
		die_error(undef, "Invalid character in file parameter.");
	}
}

my $hash = $cgi->param('h');
if (defined $hash && !($hash =~ m/^[0-9a-fA-F]{40}$/)) {
	undef $hash;
	die_error(undef, "Invalid hash parameter.");
}

my $hash_parent = $cgi->param('hp');
if (defined $hash_parent && !($hash_parent =~ m/^[0-9a-fA-F]{40}$/)) {
	undef $hash_parent;
	die_error(undef, "Invalid hash_parent parameter.");
}

my $hash_base = $cgi->param('hb');
if (defined $hash_base && !($hash_base =~ m/^[0-9a-fA-F]{40}$/)) {
	undef $hash_base;
	die_error(undef, "Invalid parent hash parameter.");
}

my $time_back = $cgi->param('t');
if (defined $time_back) {
	if ($time_back =~ m/^[^0-9]+$/) {
		undef $time_back;
		die_error(undef, "Invalid time parameter.");
	}
}

if ($action eq "summary") {
	git_summary();
	exit;
} elsif ($action eq "branches") {
	git_branches();
	exit;
} elsif ($action eq "tags") {
	git_tags();
	exit;
} elsif ($action eq "blob") {
	git_blob();
	exit;
} elsif ($action eq "tree") {
	git_tree();
	exit;
} elsif ($action eq "rss") {
	git_rss();
	exit;
} elsif ($action eq "commit") {
	git_commit();
	exit;
} elsif ($action eq "log") {
	git_log();
	exit;
} elsif ($action eq "blobdiff") {
	git_blobdiff();
	exit;
} elsif ($action eq "commitdiff") {
	git_commitdiff();
	exit;
} elsif ($action eq "history") {
	git_history();
	exit;
} else {
	undef $action;
	die_error(undef, "Unknown action.");
	exit;
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
body { font-family: sans-serif; font-size: 12px; margin:0px; border:solid #d9d8d1; border-width:1px; margin:10px; }
a { color:#0000cc; }
a:hover, a:visited, a:active { color:#880000; }
div.page_header { height:25px; padding:8px; font-size:18px; font-weight:bold; background-color:#d9d8d1; }
div.page_header a:visited { color:#0000cc; }
div.page_header a:hover { color:#880000; }
div.page_nav { padding:8px; }
div.page_nav a:visited { color:#0000cc; }
div.page_path { font-weight:bold; padding:8px; border:solid #d9d8d1; border-width:0px 0px 1px}
div.page_footer { height:17px; padding:4px 8px; background-color: #d9d8d1; }
div.page_footer_text { float:left; color:#555555; font-style:italic; }
div.page_body { padding:8px; }
div.title, a.title {
	display:block; padding:6px 8px;
	font-weight:bold; background-color:#edece6; text-decoration:none; color:#000000;
}
a.title:hover { background-color: #d9d8d1; }
div.title_text { padding:6px 0px; border: solid #d9d8d1; border-width:0px 0px 1px; }
div.log_body { padding:8px 8px 8px 150px; }
span.age { position:relative; float:left; width:142px; font-style:italic; }
div.log_link {
	padding:0px 8px;
	font-size:10px; font-family:sans-serif; font-style:normal;
	position:relative; float:left; width:136px;
}
div.list_head { padding:6px 8px 4px; border:solid #d9d8d1; border-width:1px 0px 0px; font-style:italic; }
a.list { text-decoration:none; color:#000000; }
a.list:hover { color:#880000; }
table { padding:8px 4px; }
th { padding:2px 5px; font-size:12px; text-align:left; }
td { padding:2px 5px; font-size:12px; }
td.link { padding:2px 5px; font-family:sans-serif; font-size:10px; }
div.pre { font-family:monospace; font-size:12px; white-space:pre; }
div.diff_info { font-family:monospace; color:#000099; background-color:#edece6; font-style:italic; }
div.index_include { border:solid #d9d8d1; border-width:0px 0px 1px; padding:12px 8px; }
a.rss_logo { float:right; padding:3px 0px; width:35px; line-height:10px;
	border:1px solid; border-color:#fcc7a5 #7d3302 #3e1a01 #ff954e;
	color:#ffffff; background-color:#ff6600;
	font-weight:bold; font-family:sans-serif; font-size:10px;
	text-align:center; text-decoration:none;
}
a.rss_logo:hover { background-color:#ee5500; }
</style>
</head>
<body>
EOF
	print "<div class=\"page_header\">\n" .
	      "<a href=\"http://kernel.org/pub/software/scm/git/docs/\">" .
	      "<img src=\"$my_uri?a=git-logo.png\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/></a>";
	print $cgi->a({-href => $home_link}, "projects") . " / ";
	if (defined $project) {
		print $cgi->a({-href => "$my_uri?p=$project;a=summary"}, escapeHTML($project));
		if (defined $action) {
			print " / $action";
		}
	}
	print "</div>\n";
}

sub git_footer_html {
	print "<div class=\"page_footer\">\n";
	if (defined $project) {
		my $descr = git_read_description($project);
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
	      "<br/><br/>\n" .
	      "$status - $error\n" .
	      "<br/>\n" .
	      "</div>\n";
	git_footer_html();
	exit;
}

sub git_get_type {
	my $hash = shift;

	open my $fd, "-|", "$gitbin/git-cat-file -t $hash" || return;
	my $type = <$fd>;
	close $fd;
	chomp $type;
	return $type;
}

sub git_read_hash {
	my $path = shift;

	open my $fd, "$projectroot/$path" || return undef;
	my $head = <$fd>;
	close $fd;
	chomp $head;
	if ($head =~ m/^[0-9a-fA-F]{40}$/) {
		return $head;
	}
}

sub git_read_description {
	my $path = shift;

	open my $fd, "$projectroot/$path/description" || return undef;
	my $descr = <$fd>;
	close $fd;
	chomp $descr;
	return $descr;
}

sub git_read_tag {
	my $tag_id = shift;
	my %tag;

	open my $fd, "-|", "$gitbin/git-cat-file tag $tag_id" || return;
	while (my $line = <$fd>) {
		chomp $line;
		if ($line =~ m/^object ([0-9a-fA-F]{40})$/) {
			$tag{'object'} = $1;
		} elsif ($line =~ m/^type (.+)$/) {
			$tag{'type'} = $1;
		} elsif ($line =~ m/^tag (.+)$/) {
			$tag{'name'} = $1;
		}
	}
	close $fd || return;
	if (!defined $tag{'name'}) {
		return
	};
	return %tag
}

sub git_read_commit {
	my $commit = shift;
	my %co;
	my @parents;

	open my $fd, "-|", "$gitbin/git-cat-file commit $commit" || return;
	while (my $line = <$fd>) {
		last if $line eq "\n";
		chomp $line;
		if ($line =~ m/^tree ([0-9a-fA-F]{40})$/) {
			$co{'tree'} = $1;
		} elsif ($line =~ m/^parent ([0-9a-fA-F]{40})$/) {
			push @parents, $1;
		} elsif ($line =~ m/^author (.*) ([0-9]+) (.*)$/) {
			$co{'author'} = $1;
			$co{'author_epoch'} = $2;
			$co{'author_tz'} = $3;
			if ($co{'author'} =~ m/^([^<]+) </) {
				$co{'author_name'} = $1;
			} else {
				$co{'author_name'} = $co{'author'};
			}
		} elsif ($line =~ m/^committer (.*) ([0-9]+) (.*)$/) {
			$co{'committer'} = $1;
			$co{'committer_epoch'} = $2;
			$co{'committer_tz'} = $3;
			$co{'committer_name'} = $co{'committer'};
			$co{'committer_name'} =~ s/ <.*//;
		}
	}
	if (!defined $co{'tree'}) {
		close $fd;
		return undef
	};
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];
	my (@comment) = map { chomp; $_ } <$fd>;
	$co{'comment'} = \@comment;
	$co{'title'} = chop_str($comment[0], 50);
	close $fd || return;

	my $age = time - $co{'committer_epoch'};
	$co{'age'} = $age;
	if ($age > 60*60*24*365*2) {
		$co{'age_string'} = (int $age/60/60/24/365);
		$co{'age_string'} .= " years ago";
	} elsif ($age > 60*60*24*(365/12)*2) {
		$co{'age_string'} = int $age/60/60/24/(365/12);
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
		$co{'age_string'} .= " min ago";
	} elsif ($age > 2) {
		$co{'age_string'} = int $age;
		$co{'age_string'} .= " sec ago";
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
		chomp($line);
		my $char = substr($line, 0, 1);
		my $color = "";
		if ($char eq '+') {
			$color = " style=\"color:#008800;\"";
		} elsif ($char eq '-') {
			$color = " style=\"color:#cc0000;\"";
		} elsif ($char eq '@') {
			$color = " style=\"color:#990099;\"";
		} elsif ($char eq '\\') {
			# skip errors
			next;
		}
		print "<div class=\"pre\"$color>" . escapeHTML($line) . "</div>\n";
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

sub chop_str {
	my $str = shift;
	my $len = shift;

	$str =~ m/^(.{0,$len}[^ \/\-_:\.@]{0,10})/;
	my $chopped = $1;
	if ($chopped ne $str) {
		$chopped .= " ...";
	}
	return $chopped;
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
sub git_logo {
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
}

sub get_file_owner {
	my $path = shift;

	my ($dev, $ino, $mode, $nlink, $st_uid, $st_gid, $rdev, $size) = stat($path);
	my ($name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell) = getpwuid($st_uid);
	if (!defined $gcos) {
		return undef;
	}
	my $owner = $gcos;
	$owner =~ s/[,;].*$//;
	return $owner;
}

sub git_project_list {
	my @list;

	if (-d $projects_list) {
		# search in directory
		my $dir = $projects_list;
		opendir my $dh, $dir || return undef;
		while (my $dir = readdir($dh)) {
			if (-e "$projectroot/$dir/HEAD") {
				my $pr = {
					path => $dir,
				};
				push @list, $pr
			}
		}
		closedir($dh);
	} elsif (-f $projects_list) {
		# read from file(url-encoded):
		# 'git%2Fgit.git Linus+Torvalds'
		# 'libs%2Fklibc%2Fklibc.git H.+Peter+Anvin'
		# 'linux%2Fhotplug%2Fudev.git Greg+Kroah-Hartman'
		open my $fd , $projects_list || return undef;
		while (my $line = <$fd>) {
			chomp $line;
			my ($path, $owner) = split ' ', $line;
			$path = unescape($path);
			$owner = unescape($owner);
			if (!defined $path) {
				next;
			}
			if (-e "$projectroot/$path/HEAD") {
				my $pr = {
					path => $path,
					owner => $owner,
				};
				push @list, $pr
			}
		}
		close $fd;
	}

	if (!@list) {
		die_error(undef, "No project found.");
	}
	@list = sort {$a->{'path'} cmp $b->{'path'}} @list;

	git_header_html();
	if (-f $home_text) {
		print "<div class=\"index_include\">\n";
		open (my $fd, $home_text);
		print <$fd>;
		close $fd;
		print "</div>\n";
	}
	print "<table cellspacing=\"0\">\n" .
	      "<tr>\n" .
	      "<th>Project</th>\n" .
	      "<th>Description</th>\n" .
	      "<th>Owner</th>\n" .
	      "<th>last change</th>\n" .
	      "<th></th>\n" .
	      "</tr>\n";
	my $alternate = 0;
	foreach my $pr (@list) {
		my %proj = %$pr;
		my $head = git_read_hash("$proj{'path'}/HEAD");
		if (!defined $head) {
			next;
		}
		$ENV{'GIT_OBJECT_DIRECTORY'} = "$projectroot/$proj{'path'}/objects";
		$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$proj{'path'}/objects";
		my %co = git_read_commit($head);
		if (!%co) {
			next;
		}
		my $descr = git_read_description($proj{'path'}) || "";
		$descr = chop_str($descr, 30);
		# get directory owner if not already specified
		if (!defined $proj{'owner'}) {
			$proj{'owner'} = get_file_owner("$projectroot/$proj{'path'}") || "";
		}
		if ($alternate) {
			print "<tr style=\"background-color:#f6f5ed\">\n";
		} else {
			print "<tr>\n";
		}
		$alternate ^= 1;
		print "<td>" . $cgi->a({-href => "$my_uri?p=$proj{'path'};a=summary", -class => "list"}, escapeHTML($proj{'path'})) . "</td>\n" .
		      "<td>$descr</td>\n" .
		      "<td><i>" . chop_str($proj{'owner'}, 20) . "</i></td>\n";
		my $colored_age;
		if ($co{'age'} < 60*60*2) {
			$colored_age = "<span style =\"color: #009900;\"><b><i>$co{'age_string'}</i></b></span>";
		} elsif ($co{'age'} < 60*60*24*2) {
			$colored_age = "<span style =\"color: #009900;\"><i>$co{'age_string'}</i></span>";
		} else {
			$colored_age = "<i>$co{'age_string'}</i>";
		}
		print "<td>$colored_age</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => "$my_uri?p=$proj{'path'};a=summary"}, "summary") .
		      " | " . $cgi->a({-href => "$my_uri?p=$proj{'path'};a=log"}, "log") .
		      "</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
	git_footer_html();
}

sub git_read_refs {
	my $ref_dir = shift;
	my @reflist;

	opendir my $dh, "$projectroot/$project/$ref_dir";
	my @refs = grep !m/^\./, readdir $dh;
	closedir($dh);
	foreach my $ref_file (@refs) {
		my $ref_id = git_read_hash("$project/$ref_dir/$ref_file");
		my $type = git_get_type($ref_id) || next;
		my %ref_item;
		my %co;
		if ($type eq "tag") {
			my %tag = git_read_tag($ref_id);
			if ($tag{'type'} eq "commit") {
				%co = git_read_commit($tag{'object'});
			}
			$ref_item{'type'} = $tag{'type'};
			$ref_item{'name'} = $tag{'name'};
			$ref_item{'id'} = $tag{'object'};
		} elsif ($type eq "commit"){
			%co = git_read_commit($ref_id);
			$ref_item{'type'} = "commit";
			$ref_item{'name'} = $ref_file;
			$ref_item{'title'} = $co{'title'};
			$ref_item{'id'} = $ref_id;
		}
		$ref_item{'epoch'} = $co{'committer_epoch'} || 0;
		$ref_item{'age'} = $co{'age_string'} || "unknown";

		push @reflist, \%ref_item;
	}
	# sort tags by age
	@reflist = sort {$b->{'epoch'} <=> $a->{'epoch'}} @reflist;
	return \@reflist;
}

sub git_summary {
	my $descr = git_read_description($project) || "none";
	my $head = git_read_hash("$project/HEAD");
	$ENV{'GIT_OBJECT_DIRECTORY'} = "$projectroot/$project/objects";
	$ENV{'SHA1_FILE_DIRECTORY'} = "$projectroot/$project/objects";
	my %co = git_read_commit($head);
	my %cd = date_str($co{'committer_epoch'}, $co{'committer_tz'});

	my $owner;
	if (-f $projects_list) {
		open (my $fd , $projects_list);
		while (my $line = <$fd>) {
			chomp $line;
			my ($pr, $ow) = split ' ', $line;
			$pr = unescape($pr);
			$ow = unescape($ow);
			if ($pr eq $project) {
				$owner = $ow;
				last;
			}
		}
		close $fd;
	}
	if (!defined $owner) {
		$owner = get_file_owner("$projectroot/$project");
	}

	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree"}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";
	print "<div class=\"title\">project</div>\n";
	print "<table cellspacing=\"0\">\n" .
	      "<tr><td>description</td><td>" . escapeHTML($descr) . "</td></tr>\n" .
	      "<tr><td>owner</td><td>$owner</td></tr>\n" .
	      "<tr><td>last change</td><td>$cd{'rfc2822'}</td></tr>\n" .
	      "</table>\n";
	open my $fd, "-|", "$gitbin/git-rev-list --max-count=16 " . git_read_hash("$project/HEAD") || die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=log", -class => "title"}, "commits") .
	      "</div>\n";
	my $i = 15;
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $commit (@revlist) {
		my %co = git_read_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		if ($alternate) {
			print "<tr style=\"background-color:#f6f5ed\">\n";
		} else {
			print "<tr>\n";
		}
		$alternate ^= 1;
		if (--$i > 0) {
			print "<td><i>$co{'age_string'}</i></td>\n" .
			      "<td><i>" . escapeHTML(chop_str($co{'author_name'}, 10)) . "</i></td>\n" .
			      "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit", -class => "list"}, "<b>" . escapeHTML($co{'title'}) . "</b>") . "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"}, "commit") .
			      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$commit"}, "commitdiff") .
			      "</td>\n" .
			      "</tr>";
		} else {
			print "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=log"}, "...") . "</td>\n" .
			"</tr>";
			last;
		}
	}
	print "</table\n>";

	my $taglist = git_read_refs("refs/tags");
	if (defined @$taglist) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=tags", -class => "title"}, "tags") .
		      "</div>\n";
		my $i = 15;
		print "<table cellspacing=\"0\">\n";
		my $alternate = 0;
		foreach my $entry (@$taglist) {
			my %tag = %$entry;
			if ($alternate) {
				print "<tr style=\"background-color:#f6f5ed\">\n";
			} else {
				print "<tr>\n";
			}
			$alternate ^= 1;
			if (--$i > 0) {
				print "<td><i>$tag{'age'}</i></td>\n" .
				      "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=$tag{'type'};h=$tag{'id'}", -class => "list"}, "<b>" . escapeHTML($tag{'name'}) . "</b>") . "</td>\n" .
				      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?p=$project;a=$tag{'type'};h=$tag{'id'}"}, $tag{'type'}) . "</td>\n" .
				      "</tr>";
			} else {
				print "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=tags"}, "...") . "</td>\n" .
				"</tr>";
				last;
			}
		}
		print "</table\n>";
	}

	my $branchlist = git_read_refs("refs/heads");
	if (defined @$branchlist) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=branches", -class => "title"}, "branches") .
		      "</div>\n";
		my $i = 15;
		print "<table cellspacing=\"0\">\n";
		my $alternate = 0;
		foreach my $entry (@$branchlist) {
			my %tag = %$entry;
			if ($alternate) {
				print "<tr style=\"background-color:#f6f5ed\">\n";
			} else {
				print "<tr>\n";
			}
			$alternate ^= 1;
			if (--$i > 0) {
				print "<td><i>$tag{'age'}</i></td>\n" .
				      "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=log;h=$tag{'id'}", -class => "list"}, "<b>" . escapeHTML($tag{'name'}) . "</b>") . "</td>\n" .
				      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?p=$project;a=log;h=$tag{'id'}"}, "log") . "</td>\n" .
				      "</tr>";
			} else {
				print "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=branches"}, "...") . "</td>\n" .
				"</tr>";
				last;
			}
		}
		print "</table\n>";
	}
	git_footer_html();
}

sub git_tags {
	my $head = git_read_hash("$project/HEAD");
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$head"}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree"}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";
	my $taglist = git_read_refs("refs/tags");
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=summary", -class => "title"}, "tags") .
	      "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	if (defined @$taglist) {
		foreach my $entry (@$taglist) {
			my %tag = %$entry;
			if ($alternate) {
				print "<tr style=\"background-color:#f6f5ed\">\n";
			} else {
				print "<tr>\n";
			}
			$alternate ^= 1;
			print "<td><i>$tag{'age'}</i></td>\n" .
			      "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=log;h=$tag{'id'}", -class => "list"}, "<b>" . escapeHTML($tag{'name'}) . "</b>") . "</td>\n" .
			      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?p=$project;a=$tag{'type'};h=$tag{'id'}"}, $tag{'type'}) . "</td>\n" .
			      "</tr>";
		}
	}
	print "</table\n>";
	git_footer_html();
}

sub git_branches {
	my $head = git_read_hash("$project/HEAD");
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$head"}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree"}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";
	my $taglist = git_read_refs("refs/heads");
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=summary", -class => "title"}, "branches") .
	      "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	if (defined @$taglist) {
		foreach my $entry (@$taglist) {
			my %tag = %$entry;
			if ($alternate) {
				print "<tr style=\"background-color:#f6f5ed\">\n";
			} else {
				print "<tr>\n";
			}
			$alternate ^= 1;
			print "<td><i>$tag{'age'}</i></td>\n" .
			      "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=log;h=$tag{'id'}", -class => "list"}, "<b>" . escapeHTML($tag{'name'}) . "</b>") . "</td>\n" .
			      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?p=$project;a=log;h=$tag{'id'}"}, "log") . "</td>\n" .
			      "</tr>";
		}
	}
	print "</table\n>";
	git_footer_html();
}

sub git_get_hash_by_path {
	my $base = shift;
	my $path = shift;

	my $tree = $base;
	my @parts = split '/', $path;
	while (my $part = shift @parts) {
		open my $fd, "-|", "$gitbin/git-ls-tree $tree" || die_error(undef, "Open git-ls-tree failed.");
		my (@entries) = map { chomp; $_ } <$fd>;
		close $fd || return undef;
		foreach my $line (@entries) {
			#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
			$line =~ m/^([0-9]+)\t(.+)\t([0-9a-fA-F]{40})\t(.+)$/;
			my $t_mode = $1;
			my $t_type = $2;
			my $t_hash = $3;
			my $t_name = $4;
			if ($t_name eq $part) {
				if (!(@parts)) {
					return $t_hash;
				}
				if ($t_type eq "tree") {
					$tree = $t_hash;
				}
				last;
			}
		}
	}
}

sub git_blob {
	if (!defined $hash && defined $file_name) {
		my $base = $hash_base || git_read_hash("$project/HEAD");
		$hash = git_get_hash_by_path($base, $file_name, "blob");
	}
	open my $fd, "-|", "$gitbin/git-cat-file blob $hash" || die_error(undef, "Open failed.");
	my $base = $file_name || "";
	git_header_html();
	if (defined $hash_base && (my %co = git_read_commit($hash_base))) {
		print "<div class=\"page_nav\">\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash_base"}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash_base"}, "commitdiff") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash_base"}, "tree");
		if (defined $file_name) {
			print " | " . $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash_base;f=$file_name"}, "history");
		}
		print "<br/><br/>\n" .
		      "</div>\n";
		print "<div>" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash_base", -class => "title"}, escapeHTML($co{'title'})) .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n" .
		      "<br/><br/></div>\n" .
		      "<div class=\"title\">$hash</div>\n";
	}
	if (defined $file_name) {
		print "<div class=\"page_path\">/$file_name</div>\n";
	}
	print "<div class=\"page_body\">\n";
	my $nr;
	while (my $line = <$fd>) {
		chomp $line;
		$nr++;
		print "<div class=\"pre\">";
		printf "<span style=\"color:#999999;\">%4i</span>", $nr;
		print " " .escapeHTML($line) . "</div>\n";
	}
	close $fd || print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub git_tree {
	if (!defined $hash) {
		$hash = git_read_hash("$project/HEAD");
		if (defined $file_name) {
			my $base = $hash_base || git_read_hash("$project/HEAD");
			$hash = git_get_hash_by_path($base, $file_name, "tree");
		}
		if (!defined $hash_base) {
			$hash_base = git_read_hash("$project/HEAD");
		}
	}
	open my $fd, "-|", "$gitbin/git-ls-tree $hash" || die_error(undef, "Open git-ls-tree failed.");
	my (@entries) = map { chomp; $_ } <$fd>;
	close $fd || die_error(undef, "Reading tree failed.");

	git_header_html();
	my $base_key = "";
	my $file_key = "";
	my $base = "";
	if (defined $hash_base && (my %co = git_read_commit($hash_base))) {
		$base_key = ";hb=$hash_base";
		print "<div class=\"page_nav\">\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash_base"}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash_base"}, "commitdiff") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash_base"}, "tree") .
		      "<br/><br/>\n" .
		      "</div>\n";
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash_base", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n";
		print "<br/><br/></div>\n";
		print "<div class=\"title\">$hash</div>\n";
	}
	if (defined $file_name) {
		$base = "$file_name/";
		print "<div class=\"page_path\">/$file_name</div>\n";
	} else {
		print "<div class=\"page_path\">/</div>\n";
	}
	print "<div class=\"page_body\">\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $line (@entries) {
		#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+)\t(.+)\t([0-9a-fA-F]{40})\t(.+)$/;
		my $t_mode = $1;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = $4;
		$file_key = ";f=$base$t_name";
		if ($alternate) {
			print "<tr style=\"background-color:#f6f5ed\">\n";
		} else {
			print "<tr>\n";
		}
		$alternate ^= 1;
		print "<td style=\"font-family:monospace\">" . mode_str($t_mode) . "</td>\n";
		if ($t_type eq "blob") {
			print "<td class=\"list\">" .
			$cgi->a({-href => "$my_uri?p=$project;a=blob;h=$t_hash" . $base_key . $file_key, -class => "list"}, $t_name) .
			"</td>\n";
			print "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$t_hash" . $base_key . $file_key}, "blob") .
			      " | " . $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash_base" . $file_key}, "history") .
			      "</td>\n";
		} elsif ($t_type eq "tree") {
			print "<td class=\"list\">" .
			      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$t_hash" . $base_key . $file_key}, $t_name) .
			      "</td>\n";
			print "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash_base" . $file_key}, "history") .
			      "</td>\n";
		}
		print "</tr>\n";
	}
	print "</table>\n" .
	      "</div>";
	git_footer_html();
}

sub git_rss {
	open my $fd, "-|", "$gitbin/git-rev-list --max-count=20 " . git_read_hash("$project/HEAD") || die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd || die_error(undef, "Reading rev-list failed.");

	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n".
	      "<rss version=\"0.91\">\n";
	print "<channel>\n";
	print "<title>$project</title>\n".
	      "<link> $my_url/$project/log</link>\n".
	      "<description>$project log</description>\n".
	      "<language>en</language>\n";

	foreach my $commit (@revlist) {
		my %co = git_read_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		print "<item>\n" .
		      "\t<title>" . sprintf("%d %s %02d:%02d", $ad{'mday'}, $ad{'month'}, $ad{'hour'}, $ad{'minute'}) . " - " . escapeHTML($co{'title'}) . "</title>\n" .
		      "\t<link> $my_url?p=$project;a=commit;h=$commit</link>\n" .
		      "\t<description>";
		my $comment = $co{'comment'};
		foreach my $line (@$comment) {
			print escapeHTML($line) . "<br/>\n";
		}
		print "\t</description>\n" .
		      "</item>\n";
	}
	print "</channel></rss>";
}

sub git_log {
	if (!defined $hash) {
		$hash = git_read_hash("$project/HEAD");
	}
	my $limit_option = "";
	if (!defined $time_back) {
		$limit_option = "--max-count=10";
	} elsif ($time_back > 0) {
		my $date = time - $time_back*24*60*60;
		$limit_option = "--max-age=$date";
	}
	open my $fd, "-|", "$gitbin/git-rev-list $limit_option $hash" || die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd || die_error(undef, "Reading rev-list failed.");

	git_header_html();
	print "<div class=\"page_nav\">\n";
	print $cgi->a({-href => "$my_uri?p=$project;a=log;h=$hash"}, "last 10") .
	      " &sdot; " . $cgi->a({-href => "$my_uri?p=$project;a=log;t=1;h=$hash"}, "day") .
	      " &sdot; " .$cgi->a({-href => "$my_uri?p=$project;a=log;t=7;h=$hash"}, "week") .
	      " &sdot; " . $cgi->a({-href => "$my_uri?p=$project;a=log;t=31;h=$hash"}, "month") .
	      " &sdot; " . $cgi->a({-href => "$my_uri?p=$project;a=log;t=365;h=$hash"}, "year") .
	      " &sdot; " . $cgi->a({-href => "$my_uri?p=$project;a=log;t=0;h=$hash"}, "all") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;hb=$hash"}, "tree") . "<br/>\n";
	print "<br/>\n" .
	      "</div>\n";

	if (!@revlist) {
		my %co = git_read_commit($hash);
		print "<div class=\"page_body\"> Last change $co{'age_string'}.<br/><br/></div>\n";
	}

	foreach my $commit (@revlist) {
		my %co = git_read_commit($commit);
		next if !%co;
		my %ad = date_str($co{'author_epoch'});
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit", -class => "title"},
		      "<span class=\"age\">$co{'age_string'}</span>" . escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
		print "<div class=\"title_text\">\n" .
		      "<div class=\"log_link\">\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$commit"}, "commitdiff") .
		      "<br/>\n" .
		      "</div>\n" .
		      "<i>" . escapeHTML($co{'author_name'}) .  " [$ad{'rfc2822'}]</i><br/>\n" .
		      "</div>\n" .
		      "<div class=\"log_body\">\n";
		my $comment = $co{'comment'};
		my $empty = 0;
		foreach my $line (@$comment) {
			if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
				next;
			}
			if ($line eq "") {
				if ($empty) {
					next;
				}
				$empty = 1;
			} else {
				$empty = 0;
			}
			print escapeHTML($line) . "<br/>\n";
		}
		if (!$empty) {
			print "<br/>\n";
		}
		print "</div>\n";
	}
	git_footer_html();
}

sub git_commit {
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	my %ad = date_str($co{'author_epoch'}, $co{'author_tz'});
	my %cd = date_str($co{'committer_epoch'}, $co{'committer_tz'});

	my @difftree;
	if (defined $co{'parent'}) {
		open my $fd, "-|", "$gitbin/git-diff-tree -r $co{'parent'} $hash" || die_error(undef, "Open failed.");
		@difftree = map { chomp; $_ } <$fd>;
		close $fd || die_error(undef, "Reading diff-tree failed.");
	} else {
		# fake git-diff-tree output for initial revision
		open my $fd, "-|", "$gitbin/git-ls-tree -r $hash" || die_error(undef, "Open failed.");
		@difftree = map { chomp;  "+" . $_ } <$fd>;
		close $fd || die_error(undef, "Reading ls-tree failed.");
	}
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit");
	if (defined $co{'parent'}) {
		print " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "commitdiff");
	}
	print " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash"}, "tree") . "\n" .
	      "<br/><br/></div>\n";
	if (defined $co{'parent'}) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	} else {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	}
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">\n";
	print "<tr><td>author</td><td>" . escapeHTML($co{'author'}) . "</td></tr>\n".
	      "<tr>" .
	      "<td></td><td> $ad{'rfc2822'}";
	if ($ad{'hour_local'} < 6) {
		printf(" (<span style=\"color: #cc0000;\">%02d:%02d</span> %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	} else {
		printf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	}
	print "</td>" .
	      "</tr>\n";
	print "<tr><td>committer</td><td>" . escapeHTML($co{'committer'}) . "</td></tr>\n";
	print "<tr><td></td><td> $cd{'rfc2822'}" . sprintf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'}) . "</td></tr>\n";
	print "<tr><td>commit</td><td style=\"font-family:monospace\">$hash</td></tr>\n";
	print "<tr>" .
	      "<td>tree</td>" .
	      "<td style=\"font-family:monospace\">" . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash", class => "list"}, $co{'tree'}) . "</td>" .
	      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash"}, "tree") .
	      "</td>" .
	      "</tr>\n";
	my $parents  = $co{'parents'};
	foreach my $par (@$parents) {
		print "<tr>" .
		      "<td>parent</td>" .
		      "<td style=\"font-family:monospace\">" . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$par", class => "list"}, $par) . "</td>" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$par"}, "commit") .
		      " |" . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash;hp=$par"}, "commitdiff") .
		      "</td>" .
		      "</tr>\n";
	}
	print "</table>". 
	      "</div>\n";
	print "<div class=\"page_body\">\n";
	my $comment = $co{'comment'};
	my $empty = 0;
	my $signed = 0;
	foreach my $line (@$comment) {
		# print only one empty line
		if ($line eq "") {
			if ($empty || $signed) {
				next;
			}
			$empty = 1;
		} else {
			$empty = 0;
		}
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			$signed = 1;
			print "<span style=\"color: #888888\">" . escapeHTML($line) . "</span><br/>\n";
		} else {
			$signed = 0;
			print escapeHTML($line) . "<br/>\n";
		}
	}
	print "</div>\n";
	print "<div class=\"list_head\">\n";
	if ($#difftree > 10) {
		print(($#difftree + 1) . " files changed:\n");
	}
	print "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $line (@difftree) {
		# '*100644->100644	blob	9f91a116d91926df3ba936a80f020a6ab1084d2b->bb90a0c3a91eb52020d0db0e8b4f94d30e02d596	net/ipv4/route.c'
		# '+100644	blob	4a83ab6cd565d21ab0385bac6643826b83c2fcd4	arch/arm/lib/bitops.h'
		# '*100664->100644	blob	b1a8e3dd5556b61dd771d32307c6ee5d7150fa43->b1a8e3dd5556b61dd771d32307c6ee5d7150fa43	show-files.c'
		# '*100664->100644	blob	d08e895238bac36d8220586fdc28c27e1a7a76d3->d08e895238bac36d8220586fdc28c27e1a7a76d3	update-cache.c'
		$line =~ m/^(.)(.+)\t(.+)\t([0-9a-fA-F]{40}|[0-9a-fA-F]{40}->[0-9a-fA-F]{40})\t(.+)$/;
		my $op = $1;
		my $mode = $2;
		my $type = $3;
		my $id = $4;
		my $file = $5;
		if ($type ne "blob") {
			next;
		}
		if ($alternate) {
			print "<tr style=\"background-color:#f6f5ed\">\n";
		} else {
			print "<tr>\n";
		}
		$alternate ^= 1;
		if ($op eq "+") {
			my $mode_chng = "";
			if (S_ISREG(oct $mode)) {
				$mode_chng = sprintf(" with mode: %04o", (oct $mode) & 0777);
			}
			print "<td>" .
			      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id;hp=$hash;f=$file", -class => "list"}, escapeHTML($file)) . "</td>\n" .
			      "<td><span style=\"color: #008000;\">[new " . file_type($mode) . "$mode_chng]</span></td>\n" .
			      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id;hb=$hash;f=$file"}, "blob") . "</td>\n";
		} elsif ($op eq "-") {
			print "<td>" .
			      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id;hb=$hash;f=$file", -class => "list"}, escapeHTML($file)) . "</td>\n" .
			      "<td><span style=\"color: #c00000;\">[deleted " . file_type($mode). "]</span></td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id;hb=$hash;f=$file"}, "blob") .
			      " | " . $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash;f=$file"}, "history") .
			      "</td>\n"
		} elsif ($op eq "*") {
			$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
			my $from_id = $1;
			my $to_id = $2;
			$mode =~ m/^([0-7]{6})->([0-7]{6})$/;
			my $from_mode = $1;
			my $to_mode = $2;
			my $mode_chnge = "";
			if ($from_mode != $to_mode) {
				$mode_chnge = " <span style=\"color: #777777;\">[changed";
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
			print "<td>";
			if ($to_id ne $from_id) {
				print $cgi->a({-href => "$my_uri?p=$project;a=blobdiff;h=$to_id;hp=$from_id;hb=$hash;f=$file", -class => "list"}, escapeHTML($file));
			} else {
				print $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to_id;hb=$hash;f=$file", -class => "list"}, escapeHTML($file));
			}
			print "</td>\n" .
			      "<td>$mode_chnge</td>\n" .
			      "<td class=\"link\">";
			print $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to_id;hb=$hash;f=$file"}, "blob");
			if ($to_id ne $from_id) {
				print " | " . $cgi->a({-href => "$my_uri?p=$project;a=blobdiff;h=$to_id;hp=$from_id;hb=$hash;f=$file"}, "diff");
			}
			print " | " . $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash;f=$file"}, "history") . "\n";
			print "</td>\n";
		}
		print "</tr>\n";
	}
	print "</table>\n";
	git_footer_html();
}

sub git_blobdiff {
	mkdir($gittmp, 0700);
	git_header_html();
	if (defined $hash_base && (my %co = git_read_commit($hash_base))) {
		print "<div class=\"page_nav\">\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash_base"}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash_base"}, "commitdiff") .
		      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash_base"}, "tree");
			if (defined $file_name) {
				print " | " . $cgi->a({-href => "$my_uri?p=$project;a=history;h=$hash_base;f=$file_name"}, "history");
			}
		print "<br/><br/>\n" .
		      "</div>\n";
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash_base", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n" .
		      "<br/><br/></div>\n" .
		      "<div class=\"title\">$hash vs $hash_parent</div>\n";
	}
	if (defined $file_name) {
		print "<div class=\"page_path\">\n" .
		      "/$file_name\n" .
		      "</div>\n";
	}
	print "<div class=\"page_body\">\n" .
	      "<div class=\"diff_info\">blob:" .
	      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$hash_parent;hb=$hash_base;f=$file_name"}, $hash_parent) .
	      " -> blob:" .
	      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$hash;hb=$hash_base;f=$file_name"}, $hash) .
	      "</div>\n";
	git_diff_html($hash_parent, $file_name || $hash_parent, $hash, $file_name || $hash);
	print "</div>";
	git_footer_html();
}

sub git_commitdiff {
	mkdir($gittmp, 0700);
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	if (!defined $hash_parent) {
		$hash_parent = $co{'parent'};
	}
	open my $fd, "-|", "$gitbin/git-diff-tree -r $hash_parent $hash" || die_error(undef, "Open failed.");
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd || die_error(undef, "Reading diff-tree failed.");

	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=log"}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=" .  $co{'tree'} . ";hb=$hash"}, "tree") .
	      "<br/><br/></div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
	      "</div>\n";
	print "<div class=\"page_body\">\n";
	my $comment = $co{'comment'};
	my $empty = 0;
	my $signed = 0;
	my @log = @$comment;
	# remove first and empty lines after that
	shift @log;
	while (defined $log[0] && $log[0] eq "") {
		shift @log;
	}
	foreach my $line (@log) {
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			next;
		}
		if ($line eq "") {
			if ($empty) {
				next;
			}
			$empty = 1;
		} else {
			$empty = 0;
		}
		print escapeHTML($line) . "<br/>\n";
	}
	print "<br/>\n";
	foreach my $line (@difftree) {
		# '*100644->100644	blob	8e5f9bbdf4de94a1bc4b4da8cb06677ce0a57716->8da3a306d0c0c070d87048d14a033df02f40a154	Makefile'
		$line =~ m/^(.)(.+)\t(.+)\t([0-9a-fA-F]{40}|[0-9a-fA-F]{40}->[0-9a-fA-F]{40})\t(.+)$/;
		my $op = $1;
		my $mode = $2;
		my $type = $3;
		my $id = $4;
		my $file = $5;
		if ($type eq "blob") {
			if ($op eq "+") {
				print "<div class=\"diff_info\">" .  file_type($mode) . ":" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id;hb=$hash;f=$file"}, $id) . "(new)" .
				      "</div>\n";
				git_diff_html(undef, "/dev/null", $id, "b/$file");
			} elsif ($op eq "-") {
				print "<div class=\"diff_info\">" . file_type($mode) . ":" .
				      $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$id;hb=$hash;f=$file"}, $id) . "(deleted)" .
				      "</div>\n";
				git_diff_html($id, "a/$file", undef, "/dev/null");
			} elsif ($op eq "*") {
				$id =~ m/([0-9a-fA-F]+)->([0-9a-fA-F]+)/;
				my $from_id = $1;
				my $to_id = $2;
				$mode =~ m/([0-7]+)->([0-7]+)/;
				my $from_mode = $1;
				my $to_mode = $2;
				if ($from_id ne $to_id) {
					print "<div class=\"diff_info\">" .
					      file_type($from_mode) . ":" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$from_id;hb=$hash;f=$file"}, $from_id) .
					      " -> " .
					      file_type($to_mode) . ":" . $cgi->a({-href => "$my_uri?p=$project;a=blob;h=$to_id;hb=$hash;f=$file"}, $to_id);
					print "</div>\n";
					git_diff_html($from_id, "a/$file",  $to_id, "b/$file");
				}
			}
		}
	}
	print "<br/>\n" .
	      "</div>";
	git_footer_html();
}

sub git_history {
	if (!defined $hash) {
		$hash = git_read_hash("$project/HEAD");
	}
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash"}, "commit") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=commitdiff;h=$hash"}, "commitdiff") . " | " .
	      $cgi->a({-href => "$my_uri?p=$project;a=tree;h=$co{'tree'};hb=$hash"}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$hash", -class => "title"}, escapeHTML($co{'title'})) . "\n" .
	      "</div>\n";
	print "<div class=\"page_path\">\n" .
	      "/$file_name<br/>\n";
	print "</div>\n";

	open my $fd, "-|", "$gitbin/git-rev-list $hash | $gitbin/git-diff-tree -r --stdin $file_name";
	my $commit;
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	while (my $line = <$fd>) {
		if ($line =~ m/^([0-9a-fA-F]{40}) /){
			$commit = $1;
			next;
		}
		if ($line =~ m/^(.)(.+)\t(.+)\t([0-9a-fA-F]{40}->[0-9a-fA-F]{40})\t(.+)$/ && (defined $commit)) {
			my %co = git_read_commit($commit);
			if (!%co) {
				next;
			}
			if ($alternate) {
				print "<tr style=\"background-color:#f6f5ed\">\n";
			} else {
				print "<tr>\n";
			}
			$alternate ^= 1;
			print "<td><i>$co{'age_string'}</i></td>\n" .
			      "<td><i>" . escapeHTML(chop_str($co{'author_name'}, 10)) . "</i></td>\n" .
			      "<td>" . $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit", -class => "list"}, "<b>" . escapeHTML($co{'title'}) . "</b>") . "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?p=$project;a=commit;h=$commit"}, "commit") .
			      " | " . $cgi->a({-href => "$my_uri?p=$project;a=tree;h=" .  $co{'tree'} . ";hb=$commit"}, "tree") .
			      " | " . $cgi->a({-href => "$my_uri?p=$project;a=blob;hb=$commit;f=$file_name"}, "blob");
			my $blob = git_get_hash_by_path($hash, $file_name);
			my $blob_parent = git_get_hash_by_path($commit, $file_name);
			if (defined $blob && defined $blob_parent && $blob ne $blob_parent) {
				print " | " . $cgi->a({-href => "$my_uri?p=$project;a=blobdiff;h=$blob;hp=$blob_parent;hb=$commit;f=$file_name"}, "diff");
			}
			print "</td>\n" .
			      "</tr>\n";
			undef $commit;
		}
	}
	print "</table>\n";
	close $fd;
	git_footer_html();
}
