#!/usr/bin/perl

# gitweb - simple web interface to track changes in git repositories
#
# (C) 2005-2006, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke
#
# This program is licensed under the GPLv2

use strict;
use warnings;
use CGI qw(:standard :escapeHTML -nosticky);
use CGI::Util qw(unescape);
use CGI::Carp qw(fatalsToBrowser);
use Encode;
use Fcntl ':mode';
use File::Find qw();
use File::Basename qw(basename);
binmode STDOUT, ':utf8';

our $cgi = new CGI;
our $version = "++GIT_VERSION++";
our $my_url = $cgi->url();
our $my_uri = $cgi->url(-absolute => 1);

# core git executable to use
# this can just be "git" if your webserver has a sensible PATH
our $GIT = "++GIT_BINDIR++/git";

# absolute fs-path which will be prepended to the project path
#our $projectroot = "/pub/scm";
our $projectroot = "++GITWEB_PROJECTROOT++";

# location for temporary files needed for diffs
our $git_temp = "/tmp/gitweb";

# target of the home link on top of all pages
our $home_link = $my_uri || "/";

# string of the home link on top of all pages
our $home_link_str = "++GITWEB_HOME_LINK_STR++";

# name of your site or organization to appear in page titles
# replace this with something more descriptive for clearer bookmarks
our $site_name = "++GITWEB_SITENAME++" || $ENV{'SERVER_NAME'} || "Untitled";

# html text to include at home page
our $home_text = "++GITWEB_HOMETEXT++";

# URI of default stylesheet
our $stylesheet = "++GITWEB_CSS++";
# URI of GIT logo
our $logo = "++GITWEB_LOGO++";

# source of projects list
our $projects_list = "++GITWEB_LIST++";

# list of git base URLs used for URL to where fetch project from,
# i.e. full URL is "$git_base_url/$project"
our @git_base_url_list = ("++GITWEB_BASE_URL++");

# default blob_plain mimetype and default charset for text/plain blob
our $default_blob_plain_mimetype = 'text/plain';
our $default_text_plain_charset  = undef;

# file to use for guessing MIME types before trying /etc/mime.types
# (relative to the current git repository)
our $mimetypes_file = undef;

# You define site-wide feature defaults here; override them with
# $GITWEB_CONFIG as necessary.
our %feature = (
	# feature => {'sub' => feature-sub, 'override' => allow-override, 'default' => [ default options...]
	# if feature is overridable, feature-sub will be called with default options;
	# return value indicates if to enable specified feature

	'blame' => {
		'sub' => \&feature_blame,
		'override' => 0,
		'default' => [0]},

	'snapshot' => {
		'sub' => \&feature_snapshot,
		'override' => 0,
		#         => [content-encoding, suffix, program]
		'default' => ['x-gzip', 'gz', 'gzip']},
);

sub gitweb_check_feature {
	my ($name) = @_;
	return undef unless exists $feature{$name};
	my ($sub, $override, @defaults) = (
		$feature{$name}{'sub'},
		$feature{$name}{'override'},
		@{$feature{$name}{'default'}});
	if (!$override) { return @defaults; }
	return $sub->(@defaults);
}

# To enable system wide have in $GITWEB_CONFIG
# $feature{'blame'}{'default'} =  [1];
# To have project specific config enable override in  $GITWEB_CONFIG
# $feature{'blame'}{'override'} =  1;
# and in project config gitweb.blame = 0|1;

sub feature_blame {
	my ($val) = git_get_project_config('blame', '--bool');

	if ($val eq 'true') {
		return 1;
	} elsif ($val eq 'false') {
		return 0;
	}

	return $_[0];
}

# To disable system wide have in $GITWEB_CONFIG
# $feature{'snapshot'}{'default'} =  [undef];
# To have project specific config enable override in  $GITWEB_CONFIG
# $feature{'blame'}{'override'} =  1;
# and in project config  gitweb.snapshot = none|gzip|bzip2

sub feature_snapshot {
	my ($ctype, $suffix, $command) = @_;

	my ($val) = git_get_project_config('snapshot');

	if ($val eq 'gzip') {
		return ('x-gzip', 'gz', 'gzip');
	} elsif ($val eq 'bzip2') {
		return ('x-bzip2', 'bz2', 'bzip2');
	} elsif ($val eq 'none') {
		return ();
	}

	return ($ctype, $suffix, $command);
}

our $GITWEB_CONFIG = $ENV{'GITWEB_CONFIG'} || "++GITWEB_CONFIG++";
require $GITWEB_CONFIG if -e $GITWEB_CONFIG;

# version of the core git binary
our $git_version = qx($GIT --version) =~ m/git version (.*)$/ ? $1 : "unknown";

$projects_list ||= $projectroot;
if (! -d $git_temp) {
	mkdir($git_temp, 0700) || die_error(undef, "Couldn't mkdir $git_temp");
}

# ======================================================================
# input validation and dispatch
our $action = $cgi->param('a');
if (defined $action) {
	if ($action =~ m/[^0-9a-zA-Z\.\-_]/) {
		die_error(undef, "Invalid action parameter");
	}
}

our $project = ($cgi->param('p') || $ENV{'PATH_INFO'});
if (defined $project) {
	$project =~ s|^/||;
	$project =~ s|/$||;
	$project = undef unless $project;
}
if (defined $project) {
	if (!validate_input($project)) {
		die_error(undef, "Invalid project parameter");
	}
	if (!(-d "$projectroot/$project")) {
		die_error(undef, "No such directory");
	}
	if (!(-e "$projectroot/$project/HEAD")) {
		die_error(undef, "No such project");
	}
	$ENV{'GIT_DIR'} = "$projectroot/$project";
}

our $file_name = $cgi->param('f');
if (defined $file_name) {
	if (!validate_input($file_name)) {
		die_error(undef, "Invalid file parameter");
	}
}

our $file_parent = $cgi->param('fp');
if (defined $file_parent) {
	if (!validate_input($file_parent)) {
		die_error(undef, "Invalid file parent parameter");
	}
}

our $hash = $cgi->param('h');
if (defined $hash) {
	if (!validate_input($hash)) {
		die_error(undef, "Invalid hash parameter");
	}
}

our $hash_parent = $cgi->param('hp');
if (defined $hash_parent) {
	if (!validate_input($hash_parent)) {
		die_error(undef, "Invalid hash parent parameter");
	}
}

our $hash_base = $cgi->param('hb');
if (defined $hash_base) {
	if (!validate_input($hash_base)) {
		die_error(undef, "Invalid hash base parameter");
	}
}

our $page = $cgi->param('pg');
if (defined $page) {
	if ($page =~ m/[^0-9]$/) {
		die_error(undef, "Invalid page parameter");
	}
}

our $searchtext = $cgi->param('s');
if (defined $searchtext) {
	if ($searchtext =~ m/[^a-zA-Z0-9_\.\/\-\+\:\@ ]/) {
		die_error(undef, "Invalid search parameter");
	}
	$searchtext = quotemeta $searchtext;
}

# dispatch
my %actions = (
	"blame" => \&git_blame2,
	"blobdiff" => \&git_blobdiff,
	"blobdiff_plain" => \&git_blobdiff_plain,
	"blob" => \&git_blob,
	"blob_plain" => \&git_blob_plain,
	"commitdiff" => \&git_commitdiff,
	"commitdiff_plain" => \&git_commitdiff_plain,
	"commit" => \&git_commit,
	"heads" => \&git_heads,
	"history" => \&git_history,
	"log" => \&git_log,
	"rss" => \&git_rss,
	"search" => \&git_search,
	"shortlog" => \&git_shortlog,
	"summary" => \&git_summary,
	"tag" => \&git_tag,
	"tags" => \&git_tags,
	"tree" => \&git_tree,
	"snapshot" => \&git_snapshot,
	# those below don't need $project
	"opml" => \&git_opml,
	"project_list" => \&git_project_list,
);

if (defined $project) {
	$action ||= 'summary';
} else {
	$action ||= 'project_list';
}
if (!defined($actions{$action})) {
	die_error(undef, "Unknown action");
}
$actions{$action}->();
exit;

## ======================================================================
## action links

sub href(%) {
	my %mapping = (
		action => "a",
		project => "p",
		file_name => "f",
		file_parent => "fp",
		hash => "h",
		hash_parent => "hp",
		hash_base => "hb",
		page => "pg",
		searchtext => "s",
	);

	my %params = @_;
	$params{"project"} ||= $project;

	my $href = "$my_uri?";
	$href .= esc_param( join(";",
		map {
			defined $params{$_} ? "$mapping{$_}=$params{$_}" : ()
		} keys %params
	) );

	return $href;
}


## ======================================================================
## validation, quoting/unquoting and escaping

sub validate_input {
	my $input = shift;

	if ($input =~ m/^[0-9a-fA-F]{40}$/) {
		return $input;
	}
	if ($input =~ m/(^|\/)(|\.|\.\.)($|\/)/) {
		return undef;
	}
	if ($input =~ m/[^a-zA-Z0-9_\x80-\xff\ \t\.\/\-\+\#\~\%]/) {
		return undef;
	}
	return $input;
}

# quote unsafe chars, but keep the slash, even when it's not
# correct, but quoted slashes look too horrible in bookmarks
sub esc_param {
	my $str = shift;
	$str =~ s/([^A-Za-z0-9\-_.~();\/;?:@&=])/sprintf("%%%02X", ord($1))/eg;
	$str =~ s/\+/%2B/g;
	$str =~ s/ /\+/g;
	return $str;
}

# replace invalid utf8 character with SUBSTITUTION sequence
sub esc_html {
	my $str = shift;
	$str = decode("utf8", $str, Encode::FB_DEFAULT);
	$str = escapeHTML($str);
	$str =~ s/\014/^L/g; # escape FORM FEED (FF) character (e.g. in COPYING file)
	return $str;
}

# git may return quoted and escaped filenames
sub unquote {
	my $str = shift;
	if ($str =~ m/^"(.*)"$/) {
		$str = $1;
		$str =~ s/\\([0-7]{1,3})/chr(oct($1))/eg;
	}
	return $str;
}

# escape tabs (convert tabs to spaces)
sub untabify {
	my $line = shift;

	while ((my $pos = index($line, "\t")) != -1) {
		if (my $count = (8 - ($pos % 8))) {
			my $spaces = ' ' x $count;
			$line =~ s/\t/$spaces/;
		}
	}

	return $line;
}

## ----------------------------------------------------------------------
## HTML aware string manipulation

sub chop_str {
	my $str = shift;
	my $len = shift;
	my $add_len = shift || 10;

	# allow only $len chars, but don't cut a word if it would fit in $add_len
	# if it doesn't fit, cut it if it's still longer than the dots we would add
	$str =~ m/^(.{0,$len}[^ \/\-_:\.@]{0,$add_len})(.*)/;
	my $body = $1;
	my $tail = $2;
	if (length($tail) > 4) {
		$tail = " ...";
		$body =~ s/&[^;]*$//; # remove chopped character entities
	}
	return "$body$tail";
}

## ----------------------------------------------------------------------
## functions returning short strings

# CSS class for given age value (in seconds)
sub age_class {
	my $age = shift;

	if ($age < 60*60*2) {
		return "age0";
	} elsif ($age < 60*60*24*2) {
		return "age1";
	} else {
		return "age2";
	}
}

# convert age in seconds to "nn units ago" string
sub age_string {
	my $age = shift;
	my $age_str;

	if ($age > 60*60*24*365*2) {
		$age_str = (int $age/60/60/24/365);
		$age_str .= " years ago";
	} elsif ($age > 60*60*24*(365/12)*2) {
		$age_str = int $age/60/60/24/(365/12);
		$age_str .= " months ago";
	} elsif ($age > 60*60*24*7*2) {
		$age_str = int $age/60/60/24/7;
		$age_str .= " weeks ago";
	} elsif ($age > 60*60*24*2) {
		$age_str = int $age/60/60/24;
		$age_str .= " days ago";
	} elsif ($age > 60*60*2) {
		$age_str = int $age/60/60;
		$age_str .= " hours ago";
	} elsif ($age > 60*2) {
		$age_str = int $age/60;
		$age_str .= " min ago";
	} elsif ($age > 2) {
		$age_str = int $age;
		$age_str .= " sec ago";
	} else {
		$age_str .= " right now";
	}
	return $age_str;
}

# convert file mode in octal to symbolic file mode string
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

# convert file mode in octal to file type string
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

## ----------------------------------------------------------------------
## functions returning short HTML fragments, or transforming HTML fragments
## which don't beling to other sections

# format line of commit message or tag comment
sub format_log_line_html {
	my $line = shift;

	$line = esc_html($line);
	$line =~ s/ /&nbsp;/g;
	if ($line =~ m/([0-9a-fA-F]{40})/) {
		my $hash_text = $1;
		if (git_get_type($hash_text) eq "commit") {
			my $link =
				$cgi->a({-href => href(action=>"commit", hash=>$hash_text),
				        -class => "text"}, $hash_text);
			$line =~ s/$hash_text/$link/;
		}
	}
	return $line;
}

# format marker of refs pointing to given object
sub format_ref_marker {
	my ($refs, $id) = @_;
	my $markers = '';

	if (defined $refs->{$id}) {
		foreach my $ref (@{$refs->{$id}}) {
			my ($type, $name) = qw();
			# e.g. tags/v2.6.11 or heads/next
			if ($ref =~ m!^(.*?)s?/(.*)$!) {
				$type = $1;
				$name = $2;
			} else {
				$type = "ref";
				$name = $ref;
			}

			$markers .= " <span class=\"$type\">" . esc_html($name) . "</span>";
		}
	}

	if ($markers) {
		return ' <span class="refs">'. $markers . '</span>';
	} else {
		return "";
	}
}

# format, perhaps shortened and with markers, title line
sub format_subject_html {
	my ($long, $short, $href, $extra) = @_;
	$extra = '' unless defined($extra);

	if (length($short) < length($long)) {
		return $cgi->a({-href => $href, -class => "list subject",
		                -title => $long},
		       esc_html($short) . $extra);
	} else {
		return $cgi->a({-href => $href, -class => "list subject"},
		       esc_html($long)  . $extra);
	}
}

## ----------------------------------------------------------------------
## git utility subroutines, invoking git commands

# get HEAD ref of given project as hash
sub git_get_head_hash {
	my $project = shift;
	my $oENV = $ENV{'GIT_DIR'};
	my $retval = undef;
	$ENV{'GIT_DIR'} = "$projectroot/$project";
	if (open my $fd, "-|", $GIT, "rev-parse", "--verify", "HEAD") {
		my $head = <$fd>;
		close $fd;
		if (defined $head && $head =~ /^([0-9a-fA-F]{40})$/) {
			$retval = $1;
		}
	}
	if (defined $oENV) {
		$ENV{'GIT_DIR'} = $oENV;
	}
	return $retval;
}

# get type of given object
sub git_get_type {
	my $hash = shift;

	open my $fd, "-|", $GIT, "cat-file", '-t', $hash or return;
	my $type = <$fd>;
	close $fd or return;
	chomp $type;
	return $type;
}

sub git_get_project_config {
	my ($key, $type) = @_;

	return unless ($key);
	$key =~ s/^gitweb\.//;
	return if ($key =~ m/\W/);

	my @x = ($GIT, 'repo-config');
	if (defined $type) { push @x, $type; }
	push @x, "--get";
	push @x, "gitweb.$key";
	my $val = qx(@x);
	chomp $val;
	return ($val);
}

# get hash of given path at given ref
sub git_get_hash_by_path {
	my $base = shift;
	my $path = shift || return undef;

	my $tree = $base;

	open my $fd, "-|", $GIT, "ls-tree", $base, "--", $path
		or die_error(undef, "Open git-ls-tree failed");
	my $line = <$fd>;
	close $fd or return undef;

	#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
	$line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t(.+)$/;
	return $3;
}

## ......................................................................
## git utility functions, directly accessing git repository

# assumes that PATH is not symref
sub git_get_hash_by_ref {
	my $path = shift;

	open my $fd, "$projectroot/$path" or return undef;
	my $head = <$fd>;
	close $fd;
	chomp $head;
	if ($head =~ m/^[0-9a-fA-F]{40}$/) {
		return $head;
	}
}

sub git_get_project_description {
	my $path = shift;

	open my $fd, "$projectroot/$path/description" or return undef;
	my $descr = <$fd>;
	close $fd;
	chomp $descr;
	return $descr;
}

sub git_get_project_url_list {
	my $path = shift;

	open my $fd, "$projectroot/$path/cloneurl" or return undef;
	my @git_project_url_list = map { chomp; $_ } <$fd>;
	close $fd;

	return wantarray ? @git_project_url_list : \@git_project_url_list;
}

sub git_get_projects_list {
	my @list;

	if (-d $projects_list) {
		# search in directory
		my $dir = $projects_list;
		opendir my ($dh), $dir or return undef;
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
		open my ($fd), $projects_list or return undef;
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
					owner => decode("utf8", $owner, Encode::FB_DEFAULT),
				};
				push @list, $pr
			}
		}
		close $fd;
	}
	@list = sort {$a->{'path'} cmp $b->{'path'}} @list;
	return @list;
}

sub git_get_project_owner {
	my $project = shift;
	my $owner;

	return undef unless $project;

	# read from file (url-encoded):
	# 'git%2Fgit.git Linus+Torvalds'
	# 'libs%2Fklibc%2Fklibc.git H.+Peter+Anvin'
	# 'linux%2Fhotplug%2Fudev.git Greg+Kroah-Hartman'
	if (-f $projects_list) {
		open (my $fd , $projects_list);
		while (my $line = <$fd>) {
			chomp $line;
			my ($pr, $ow) = split ' ', $line;
			$pr = unescape($pr);
			$ow = unescape($ow);
			if ($pr eq $project) {
				$owner = decode("utf8", $ow, Encode::FB_DEFAULT);
				last;
			}
		}
		close $fd;
	}
	if (!defined $owner) {
		$owner = get_file_owner("$projectroot/$project");
	}

	return $owner;
}

sub git_get_references {
	my $type = shift || "";
	my %refs;
	my $fd;
	# 5dc01c595e6c6ec9ccda4f6f69c131c0dd945f8c	refs/tags/v2.6.11
	# c39ae07f393806ccf406ef966e9a15afc43cc36a	refs/tags/v2.6.11^{}
	if (-f "$projectroot/$project/info/refs") {
		open $fd, "$projectroot/$project/info/refs"
			or return;
	} else {
		open $fd, "-|", $GIT, "ls-remote", "."
			or return;
	}

	while (my $line = <$fd>) {
		chomp $line;
		if ($line =~ m/^([0-9a-fA-F]{40})\trefs\/($type\/?[^\^]+)/) {
			if (defined $refs{$1}) {
				push @{$refs{$1}}, $2;
			} else {
				$refs{$1} = [ $2 ];
			}
		}
	}
	close $fd or return;
	return \%refs;
}

## ----------------------------------------------------------------------
## parse to hash functions

sub parse_date {
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
	$date{'rfc2822'} = sprintf "%s, %d %s %4d %02d:%02d:%02d +0000",
	                   $days[$wday], $mday, $months[$mon], 1900+$year, $hour ,$min, $sec;
	$date{'mday-time'} = sprintf "%d %s %02d:%02d",
	                     $mday, $months[$mon], $hour ,$min;

	$tz =~ m/^([+\-][0-9][0-9])([0-9][0-9])$/;
	my $local = $epoch + ((int $1 + ($2/60)) * 3600);
	($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($local);
	$date{'hour_local'} = $hour;
	$date{'minute_local'} = $min;
	$date{'tz_local'} = $tz;
	return %date;
}

sub parse_tag {
	my $tag_id = shift;
	my %tag;
	my @comment;

	open my $fd, "-|", $GIT, "cat-file", "tag", $tag_id or return;
	$tag{'id'} = $tag_id;
	while (my $line = <$fd>) {
		chomp $line;
		if ($line =~ m/^object ([0-9a-fA-F]{40})$/) {
			$tag{'object'} = $1;
		} elsif ($line =~ m/^type (.+)$/) {
			$tag{'type'} = $1;
		} elsif ($line =~ m/^tag (.+)$/) {
			$tag{'name'} = $1;
		} elsif ($line =~ m/^tagger (.*) ([0-9]+) (.*)$/) {
			$tag{'author'} = $1;
			$tag{'epoch'} = $2;
			$tag{'tz'} = $3;
		} elsif ($line =~ m/--BEGIN/) {
			push @comment, $line;
			last;
		} elsif ($line eq "") {
			last;
		}
	}
	push @comment, <$fd>;
	$tag{'comment'} = \@comment;
	close $fd or return;
	if (!defined $tag{'name'}) {
		return
	};
	return %tag
}

sub parse_commit {
	my $commit_id = shift;
	my $commit_text = shift;

	my @commit_lines;
	my %co;

	if (defined $commit_text) {
		@commit_lines = @$commit_text;
	} else {
		$/ = "\0";
		open my $fd, "-|", $GIT, "rev-list", "--header", "--parents", "--max-count=1", $commit_id
			or return;
		@commit_lines = split '\n', <$fd>;
		close $fd or return;
		$/ = "\n";
		pop @commit_lines;
	}
	my $header = shift @commit_lines;
	if (!($header =~ m/^[0-9a-fA-F]{40}/)) {
		return;
	}
	($co{'id'}, my @parents) = split ' ', $header;
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];
	while (my $line = shift @commit_lines) {
		last if $line eq "\n";
		if ($line =~ m/^tree ([0-9a-fA-F]{40})$/) {
			$co{'tree'} = $1;
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
		return;
	};

	foreach my $title (@commit_lines) {
		$title =~ s/^    //;
		if ($title ne "") {
			$co{'title'} = chop_str($title, 80, 5);
			# remove leading stuff of merges to make the interesting part visible
			if (length($title) > 50) {
				$title =~ s/^Automatic //;
				$title =~ s/^merge (of|with) /Merge ... /i;
				if (length($title) > 50) {
					$title =~ s/(http|rsync):\/\///;
				}
				if (length($title) > 50) {
					$title =~ s/(master|www|rsync)\.//;
				}
				if (length($title) > 50) {
					$title =~ s/kernel.org:?//;
				}
				if (length($title) > 50) {
					$title =~ s/\/pub\/scm//;
				}
			}
			$co{'title_short'} = chop_str($title, 50, 5);
			last;
		}
	}
	# remove added spaces
	foreach my $line (@commit_lines) {
		$line =~ s/^    //;
	}
	$co{'comment'} = \@commit_lines;

	my $age = time - $co{'committer_epoch'};
	$co{'age'} = $age;
	$co{'age_string'} = age_string($age);
	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($co{'committer_epoch'});
	if ($age > 60*60*24*7*2) {
		$co{'age_string_date'} = sprintf "%4i-%02u-%02i", 1900 + $year, $mon+1, $mday;
		$co{'age_string_age'} = $co{'age_string'};
	} else {
		$co{'age_string_date'} = $co{'age_string'};
		$co{'age_string_age'} = sprintf "%4i-%02u-%02i", 1900 + $year, $mon+1, $mday;
	}
	return %co;
}

# parse ref from ref_file, given by ref_id, with given type
sub parse_ref {
	my $ref_file = shift;
	my $ref_id = shift;
	my $type = shift || git_get_type($ref_id);
	my %ref_item;

	$ref_item{'type'} = $type;
	$ref_item{'id'} = $ref_id;
	$ref_item{'epoch'} = 0;
	$ref_item{'age'} = "unknown";
	if ($type eq "tag") {
		my %tag = parse_tag($ref_id);
		$ref_item{'comment'} = $tag{'comment'};
		if ($tag{'type'} eq "commit") {
			my %co = parse_commit($tag{'object'});
			$ref_item{'epoch'} = $co{'committer_epoch'};
			$ref_item{'age'} = $co{'age_string'};
		} elsif (defined($tag{'epoch'})) {
			my $age = time - $tag{'epoch'};
			$ref_item{'epoch'} = $tag{'epoch'};
			$ref_item{'age'} = age_string($age);
		}
		$ref_item{'reftype'} = $tag{'type'};
		$ref_item{'name'} = $tag{'name'};
		$ref_item{'refid'} = $tag{'object'};
	} elsif ($type eq "commit"){
		my %co = parse_commit($ref_id);
		$ref_item{'reftype'} = "commit";
		$ref_item{'name'} = $ref_file;
		$ref_item{'title'} = $co{'title'};
		$ref_item{'refid'} = $ref_id;
		$ref_item{'epoch'} = $co{'committer_epoch'};
		$ref_item{'age'} = $co{'age_string'};
	} else {
		$ref_item{'reftype'} = $type;
		$ref_item{'name'} = $ref_file;
		$ref_item{'refid'} = $ref_id;
	}

	return %ref_item;
}

# parse line of git-diff-tree "raw" output
sub parse_difftree_raw_line {
	my $line = shift;
	my %res;

	# ':100644 100644 03b218260e99b78c6df0ed378e59ed9205ccc96d 3b93d5e7cc7f7dd4ebed13a5cc1a4ad976fc94d8 M	ls-files.c'
	# ':100644 100644 7f9281985086971d3877aca27704f2aaf9c448ce bc190ebc71bbd923f2b728e505408f5e54bd073a M	rev-tree.c'
	if ($line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)([0-9]{0,3})\t(.*)$/) {
		$res{'from_mode'} = $1;
		$res{'to_mode'} = $2;
		$res{'from_id'} = $3;
		$res{'to_id'} = $4;
		$res{'status'} = $5;
		$res{'similarity'} = $6;
		if ($res{'status'} eq 'R' || $res{'status'} eq 'C') { # renamed or copied
			($res{'from_file'}, $res{'to_file'}) = map { unquote($_) } split("\t", $7);
		} else {
			$res{'file'} = unquote($7);
		}
	}
	# 'c512b523472485aef4fff9e57b229d9d243c967f'
	#elsif ($line =~ m/^([0-9a-fA-F]{40})$/) {
	#	$res{'commit'} = $1;
	#}

	return wantarray ? %res : \%res;
}

## ......................................................................
## parse to array of hashes functions

sub git_get_refs_list {
	my $ref_dir = shift;
	my @reflist;

	my @refs;
	my $pfxlen = length("$projectroot/$project/$ref_dir");
	File::Find::find(sub {
		return if (/^\./);
		if (-f $_) {
			push @refs, substr($File::Find::name, $pfxlen + 1);
		}
	}, "$projectroot/$project/$ref_dir");

	foreach my $ref_file (@refs) {
		my $ref_id = git_get_hash_by_ref("$project/$ref_dir/$ref_file");
		my $type = git_get_type($ref_id) || next;
		my %ref_item = parse_ref($ref_file, $ref_id, $type);

		push @reflist, \%ref_item;
	}
	# sort refs by age
	@reflist = sort {$b->{'epoch'} <=> $a->{'epoch'}} @reflist;
	return \@reflist;
}

## ----------------------------------------------------------------------
## filesystem-related functions

sub get_file_owner {
	my $path = shift;

	my ($dev, $ino, $mode, $nlink, $st_uid, $st_gid, $rdev, $size) = stat($path);
	my ($name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell) = getpwuid($st_uid);
	if (!defined $gcos) {
		return undef;
	}
	my $owner = $gcos;
	$owner =~ s/[,;].*$//;
	return decode("utf8", $owner, Encode::FB_DEFAULT);
}

## ......................................................................
## mimetype related functions

sub mimetype_guess_file {
	my $filename = shift;
	my $mimemap = shift;
	-r $mimemap or return undef;

	my %mimemap;
	open(MIME, $mimemap) or return undef;
	while (<MIME>) {
		next if m/^#/; # skip comments
		my ($mime, $exts) = split(/\t+/);
		if (defined $exts) {
			my @exts = split(/\s+/, $exts);
			foreach my $ext (@exts) {
				$mimemap{$ext} = $mime;
			}
		}
	}
	close(MIME);

	$filename =~ /\.(.*?)$/;
	return $mimemap{$1};
}

sub mimetype_guess {
	my $filename = shift;
	my $mime;
	$filename =~ /\./ or return undef;

	if ($mimetypes_file) {
		my $file = $mimetypes_file;
		if ($file !~ m!^/!) { # if it is relative path
			# it is relative to project
			$file = "$projectroot/$project/$file";
		}
		$mime = mimetype_guess_file($filename, $file);
	}
	$mime ||= mimetype_guess_file($filename, '/etc/mime.types');
	return $mime;
}

sub blob_mimetype {
	my $fd = shift;
	my $filename = shift;

	if ($filename) {
		my $mime = mimetype_guess($filename);
		$mime and return $mime;
	}

	# just in case
	return $default_blob_plain_mimetype unless $fd;

	if (-T $fd) {
		return 'text/plain' .
		       ($default_text_plain_charset ? '; charset='.$default_text_plain_charset : '');
	} elsif (! $filename) {
		return 'application/octet-stream';
	} elsif ($filename =~ m/\.png$/i) {
		return 'image/png';
	} elsif ($filename =~ m/\.gif$/i) {
		return 'image/gif';
	} elsif ($filename =~ m/\.jpe?g$/i) {
		return 'image/jpeg';
	} else {
		return 'application/octet-stream';
	}
}

## ======================================================================
## functions printing HTML: header, footer, error page

sub git_header_html {
	my $status = shift || "200 OK";
	my $expires = shift;

	my $title = "$site_name git";
	if (defined $project) {
		$title .= " - $project";
		if (defined $action) {
			$title .= "/$action";
			if (defined $file_name) {
				$title .= " - $file_name";
				if ($action eq "tree" && $file_name !~ m|/$|) {
					$title .= "/";
				}
			}
		}
	}
	my $content_type;
	# require explicit support from the UA if we are to send the page as
	# 'application/xhtml+xml', otherwise send it as plain old 'text/html'.
	# we have to do this because MSIE sometimes globs '*/*', pretending to
	# support xhtml+xml but choking when it gets what it asked for.
	if (defined $cgi->http('HTTP_ACCEPT') &&
	    $cgi->http('HTTP_ACCEPT') =~ m/(,|;|\s|^)application\/xhtml\+xml(,|;|\s|$)/ &&
	    $cgi->Accept('application/xhtml+xml') != 0) {
		$content_type = 'application/xhtml+xml';
	} else {
		$content_type = 'text/html';
	}
	print $cgi->header(-type=>$content_type, -charset => 'utf-8',
	                   -status=> $status, -expires => $expires);
	print <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US" lang="en-US">
<!-- git web interface version $version, (C) 2005-2006, Kay Sievers <kay.sievers\@vrfy.org>, Christian Gierke -->
<!-- git core binaries version $git_version -->
<head>
<meta http-equiv="content-type" content="$content_type; charset=utf-8"/>
<meta name="generator" content="gitweb/$version git/$git_version"/>
<meta name="robots" content="index, nofollow"/>
<title>$title</title>
<link rel="stylesheet" type="text/css" href="$stylesheet"/>
EOF
	if (defined $project) {
		printf('<link rel="alternate" title="%s log" '.
		       'href="%s" type="application/rss+xml"/>'."\n",
		       esc_param($project), href(action=>"rss"));
	}

	print "</head>\n" .
	      "<body>\n" .
	      "<div class=\"page_header\">\n" .
	      "<a href=\"http://www.kernel.org/pub/software/scm/git/docs/\" title=\"git documentation\">" .
	      "<img src=\"$logo\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/>" .
	      "</a>\n";
	print $cgi->a({-href => esc_param($home_link)}, $home_link_str) . " / ";
	if (defined $project) {
		print $cgi->a({-href => href(action=>"summary")}, esc_html($project));
		if (defined $action) {
			print " / $action";
		}
		print "\n";
		if (!defined $searchtext) {
			$searchtext = "";
		}
		my $search_hash;
		if (defined $hash_base) {
			$search_hash = $hash_base;
		} elsif (defined $hash) {
			$search_hash = $hash;
		} else {
			$search_hash = "HEAD";
		}
		$cgi->param("a", "search");
		$cgi->param("h", $search_hash);
		print $cgi->startform(-method => "get", -action => $my_uri) .
		      "<div class=\"search\">\n" .
		      $cgi->hidden(-name => "p") . "\n" .
		      $cgi->hidden(-name => "a") . "\n" .
		      $cgi->hidden(-name => "h") . "\n" .
		      $cgi->textfield(-name => "s", -value => $searchtext) . "\n" .
		      "</div>" .
		      $cgi->end_form() . "\n";
	}
	print "</div>\n";
}

sub git_footer_html {
	print "<div class=\"page_footer\">\n";
	if (defined $project) {
		my $descr = git_get_project_description($project);
		if (defined $descr) {
			print "<div class=\"page_footer_text\">" . esc_html($descr) . "</div>\n";
		}
		print $cgi->a({-href => href(action=>"rss"), -class => "rss_logo"}, "RSS") . "\n";
	} else {
		print $cgi->a({-href => href(action=>"opml"), -class => "rss_logo"}, "OPML") . "\n";
	}
	print "</div>\n" .
	      "</body>\n" .
	      "</html>";
}

sub die_error {
	my $status = shift || "403 Forbidden";
	my $error = shift || "Malformed query, file missing or permission denied";

	git_header_html($status);
	print <<EOF;
<div class="page_body">
<br /><br />
$status - $error
<br />
</div>
EOF
	git_footer_html();
	exit;
}

## ----------------------------------------------------------------------
## functions printing or outputting HTML: navigation

sub git_print_page_nav {
	my ($current, $suppress, $head, $treehead, $treebase, $extra) = @_;
	$extra = '' if !defined $extra; # pager or formats

	my @navs = qw(summary shortlog log commit commitdiff tree);
	if ($suppress) {
		@navs = grep { $_ ne $suppress } @navs;
	}

	my %arg = map { $_ => {action=>$_} } @navs;
	if (defined $head) {
		for (qw(commit commitdiff)) {
			$arg{$_}{hash} = $head;
		}
		if ($current =~ m/^(tree | log | shortlog | commit | commitdiff | search)$/x) {
			for (qw(shortlog log)) {
				$arg{$_}{hash} = $head;
			}
		}
	}
	$arg{tree}{hash} = $treehead if defined $treehead;
	$arg{tree}{hash_base} = $treebase if defined $treebase;

	print "<div class=\"page_nav\">\n" .
		(join " | ",
		 map { $_ eq $current ?
		       $_ : $cgi->a({-href => href(%{$arg{$_}})}, "$_")
		 } @navs);
	print "<br/>\n$extra<br/>\n" .
	      "</div>\n";
}

sub format_paging_nav {
	my ($action, $hash, $head, $page, $nrevs) = @_;
	my $paging_nav;


	if ($hash ne $head || $page) {
		$paging_nav .= $cgi->a({-href => href(action=>$action)}, "HEAD");
	} else {
		$paging_nav .= "HEAD";
	}

	if ($page > 0) {
		$paging_nav .= " &sdot; " .
			$cgi->a({-href => href(action=>$action, hash=>$hash, page=>$page-1),
			         -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		$paging_nav .= " &sdot; prev";
	}

	if ($nrevs >= (100 * ($page+1)-1)) {
		$paging_nav .= " &sdot; " .
			$cgi->a({-href => href(action=>$action, hash=>$hash, page=>$page+1),
			         -accesskey => "n", -title => "Alt-n"}, "next");
	} else {
		$paging_nav .= " &sdot; next";
	}

	return $paging_nav;
}

## ......................................................................
## functions printing or outputting HTML: div

sub git_print_header_div {
	my ($action, $title, $hash, $hash_base) = @_;
	my %args = ();

	$args{action} = $action;
	$args{hash} = $hash if $hash;
	$args{hash_base} = $hash_base if $hash_base;

	print "<div class=\"header\">\n" .
	      $cgi->a({-href => href(%args), -class => "title"},
	      $title ? $title : $action) .
	      "\n</div>\n";
}

sub git_print_page_path {
	my $name = shift;
	my $type = shift;
	my $hb = shift;

	if (!defined $name) {
		print "<div class=\"page_path\">/</div>\n";
	} elsif (defined $type && $type eq 'blob') {
		print "<div class=\"page_path\">";
		if (defined $hb) {
			print $cgi->a({-href => href(action=>"blob_plain", file_name=>$file_name,
			                             hash_base=>$hb)},
			              esc_html($name));
		} else {
			print $cgi->a({-href => href(action=>"blob_plain", file_name=>$file_name)},
			              esc_html($name));
		}
		print "<br/></div>\n";
	} else {
		print "<div class=\"page_path\">" . esc_html($name) . "<br/></div>\n";
	}
}

sub git_print_log {
	my $log = shift;

	# remove leading empty lines
	while (defined $log->[0] && $log->[0] eq "") {
		shift @$log;
	}

	# print log
	my $signoff = 0;
	my $empty = 0;
	foreach my $line (@$log) {
		# print only one empty line
		# do not print empty line after signoff
		if ($line eq "") {
			next if ($empty || $signoff);
			$empty = 1;
		} else {
			$empty = 0;
		}
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			$signoff = 1;
			print "<span class=\"signoff\">" . esc_html($line) . "</span><br/>\n";
		} else {
			$signoff = 0;
			print format_log_line_html($line) . "<br/>\n";
		}
	}
}

sub git_print_simplified_log {
	my $log = shift;
	my $remove_title = shift;

	shift @$log if $remove_title;
	# remove leading empty lines
	while (defined $log->[0] && $log->[0] eq "") {
		shift @$log;
	}

	# simplify and print log
	my $empty = 0;
	foreach my $line (@$log) {
		# remove signoff lines
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			next;
		}
		# print only one empty line
		if ($line eq "") {
			next if $empty;
			$empty = 1;
		} else {
			$empty = 0;
		}
		print format_log_line_html($line) . "<br/>\n";
	}
	# end with single empty line
	print "<br/>\n" unless $empty;
}

## ......................................................................
## functions printing large fragments of HTML

sub git_difftree_body {
	my ($difftree, $parent) = @_;

	print "<div class=\"list_head\">\n";
	if ($#{$difftree} > 10) {
		print(($#{$difftree} + 1) . " files changed:\n");
	}
	print "</div>\n";

	print "<table class=\"diff_tree\">\n";
	my $alternate = 0;
	foreach my $line (@{$difftree}) {
		my %diff = parse_difftree_raw_line($line);

		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;

		my ($to_mode_oct, $to_mode_str, $to_file_type);
		my ($from_mode_oct, $from_mode_str, $from_file_type);
		if ($diff{'to_mode'} ne ('0' x 6)) {
			$to_mode_oct = oct $diff{'to_mode'};
			if (S_ISREG($to_mode_oct)) { # only for regular file
				$to_mode_str = sprintf("%04o", $to_mode_oct & 0777); # permission bits
			}
			$to_file_type = file_type($diff{'to_mode'});
		}
		if ($diff{'from_mode'} ne ('0' x 6)) {
			$from_mode_oct = oct $diff{'from_mode'};
			if (S_ISREG($to_mode_oct)) { # only for regular file
				$from_mode_str = sprintf("%04o", $from_mode_oct & 0777); # permission bits
			}
			$from_file_type = file_type($diff{'from_mode'});
		}

		if ($diff{'status'} eq "A") { # created
			my $mode_chng = "<span class=\"file_status new\">[new $to_file_type";
			$mode_chng   .= " with mode: $to_mode_str" if $to_mode_str;
			$mode_chng   .= "]</span>";
			print "<td>" .
			      $cgi->a({-href => href(action=>"blob", hash=>$diff{'to_id'},
			                             hash_base=>$hash, file_name=>$diff{'file'}),
			              -class => "list"}, esc_html($diff{'file'})) .
			      "</td>\n" .
			      "<td>$mode_chng</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"blob", hash=>$diff{'to_id'},
			                             hash_base=>$hash, file_name=>$diff{'file'})},
			              "blob") .
			      "</td>\n";

		} elsif ($diff{'status'} eq "D") { # deleted
			my $mode_chng = "<span class=\"file_status deleted\">[deleted $from_file_type]</span>";
			print "<td>" .
			      $cgi->a({-href => href(action=>"blob", hash=>$diff{'from_id'},
			                             hash_base=>$parent, file_name=>$diff{'file'}),
			               -class => "list"}, esc_html($diff{'file'})) .
			      "</td>\n" .
			      "<td>$mode_chng</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"blob", hash=>$diff{'from_id'},
			                             hash_base=>$parent, file_name=>$diff{'file'})},
			              "blob") .
			      " | " .
			      $cgi->a({-href => href(action=>"history", hash_base=>$parent,
			                             file_name=>$diff{'file'})},\
			              "history") .
			      "</td>\n";

		} elsif ($diff{'status'} eq "M" || $diff{'status'} eq "T") { # modified, or type changed
			my $mode_chnge = "";
			if ($diff{'from_mode'} != $diff{'to_mode'}) {
				$mode_chnge = "<span class=\"file_status mode_chnge\">[changed";
				if ($from_file_type != $to_file_type) {
					$mode_chnge .= " from $from_file_type to $to_file_type";
				}
				if (($from_mode_oct & 0777) != ($to_mode_oct & 0777)) {
					if ($from_mode_str && $to_mode_str) {
						$mode_chnge .= " mode: $from_mode_str->$to_mode_str";
					} elsif ($to_mode_str) {
						$mode_chnge .= " mode: $to_mode_str";
					}
				}
				$mode_chnge .= "]</span>\n";
			}
			print "<td>";
			if ($diff{'to_id'} ne $diff{'from_id'}) { # modified
				print $cgi->a({-href => href(action=>"blobdiff", hash=>$diff{'to_id'}, hash_parent=>$diff{'from_id'},
				                             hash_base=>$hash, file_name=>$diff{'file'}),
				              -class => "list"}, esc_html($diff{'file'}));
			} else { # only mode changed
				print $cgi->a({-href => href(action=>"blob", hash=>$diff{'to_id'},
				                             hash_base=>$hash, file_name=>$diff{'file'}),
				              -class => "list"}, esc_html($diff{'file'}));
			}
			print "</td>\n" .
			      "<td>$mode_chnge</td>\n" .
			      "<td class=\"link\">" .
				$cgi->a({-href => href(action=>"blob", hash=>$diff{'to_id'},
				                       hash_base=>$hash, file_name=>$diff{'file'})},
				        "blob");
			if ($diff{'to_id'} ne $diff{'from_id'}) { # modified
				print " | " .
					$cgi->a({-href => href(action=>"blobdiff", hash=>$diff{'to_id'}, hash_parent=>$diff{'from_id'},
					                       hash_base=>$hash, file_name=>$diff{'file'})},
					        "diff");
			}
			print " | " .
				$cgi->a({-href => href(action=>"history",
				                       hash_base=>$hash, file_name=>$diff{'file'})},
				        "history");
			print "</td>\n";

		} elsif ($diff{'status'} eq "R" || $diff{'status'} eq "C") { # renamed or copied
			my %status_name = ('R' => 'moved', 'C' => 'copied');
			my $nstatus = $status_name{$diff{'status'}};
			my $mode_chng = "";
			if ($diff{'from_mode'} != $diff{'to_mode'}) {
				# mode also for directories, so we cannot use $to_mode_str
				$mode_chng = sprintf(", mode: %04o", $to_mode_oct & 0777);
			}
			print "<td>" .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$hash,
			                             hash=>$diff{'to_id'}, file_name=>$diff{'to_file'}),
			              -class => "list"}, esc_html($diff{'to_file'})) . "</td>\n" .
			      "<td><span class=\"file_status $nstatus\">[$nstatus from " .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$parent,
			                             hash=>$diff{'from_id'}, file_name=>$diff{'from_file'}),
			              -class => "list"}, esc_html($diff{'from_file'})) .
			      " with " . (int $diff{'similarity'}) . "% similarity$mode_chng]</span></td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$hash,
			                             hash=>$diff{'to_id'}, file_name=>$diff{'to_file'})},
			              "blob");
			if ($diff{'to_id'} ne $diff{'from_id'}) {
				print " | " .
					$cgi->a({-href => href(action=>"blobdiff", hash_base=>$hash,
					                       hash=>$diff{'to_id'}, hash_parent=>$diff{'from_id'},
					                       file_name=>$diff{'to_file'}, file_parent=>$diff{'from_file'})},
					        "diff");
			}
			print "</td>\n";

		} # we should not encounter Unmerged (U) or Unknown (X) status
		print "</tr>\n";
	}
	print "</table>\n";
}

sub git_shortlog_body {
	# uses global variable $project
	my ($revlist, $from, $to, $refs, $extra) = @_;

	my ($ctype, $suffix, $command) = gitweb_check_feature('snapshot');
	my $have_snapshot = (defined $ctype && defined $suffix);

	$from = 0 unless defined $from;
	$to = $#{$revlist} if (!defined $to || $#{$revlist} < $to);

	print "<table class=\"shortlog\" cellspacing=\"0\">\n";
	my $alternate = 0;
	for (my $i = $from; $i <= $to; $i++) {
		my $commit = $revlist->[$i];
		#my $ref = defined $refs ? format_ref_marker($refs, $commit) : '';
		my $ref = format_ref_marker($refs, $commit);
		my %co = parse_commit($commit);
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		# git_summary() used print "<td><i>$co{'age_string'}</i></td>\n" .
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      "<td><i>" . esc_html(chop_str($co{'author_name'}, 10)) . "</i></td>\n" .
		      "<td>";
		print format_subject_html($co{'title'}, $co{'title_short'},
		                          href(action=>"commit", hash=>$commit), $ref);
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$commit)}, "commit") . " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff");
		if ($have_snapshot) {
			print " | " .  $cgi->a({-href => href(action=>"snapshot", hash=>$commit)}, "snapshot");
		}
		print "</td>\n" .
		      "</tr>\n";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"4\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_history_body {
	# Warning: assumes constant type (blob or tree) during history
	my ($fd, $refs, $hash_base, $ftype, $extra) = @_;

	print "<table class=\"history\" cellspacing=\"0\">\n";
	my $alternate = 0;
	while (my $line = <$fd>) {
		if ($line !~ m/^([0-9a-fA-F]{40})/) {
			next;
		}

		my $commit = $1;
		my %co = parse_commit($commit);
		if (!%co) {
			next;
		}

		my $ref = format_ref_marker($refs, $commit);

		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      # shortlog uses      chop_str($co{'author_name'}, 10)
		      "<td><i>" . esc_html(chop_str($co{'author_name'}, 15, 3)) . "</i></td>\n" .
		      "<td>";
		# originally git_history used chop_str($co{'title'}, 50)
		print format_subject_html($co{'title'}, $co{'title_short'},
		                          href(action=>"commit", hash=>$commit), $ref);
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$commit)}, "commit") . " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff") . " | " .
		      $cgi->a({-href => href(action=>$ftype, hash_base=>$commit, file_name=>$file_name)}, $ftype);

		if ($ftype eq 'blob') {
			my $blob_current = git_get_hash_by_path($hash_base, $file_name);
			my $blob_parent  = git_get_hash_by_path($commit, $file_name);
			if (defined $blob_current && defined $blob_parent &&
					$blob_current ne $blob_parent) {
				print " | " .
					$cgi->a({-href => href(action=>"blobdiff", hash=>$blob_current, hash_parent=>$blob_parent,
					                       hash_base=>$commit, file_name=>$file_name)},
					        "diff to current");
			}
		}
		print "</td>\n" .
		      "</tr>\n";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"4\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_tags_body {
	# uses global variable $project
	my ($taglist, $from, $to, $extra) = @_;
	$from = 0 unless defined $from;
	$to = $#{$taglist} if (!defined $to || $#{$taglist} < $to);

	print "<table class=\"tags\" cellspacing=\"0\">\n";
	my $alternate = 0;
	for (my $i = $from; $i <= $to; $i++) {
		my $entry = $taglist->[$i];
		my %tag = %$entry;
		my $comment_lines = $tag{'comment'};
		my $comment = shift @$comment_lines;
		my $comment_short;
		if (defined $comment) {
			$comment_short = chop_str($comment, 30, 5);
		}
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td><i>$tag{'age'}</i></td>\n" .
		      "<td>" .
		      $cgi->a({-href => href(action=>$tag{'reftype'}, hash=>$tag{'refid'}),
		               -class => "list name"}, esc_html($tag{'name'})) .
		      "</td>\n" .
		      "<td>";
		if (defined $comment) {
			print format_subject_html($comment, $comment_short,
			                          href(action=>"tag", hash=>$tag{'id'}));
		}
		print "</td>\n" .
		      "<td class=\"selflink\">";
		if ($tag{'type'} eq "tag") {
			print $cgi->a({-href => href(action=>"tag", hash=>$tag{'id'})}, "tag");
		} else {
			print "&nbsp;";
		}
		print "</td>\n" .
		      "<td class=\"link\">" . " | " .
		      $cgi->a({-href => href(action=>$tag{'reftype'}, hash=>$tag{'refid'})}, $tag{'reftype'});
		if ($tag{'reftype'} eq "commit") {
			print " | " . $cgi->a({-href => href(action=>"shortlog", hash=>$tag{'name'})}, "shortlog") .
			      " | " . $cgi->a({-href => href(action=>"log", hash=>$tag{'refid'})}, "log");
		} elsif ($tag{'reftype'} eq "blob") {
			print " | " . $cgi->a({-href => href(action=>"blob_plain", hash=>$tag{'refid'})}, "raw");
		}
		print "</td>\n" .
		      "</tr>";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"5\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_heads_body {
	# uses global variable $project
	my ($taglist, $head, $from, $to, $extra) = @_;
	$from = 0 unless defined $from;
	$to = $#{$taglist} if (!defined $to || $#{$taglist} < $to);

	print "<table class=\"heads\" cellspacing=\"0\">\n";
	my $alternate = 0;
	for (my $i = $from; $i <= $to; $i++) {
		my $entry = $taglist->[$i];
		my %tag = %$entry;
		my $curr = $tag{'id'} eq $head;
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td><i>$tag{'age'}</i></td>\n" .
		      ($tag{'id'} eq $head ? "<td class=\"current_head\">" : "<td>") .
		      $cgi->a({-href => href(action=>"shortlog", hash=>$tag{'name'}),
		               -class => "list name"},esc_html($tag{'name'})) .
		      "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"shortlog", hash=>$tag{'name'})}, "shortlog") . " | " .
		      $cgi->a({-href => href(action=>"log", hash=>$tag{'name'})}, "log") .
		      "</td>\n" .
		      "</tr>";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"3\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

## ----------------------------------------------------------------------
## functions printing large fragments, format as one of arguments

sub git_diff_print {
	my $from = shift;
	my $from_name = shift;
	my $to = shift;
	my $to_name = shift;
	my $format = shift || "html";

	my $from_tmp = "/dev/null";
	my $to_tmp = "/dev/null";
	my $pid = $$;

	# create tmp from-file
	if (defined $from) {
		$from_tmp = "$git_temp/gitweb_" . $$ . "_from";
		open my $fd2, "> $from_tmp";
		open my $fd, "-|", $GIT, "cat-file", "blob", $from;
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	# create tmp to-file
	if (defined $to) {
		$to_tmp = "$git_temp/gitweb_" . $$ . "_to";
		open my $fd2, "> $to_tmp";
		open my $fd, "-|", $GIT, "cat-file", "blob", $to;
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	open my $fd, "-|", "/usr/bin/diff -u -p -L \'$from_name\' -L \'$to_name\' $from_tmp $to_tmp";
	if ($format eq "plain") {
		undef $/;
		print <$fd>;
		$/ = "\n";
	} else {
		while (my $line = <$fd>) {
			chomp $line;
			my $char = substr($line, 0, 1);
			my $diff_class = "";
			if ($char eq '+') {
				$diff_class = " add";
			} elsif ($char eq "-") {
				$diff_class = " rem";
			} elsif ($char eq "@") {
				$diff_class = " chunk_header";
			} elsif ($char eq "\\") {
				# skip errors
				next;
			}
			$line = untabify($line);
			print "<div class=\"diff$diff_class\">" . esc_html($line) . "</div>\n";
		}
	}
	close $fd;

	if (defined $from) {
		unlink($from_tmp);
	}
	if (defined $to) {
		unlink($to_tmp);
	}
}


## ======================================================================
## ======================================================================
## actions

sub git_project_list {
	my $order = $cgi->param('o');
	if (defined $order && $order !~ m/project|descr|owner|age/) {
		die_error(undef, "Unknown order parameter");
	}

	my @list = git_get_projects_list();
	my @projects;
	if (!@list) {
		die_error(undef, "No projects found");
	}
	foreach my $pr (@list) {
		my $head = git_get_head_hash($pr->{'path'});
		if (!defined $head) {
			next;
		}
		$ENV{'GIT_DIR'} = "$projectroot/$pr->{'path'}";
		my %co = parse_commit($head);
		if (!%co) {
			next;
		}
		$pr->{'commit'} = \%co;
		if (!defined $pr->{'descr'}) {
			my $descr = git_get_project_description($pr->{'path'}) || "";
			$pr->{'descr'} = chop_str($descr, 25, 5);
		}
		if (!defined $pr->{'owner'}) {
			$pr->{'owner'} = get_file_owner("$projectroot/$pr->{'path'}") || "";
		}
		push @projects, $pr;
	}

	git_header_html();
	if (-f $home_text) {
		print "<div class=\"index_include\">\n";
		open (my $fd, $home_text);
		print <$fd>;
		close $fd;
		print "</div>\n";
	}
	print "<table class=\"project_list\">\n" .
	      "<tr>\n";
	$order ||= "project";
	if ($order eq "project") {
		@projects = sort {$a->{'path'} cmp $b->{'path'}} @projects;
		print "<th>Project</th>\n";
	} else {
		print "<th>" .
		      $cgi->a({-href => "$my_uri?" . esc_param("o=project"),
		               -class => "header"}, "Project") .
		      "</th>\n";
	}
	if ($order eq "descr") {
		@projects = sort {$a->{'descr'} cmp $b->{'descr'}} @projects;
		print "<th>Description</th>\n";
	} else {
		print "<th>" .
		      $cgi->a({-href => "$my_uri?" . esc_param("o=descr"),
		               -class => "header"}, "Description") .
		      "</th>\n";
	}
	if ($order eq "owner") {
		@projects = sort {$a->{'owner'} cmp $b->{'owner'}} @projects;
		print "<th>Owner</th>\n";
	} else {
		print "<th>" .
		      $cgi->a({-href => "$my_uri?" . esc_param("o=owner"),
		               -class => "header"}, "Owner") .
		      "</th>\n";
	}
	if ($order eq "age") {
		@projects = sort {$a->{'commit'}{'age'} <=> $b->{'commit'}{'age'}} @projects;
		print "<th>Last Change</th>\n";
	} else {
		print "<th>" .
		      $cgi->a({-href => "$my_uri?" . esc_param("o=age"),
		               -class => "header"}, "Last Change") .
		      "</th>\n";
	}
	print "<th></th>\n" .
	      "</tr>\n";
	my $alternate = 0;
	foreach my $pr (@projects) {
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td>" . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary"),
		                        -class => "list"}, esc_html($pr->{'path'})) . "</td>\n" .
		      "<td>" . esc_html($pr->{'descr'}) . "</td>\n" .
		      "<td><i>" . chop_str($pr->{'owner'}, 15) . "</i></td>\n";
		print "<td class=\"". age_class($pr->{'commit'}{'age'}) . "\">" .
		      $pr->{'commit'}{'age_string'} . "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary")}, "summary")   . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"shortlog")}, "shortlog") . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"log")}, "log") .
		      "</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
	git_footer_html();
}

sub git_summary {
	my $descr = git_get_project_description($project) || "none";
	my $head = git_get_head_hash($project);
	my %co = parse_commit($head);
	my %cd = parse_date($co{'committer_epoch'}, $co{'committer_tz'});

	my $owner = git_get_project_owner($project);

	my $refs = git_get_references();
	git_header_html();
	git_print_page_nav('summary','', $head);

	print "<div class=\"title\">&nbsp;</div>\n";
	print "<table cellspacing=\"0\">\n" .
	      "<tr><td>description</td><td>" . esc_html($descr) . "</td></tr>\n" .
	      "<tr><td>owner</td><td>$owner</td></tr>\n" .
	      "<tr><td>last change</td><td>$cd{'rfc2822'}</td></tr>\n";
	# use per project git URL list in $projectroot/$project/cloneurl
	# or make project git URL from git base URL and project name
	my $url_tag = "URL";
	my @url_list = git_get_project_url_list($project);
	@url_list = map { "$_/$project" } @git_base_url_list unless @url_list;
	foreach my $git_url (@url_list) {
		next unless $git_url;
		print "<tr><td>$url_tag</td><td>$git_url</td></tr>\n";
		$url_tag = "";
	}
	print "</table>\n";

	open my $fd, "-|", $GIT, "rev-list", "--max-count=17", git_get_head_hash($project)
		or die_error(undef, "Open git-rev-list failed");
	my @revlist = map { chomp; $_ } <$fd>;
	close $fd;
	git_print_header_div('shortlog');
	git_shortlog_body(\@revlist, 0, 15, $refs,
	                  $cgi->a({-href => href(action=>"shortlog")}, "..."));

	my $taglist = git_get_refs_list("refs/tags");
	if (defined @$taglist) {
		git_print_header_div('tags');
		git_tags_body($taglist, 0, 15,
		              $cgi->a({-href => href(action=>"tags")}, "..."));
	}

	my $headlist = git_get_refs_list("refs/heads");
	if (defined @$headlist) {
		git_print_header_div('heads');
		git_heads_body($headlist, $head, 0, 15,
		               $cgi->a({-href => href(action=>"heads")}, "..."));
	}

	git_footer_html();
}

sub git_tag {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
	my %tag = parse_tag($hash);
	git_print_header_div('commit', esc_html($tag{'name'}), $hash);
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">\n" .
	      "<tr>\n" .
	      "<td>object</td>\n" .
	      "<td>" . $cgi->a({-class => "list", -href => href(action=>$tag{'type'}, hash=>$tag{'object'})},
	                       $tag{'object'}) . "</td>\n" .
	      "<td class=\"link\">" . $cgi->a({-href => href(action=>$tag{'type'}, hash=>$tag{'object'})},
	                                      $tag{'type'}) . "</td>\n" .
	      "</tr>\n";
	if (defined($tag{'author'})) {
		my %ad = parse_date($tag{'epoch'}, $tag{'tz'});
		print "<tr><td>author</td><td>" . esc_html($tag{'author'}) . "</td></tr>\n";
		print "<tr><td></td><td>" . $ad{'rfc2822'} .
			sprintf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'}) .
			"</td></tr>\n";
	}
	print "</table>\n\n" .
	      "</div>\n";
	print "<div class=\"page_body\">";
	my $comment = $tag{'comment'};
	foreach my $line (@$comment) {
		print esc_html($line) . "<br/>\n";
	}
	print "</div>\n";
	git_footer_html();
}

sub git_blame2 {
	my $fd;
	my $ftype;

	if (!gitweb_check_feature('blame')) {
		die_error('403 Permission denied', "Permission denied");
	}
	die_error('404 Not Found', "File name not defined") if (!$file_name);
	$hash_base ||= git_get_head_hash($project);
	die_error(undef, "Couldn't find base commit") unless ($hash_base);
	my %co = parse_commit($hash_base)
		or die_error(undef, "Reading commit failed");
	if (!defined $hash) {
		$hash = git_get_hash_by_path($hash_base, $file_name, "blob")
			or die_error(undef, "Error looking up file");
	}
	$ftype = git_get_type($hash);
	if ($ftype !~ "blob") {
		die_error("400 Bad Request", "Object is not a blob");
	}
	open ($fd, "-|", $GIT, "blame", '-l', $file_name, $hash_base)
		or die_error(undef, "Open git-blame failed");
	git_header_html();
	my $formats_nav =
		$cgi->a({-href => href(action=>"blob", hash=>$hash, hash_base=>$hash_base, file_name=>$file_name)},
		        "blob") .
		" | " .
		$cgi->a({-href => href(action=>"blame", file_name=>$file_name)},
		        "head");
	git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	git_print_page_path($file_name, $ftype, $hash_base);
	my @rev_color = (qw(light2 dark2));
	my $num_colors = scalar(@rev_color);
	my $current_color = 0;
	my $last_rev;
	print <<HTML;
<div class="page_body">
<table class="blame">
<tr><th>Commit</th><th>Line</th><th>Data</th></tr>
HTML
	while (<$fd>) {
		/^([0-9a-fA-F]{40}).*?(\d+)\)\s{1}(\s*.*)/;
		my $full_rev = $1;
		my $rev = substr($full_rev, 0, 8);
		my $lineno = $2;
		my $data = $3;

		if (!defined $last_rev) {
			$last_rev = $full_rev;
		} elsif ($last_rev ne $full_rev) {
			$last_rev = $full_rev;
			$current_color = ++$current_color % $num_colors;
		}
		print "<tr class=\"$rev_color[$current_color]\">\n";
		print "<td class=\"sha1\">" .
			$cgi->a({-href => href(action=>"commit", hash=>$full_rev, file_name=>$file_name)},
			        esc_html($rev)) . "</td>\n";
		print "<td class=\"linenr\"><a id=\"l$lineno\" href=\"#l$lineno\" class=\"linenr\">" .
		      esc_html($lineno) . "</a></td>\n";
		print "<td class=\"pre\">" . esc_html($data) . "</td>\n";
		print "</tr>\n";
	}
	print "</table>\n";
	print "</div>";
	close $fd
		or print "Reading blob failed\n";
	git_footer_html();
}

sub git_blame {
	my $fd;

	if (!gitweb_check_feature('blame')) {
		die_error('403 Permission denied', "Permission denied");
	}
	die_error('404 Not Found', "File name not defined") if (!$file_name);
	$hash_base ||= git_get_head_hash($project);
	die_error(undef, "Couldn't find base commit") unless ($hash_base);
	my %co = parse_commit($hash_base)
		or die_error(undef, "Reading commit failed");
	if (!defined $hash) {
		$hash = git_get_hash_by_path($hash_base, $file_name, "blob")
			or die_error(undef, "Error lookup file");
	}
	open ($fd, "-|", $GIT, "annotate", '-l', '-t', '-r', $file_name, $hash_base)
		or die_error(undef, "Open git-annotate failed");
	git_header_html();
	my $formats_nav =
		$cgi->a({-href => href(action=>"blob", hash=>$hash, hash_base=>$hash_base, file_name=>$file_name)},
		        "blob") .
		" | " .
		$cgi->a({-href => href(action=>"blame", file_name=>$file_name)},
		        "head");
	git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	git_print_page_path($file_name, 'blob', $hash_base);
	print "<div class=\"page_body\">\n";
	print <<HTML;
<table class="blame">
  <tr>
    <th>Commit</th>
    <th>Age</th>
    <th>Author</th>
    <th>Line</th>
    <th>Data</th>
  </tr>
HTML
	my @line_class = (qw(light dark));
	my $line_class_len = scalar (@line_class);
	my $line_class_num = $#line_class;
	while (my $line = <$fd>) {
		my $long_rev;
		my $short_rev;
		my $author;
		my $time;
		my $lineno;
		my $data;
		my $age;
		my $age_str;
		my $age_class;

		chomp $line;
		$line_class_num = ($line_class_num + 1) % $line_class_len;

		if ($line =~ m/^([0-9a-fA-F]{40})\t\(\s*([^\t]+)\t(\d+) \+\d\d\d\d\t(\d+)\)(.*)$/) {
			$long_rev = $1;
			$author   = $2;
			$time     = $3;
			$lineno   = $4;
			$data     = $5;
		} else {
			print qq(  <tr><td colspan="5" class="error">Unable to parse: $line</td></tr>\n);
			next;
		}
		$short_rev  = substr ($long_rev, 0, 8);
		$age        = time () - $time;
		$age_str    = age_string ($age);
		$age_str    =~ s/ /&nbsp;/g;
		$age_class  = age_class($age);
		$author     = esc_html ($author);
		$author     =~ s/ /&nbsp;/g;

		$data = untabify($data);
		$data = esc_html ($data);

		print <<HTML;
  <tr class="$line_class[$line_class_num]">
    <td class="sha1"><a href="${\href (action=>"commit", hash=>$long_rev)}" class="text">$short_rev..</a></td>
    <td class="$age_class">$age_str</td>
    <td>$author</td>
    <td class="linenr"><a id="$lineno" href="#$lineno" class="linenr">$lineno</a></td>
    <td class="pre">$data</td>
  </tr>
HTML
	} # while (my $line = <$fd>)
	print "</table>\n\n";
	close $fd
		or print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub git_tags {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
	git_print_header_div('summary', $project);

	my $taglist = git_get_refs_list("refs/tags");
	if (defined @$taglist) {
		git_tags_body($taglist);
	}
	git_footer_html();
}

sub git_heads {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
	git_print_header_div('summary', $project);

	my $taglist = git_get_refs_list("refs/heads");
	if (defined @$taglist) {
		git_heads_body($taglist, $head);
	}
	git_footer_html();
}

sub git_blob_plain {
	if (!defined $hash) {
		if (defined $file_name) {
			my $base = $hash_base || git_get_head_hash($project);
			$hash = git_get_hash_by_path($base, $file_name, "blob")
				or die_error(undef, "Error lookup file");
		} else {
			die_error(undef, "No file name defined");
		}
	}
	my $type = shift;
	open my $fd, "-|", $GIT, "cat-file", "blob", $hash
		or die_error(undef, "Couldn't cat $file_name, $hash");

	$type ||= blob_mimetype($fd, $file_name);

	# save as filename, even when no $file_name is given
	my $save_as = "$hash";
	if (defined $file_name) {
		$save_as = $file_name;
	} elsif ($type =~ m/^text\//) {
		$save_as .= '.txt';
	}

	print $cgi->header(-type => "$type",
	                   -content_disposition => "inline; filename=\"$save_as\"");
	undef $/;
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	$/ = "\n";
	close $fd;
}

sub git_blob {
	if (!defined $hash) {
		if (defined $file_name) {
			my $base = $hash_base || git_get_head_hash($project);
			$hash = git_get_hash_by_path($base, $file_name, "blob")
				or die_error(undef, "Error lookup file");
		} else {
			die_error(undef, "No file name defined");
		}
	}
	my $have_blame = gitweb_check_feature('blame');
	open my $fd, "-|", $GIT, "cat-file", "blob", $hash
		or die_error(undef, "Couldn't cat $file_name, $hash");
	my $mimetype = blob_mimetype($fd, $file_name);
	if ($mimetype !~ m/^text\//) {
		close $fd;
		return git_blob_plain($mimetype);
	}
	git_header_html();
	my $formats_nav = '';
	if (defined $hash_base && (my %co = parse_commit($hash_base))) {
		if (defined $file_name) {
			if ($have_blame) {
				$formats_nav .=
					$cgi->a({-href => href(action=>"blame", hash_base=>$hash_base,
					                       hash=>$hash, file_name=>$file_name)},
					        "blame") .
					" | ";
			}
			$formats_nav .=
				$cgi->a({-href => href(action=>"blob_plain",
				                       hash=>$hash, file_name=>$file_name)},
				        "plain") .
				" | " .
				$cgi->a({-href => href(action=>"blob",
				                       hash_base=>"HEAD", file_name=>$file_name)},
				        "head");
		} else {
			$formats_nav .=
				$cgi->a({-href => href(action=>"blob_plain", hash=>$hash)}, "plain");
		}
		git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
		git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	} else {
		print "<div class=\"page_nav\">\n" .
		      "<br/><br/></div>\n" .
		      "<div class=\"title\">$hash</div>\n";
	}
	git_print_page_path($file_name, "blob", $hash_base);
	print "<div class=\"page_body\">\n";
	my $nr;
	while (my $line = <$fd>) {
		chomp $line;
		$nr++;
		$line = untabify($line);
		printf "<div class=\"pre\"><a id=\"l%i\" href=\"#l%i\" class=\"linenr\">%4i</a> %s</div>\n",
		       $nr, $nr, $nr, esc_html($line);
	}
	close $fd
		or print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub git_tree {
	if (!defined $hash) {
		$hash = git_get_head_hash($project);
		if (defined $file_name) {
			my $base = $hash_base || $hash;
			$hash = git_get_hash_by_path($base, $file_name, "tree");
		}
		if (!defined $hash_base) {
			$hash_base = $hash;
		}
	}
	$/ = "\0";
	open my $fd, "-|", $GIT, "ls-tree", '-z', $hash
		or die_error(undef, "Open git-ls-tree failed");
	my @entries = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading tree failed");
	$/ = "\n";

	my $refs = git_get_references();
	my $ref = format_ref_marker($refs, $hash_base);
	git_header_html();
	my %base_key = ();
	my $base = "";
	my $have_blame = gitweb_check_feature('blame');
	if (defined $hash_base && (my %co = parse_commit($hash_base))) {
		$base_key{hash_base} = $hash_base;
		git_print_page_nav('tree','', $hash_base);
		git_print_header_div('commit', esc_html($co{'title'}) . $ref, $hash_base);
	} else {
		print "<div class=\"page_nav\">\n";
		print "<br/><br/></div>\n";
		print "<div class=\"title\">$hash</div>\n";
	}
	if (defined $file_name) {
		$base = esc_html("$file_name/");
	}
	git_print_page_path($file_name, 'tree', $hash_base);
	print "<div class=\"page_body\">\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $line (@entries) {
		#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t(.+)$/;
		my $t_mode = $1;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = validate_input($4);
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td class=\"mode\">" . mode_str($t_mode) . "</td>\n";
		if ($t_type eq "blob") {
			print "<td class=\"list\">" .
			      $cgi->a({-href => href(action=>"blob", hash=>$t_hash, file_name=>"$base$t_name", %base_key),
			              -class => "list"}, esc_html($t_name)) .
			      "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"blob", hash=>$t_hash, file_name=>"$base$t_name", %base_key)},
			              "blob");
			if ($have_blame) {
				print " | " .
					$cgi->a({-href => href(action=>"blame", hash=>$t_hash, file_name=>"$base$t_name", %base_key)},
					        "blame");
			}
			print " | " .
			      $cgi->a({-href => href(action=>"history", hash_base=>$hash_base,
			                             hash=>$t_hash, file_name=>"$base$t_name")},
			              "history") .
			      " | " .
			      $cgi->a({-href => href(action=>"blob_plain",
			                             hash=>$t_hash, file_name=>"$base$t_name")},
			              "raw") .
			      "</td>\n";
		} elsif ($t_type eq "tree") {
			print "<td class=\"list\">" .
			      $cgi->a({-href => href(action=>"tree", hash=>$t_hash, file_name=>"$base$t_name", %base_key)},
			              esc_html($t_name)) .
			      "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"tree", hash=>$t_hash, file_name=>"$base$t_name", %base_key)},
			              "tree") .
			      " | " .
			      $cgi->a({-href => href(action=>"history", hash_base=>$hash_base, file_name=>"$base$t_name")},
			              "history") .
			      "</td>\n";
		}
		print "</tr>\n";
	}
	print "</table>\n" .
	      "</div>";
	git_footer_html();
}

sub git_snapshot {

	my ($ctype, $suffix, $command) = gitweb_check_feature('snapshot');
	my $have_snapshot = (defined $ctype && defined $suffix);
	if (!$have_snapshot) {
		die_error('403 Permission denied', "Permission denied");
	}

	if (!defined $hash) {
		$hash = git_get_head_hash($project);
	}

	my $filename = basename($project) . "-$hash.tar.$suffix";

	print $cgi->header(-type => 'application/x-tar',
	                   -content_encoding => $ctype,
	                   -content_disposition => "inline; filename=\"$filename\"",
	                   -status => '200 OK');

	open my $fd, "-|", "$GIT tar-tree $hash \'$project\' | $command" or
		die_error(undef, "Execute git-tar-tree failed.");
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	close $fd;

}

sub git_log {
	my $head = git_get_head_hash($project);
	if (!defined $hash) {
		$hash = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = git_get_references();

	my $limit = sprintf("--max-count=%i", (100 * ($page+1)));
	open my $fd, "-|", $GIT, "rev-list", $limit, $hash
		or die_error(undef, "Open git-rev-list failed");
	my @revlist = map { chomp; $_ } <$fd>;
	close $fd;

	my $paging_nav = format_paging_nav('log', $hash, $head, $page, $#revlist);

	git_header_html();
	git_print_page_nav('log','', $hash,undef,undef, $paging_nav);

	if (!@revlist) {
		my %co = parse_commit($hash);

		git_print_header_div('summary', $project);
		print "<div class=\"page_body\"> Last change $co{'age_string'}.<br/><br/></div>\n";
	}
	for (my $i = ($page * 100); $i <= $#revlist; $i++) {
		my $commit = $revlist[$i];
		my $ref = format_ref_marker($refs, $commit);
		my %co = parse_commit($commit);
		next if !%co;
		my %ad = parse_date($co{'author_epoch'});
		git_print_header_div('commit',
		               "<span class=\"age\">$co{'age_string'}</span>" .
		               esc_html($co{'title'}) . $ref,
		               $commit);
		print "<div class=\"title_text\">\n" .
		      "<div class=\"log_link\">\n" .
		      $cgi->a({-href => href(action=>"commit", hash=>$commit)}, "commit") .
		      " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff") .
		      "<br/>\n" .
		      "</div>\n" .
		      "<i>" . esc_html($co{'author_name'}) .  " [$ad{'rfc2822'}]</i><br/>\n" .
		      "</div>\n";

		print "<div class=\"log_body\">\n";
		git_print_simplified_log($co{'comment'});
		print "</div>\n";
	}
	git_footer_html();
}

sub git_commit {
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	my %ad = parse_date($co{'author_epoch'}, $co{'author_tz'});
	my %cd = parse_date($co{'committer_epoch'}, $co{'committer_tz'});

	my $parent = $co{'parent'};
	if (!defined $parent) {
		$parent = "--root";
	}
	open my $fd, "-|", $GIT, "diff-tree", '-r', '-M', $parent, $hash
		or die_error(undef, "Open git-diff-tree failed");
	my @difftree = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading git-diff-tree failed");

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		$expires = "+1d";
	}
	my $refs = git_get_references();
	my $ref = format_ref_marker($refs, $co{'id'});

	my ($ctype, $suffix, $command) = gitweb_check_feature('snapshot');
	my $have_snapshot = (defined $ctype && defined $suffix);

	my $formats_nav = '';
	if (defined $file_name && defined $co{'parent'}) {
		my $parent = $co{'parent'};
		$formats_nav .=
			$cgi->a({-href => href(action=>"blame", hash_parent=>$parent, file_name=>$file_name)},
			        "blame");
	}
	git_header_html(undef, $expires);
	git_print_page_nav('commit', defined $co{'parent'} ? '' : 'commitdiff',
	                   $hash, $co{'tree'}, $hash,
	                   $formats_nav);

	if (defined $co{'parent'}) {
		git_print_header_div('commitdiff', esc_html($co{'title'}) . $ref, $hash);
	} else {
		git_print_header_div('tree', esc_html($co{'title'}) . $ref, $co{'tree'}, $hash);
	}
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">\n";
	print "<tr><td>author</td><td>" . esc_html($co{'author'}) . "</td></tr>\n".
	      "<tr>" .
	      "<td></td><td> $ad{'rfc2822'}";
	if ($ad{'hour_local'} < 6) {
		printf(" (<span class=\"atnight\">%02d:%02d</span> %s)",
		       $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	} else {
		printf(" (%02d:%02d %s)",
		       $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	}
	print "</td>" .
	      "</tr>\n";
	print "<tr><td>committer</td><td>" . esc_html($co{'committer'}) . "</td></tr>\n";
	print "<tr><td></td><td> $cd{'rfc2822'}" .
	      sprintf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'}) .
	      "</td></tr>\n";
	print "<tr><td>commit</td><td class=\"sha1\">$co{'id'}</td></tr>\n";
	print "<tr>" .
	      "<td>tree</td>" .
	      "<td class=\"sha1\">" .
	      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$hash),
	               class => "list"}, $co{'tree'}) .
	      "</td>" .
	      "<td class=\"link\">" .
	      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$hash)},
	              "tree");
	if ($have_snapshot) {
		print " | " .
		      $cgi->a({-href => href(action=>"snapshot", hash=>$hash)}, "snapshot");
	}
	print "</td>" .
	      "</tr>\n";
	my $parents = $co{'parents'};
	foreach my $par (@$parents) {
		print "<tr>" .
		      "<td>parent</td>" .
		      "<td class=\"sha1\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$par),
		               class => "list"}, $par) .
		      "</td>" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$par)}, "commit") .
		      " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$hash, hash_parent=>$par)}, "commitdiff") .
		      "</td>" .
		      "</tr>\n";
	}
	print "</table>".
	      "</div>\n";

	print "<div class=\"page_body\">\n";
	git_print_log($co{'comment'});
	print "</div>\n";

	git_difftree_body(\@difftree, $parent);

	git_footer_html();
}

sub git_blobdiff {
	mkdir($git_temp, 0700);
	git_header_html();
	if (defined $hash_base && (my %co = parse_commit($hash_base))) {
		my $formats_nav =
			$cgi->a({-href => href(action=>"blobdiff_plain",
			                       hash=>$hash, hash_parent=>$hash_parent)},
			        "plain");
		git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
		git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	} else {
		print <<HTML;
<div class="page_nav"><br/><br/></div>
<div class="title">$hash vs $hash_parent</div>
HTML
	}
	git_print_page_path($file_name, "blob", $hash_base);
	print "<div class=\"page_body\">\n" .
	      "<div class=\"diff_info\">blob:" .
	      $cgi->a({-href => href(action=>"blob", hash=>$hash_parent,
	                             hash_base=>$hash_base, file_name=>($file_parent || $file_name))},
	              $hash_parent) .
	      " -> blob:" .
	      $cgi->a({-href => href(action=>"blob", hash=>$hash,
	                             hash_base=>$hash_base, file_name=>$file_name)},
	              $hash) .
	      "</div>\n";
	git_diff_print($hash_parent, $file_name || $hash_parent, $hash, $file_name || $hash);
	print "</div>"; # page_body
	git_footer_html();
}

sub git_blobdiff_plain {
	mkdir($git_temp, 0700);
	print $cgi->header(-type => "text/plain", -charset => 'utf-8');
	git_diff_print($hash_parent, $file_name || $hash_parent, $hash, $file_name || $hash, "plain");
}

sub git_commitdiff {
	mkdir($git_temp, 0700);
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	if (!defined $hash_parent) {
		$hash_parent = $co{'parent'} || '--root';
	}
	open my $fd, "-|", $GIT, "diff-tree", '-r', $hash_parent, $hash
		or die_error(undef, "Open git-diff-tree failed");
	my @difftree = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading git-diff-tree failed");

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		$expires = "+1d";
	}
	my $refs = git_get_references();
	my $ref = format_ref_marker($refs, $co{'id'});
	my $formats_nav =
		$cgi->a({-href => href(action=>"commitdiff_plain", hash=>$hash, hash_parent=>$hash_parent)},
		        "plain");
	git_header_html(undef, $expires);
	git_print_page_nav('commitdiff','', $hash,$co{'tree'},$hash, $formats_nav);
	git_print_header_div('commit', esc_html($co{'title'}) . $ref, $hash);
	print "<div class=\"page_body\">\n";
	git_print_simplified_log($co{'comment'}, 1); # skip title
	print "<br/>\n";
	foreach my $line (@difftree) {
		# ':100644 100644 03b218260e99b78c6df0ed378e59ed9205ccc96d 3b93d5e7cc7f7dd4ebed13a5cc1a4ad976fc94d8 M      ls-files.c'
		# ':100644 100644 7f9281985086971d3877aca27704f2aaf9c448ce bc190ebc71bbd923f2b728e505408f5e54bd073a M      rev-tree.c'
		if ($line !~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/) {
			next;
		}
		my $from_mode = $1;
		my $to_mode = $2;
		my $from_id = $3;
		my $to_id = $4;
		my $status = $5;
		my $file = validate_input(unquote($6));
		if ($status eq "A") {
			print "<div class=\"diff_info\">" . file_type($to_mode) . ":" .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$hash,
			                             hash=>$to_id, file_name=>$file)},
			              $to_id) . "(new)" .
			      "</div>\n";
			git_diff_print(undef, "/dev/null", $to_id, "b/$file");
		} elsif ($status eq "D") {
			print "<div class=\"diff_info\">" . file_type($from_mode) . ":" .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$hash_parent,
			                             hash=>$from_id, file_name=>$file)},
			              $from_id) . "(deleted)" .
			      "</div>\n";
			git_diff_print($from_id, "a/$file", undef, "/dev/null");
		} elsif ($status eq "M") {
			if ($from_id ne $to_id) {
				print "<div class=\"diff_info\">" .
				      file_type($from_mode) . ":" .
				      $cgi->a({-href => href(action=>"blob", hash_base=>$hash_parent,
				                             hash=>$from_id, file_name=>$file)},
				              $from_id) .
				      " -> " .
				      file_type($to_mode) . ":" .
				      $cgi->a({-href => href(action=>"blob", hash_base=>$hash,
				                             hash=>$to_id, file_name=>$file)},
				              $to_id);
				print "</div>\n";
				git_diff_print($from_id, "a/$file",  $to_id, "b/$file");
			}
		}
	}
	print "<br/>\n" .
	      "</div>";
	git_footer_html();
}

sub git_commitdiff_plain {
	mkdir($git_temp, 0700);
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	if (!defined $hash_parent) {
		$hash_parent = $co{'parent'} || '--root';
	}
	open my $fd, "-|", $GIT, "diff-tree", '-r', $hash_parent, $hash
		or die_error(undef, "Open git-diff-tree failed");
	my @difftree = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading diff-tree failed");

	# try to figure out the next tag after this commit
	my $tagname;
	my $refs = git_get_references("tags");
	open $fd, "-|", $GIT, "rev-list", "HEAD";
	my @commits = map { chomp; $_ } <$fd>;
	close $fd;
	foreach my $commit (@commits) {
		if (defined $refs->{$commit}) {
			$tagname = $refs->{$commit}
		}
		if ($commit eq $hash) {
			last;
		}
	}

	print $cgi->header(-type => "text/plain",
	                   -charset => 'utf-8',
	                   -content_disposition => "inline; filename=\"git-$hash.patch\"");
	my %ad = parse_date($co{'author_epoch'}, $co{'author_tz'});
	my $comment = $co{'comment'};
	print <<TEXT;
From: $co{'author'}
Date: $ad{'rfc2822'} ($ad{'tz_local'})
Subject: $co{'title'}
TEXT
	if (defined $tagname) {
		print "X-Git-Tag: $tagname\n";
	}
	print "X-Git-Url: $my_url?p=$project;a=commitdiff;h=$hash\n" .
	      "\n";

	foreach my $line (@$comment) {;
		print "$line\n";
	}
	print "---\n\n";

	foreach my $line (@difftree) {
		if ($line !~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/) {
			next;
		}
		my $from_id = $3;
		my $to_id = $4;
		my $status = $5;
		my $file = $6;
		if ($status eq "A") {
			git_diff_print(undef, "/dev/null", $to_id, "b/$file", "plain");
		} elsif ($status eq "D") {
			git_diff_print($from_id, "a/$file", undef, "/dev/null", "plain");
		} elsif ($status eq "M") {
			git_diff_print($from_id, "a/$file",  $to_id, "b/$file", "plain");
		}
	}
}

sub git_history {
	if (!defined $hash_base) {
		$hash_base = git_get_head_hash($project);
	}
	my $ftype;
	my %co = parse_commit($hash_base);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	my $refs = git_get_references();
	git_header_html();
	git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	if (!defined $hash && defined $file_name) {
		$hash = git_get_hash_by_path($hash_base, $file_name);
	}
	if (defined $hash) {
		$ftype = git_get_type($hash);
	}
	git_print_page_path($file_name, $ftype, $hash_base);

	open my $fd, "-|",
		$GIT, "rev-list", "--full-history", $hash_base, "--", $file_name;
	git_history_body($fd, $refs, $hash_base, $ftype);

	close $fd;
	git_footer_html();
}

sub git_search {
	if (!defined $searchtext) {
		die_error(undef, "Text field empty");
	}
	if (!defined $hash) {
		$hash = git_get_head_hash($project);
	}
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	# pickaxe may take all resources of your box and run for several minutes
	# with every query - so decide by yourself how public you make this feature :)
	my $commit_search = 1;
	my $author_search = 0;
	my $committer_search = 0;
	my $pickaxe_search = 0;
	if ($searchtext =~ s/^author\\://i) {
		$author_search = 1;
	} elsif ($searchtext =~ s/^committer\\://i) {
		$committer_search = 1;
	} elsif ($searchtext =~ s/^pickaxe\\://i) {
		$commit_search = 0;
		$pickaxe_search = 1;
	}
	git_header_html();
	git_print_page_nav('','', $hash,$co{'tree'},$hash);
	git_print_header_div('commit', esc_html($co{'title'}), $hash);

	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	if ($commit_search) {
		$/ = "\0";
		open my $fd, "-|", $GIT, "rev-list", "--header", "--parents", $hash or next;
		while (my $commit_text = <$fd>) {
			if (!grep m/$searchtext/i, $commit_text) {
				next;
			}
			if ($author_search && !grep m/\nauthor .*$searchtext/i, $commit_text) {
				next;
			}
			if ($committer_search && !grep m/\ncommitter .*$searchtext/i, $commit_text) {
				next;
			}
			my @commit_lines = split "\n", $commit_text;
			my %co = parse_commit(undef, \@commit_lines);
			if (!%co) {
				next;
			}
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
			      "<td><i>" . esc_html(chop_str($co{'author_name'}, 15, 5)) . "</i></td>\n" .
			      "<td>" .
			      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'}), -class => "list subject"},
			               esc_html(chop_str($co{'title'}, 50)) . "<br/>");
			my $comment = $co{'comment'};
			foreach my $line (@$comment) {
				if ($line =~ m/^(.*)($searchtext)(.*)$/i) {
					my $lead = esc_html($1) || "";
					$lead = chop_str($lead, 30, 10);
					my $match = esc_html($2) || "";
					my $trail = esc_html($3) || "";
					$trail = chop_str($trail, 30, 10);
					my $text = "$lead<span class=\"match\">$match</span>$trail";
					print chop_str($text, 80, 5) . "<br/>\n";
				}
			}
			print "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})}, "commit") .
			      " | " .
			      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$co{'id'})}, "tree");
			print "</td>\n" .
			      "</tr>\n";
		}
		close $fd;
	}

	if ($pickaxe_search) {
		$/ = "\n";
		open my $fd, "-|", "$GIT rev-list $hash | $GIT diff-tree -r --stdin -S\'$searchtext\'";
		undef %co;
		my @files;
		while (my $line = <$fd>) {
			if (%co && $line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/) {
				my %set;
				$set{'file'} = $6;
				$set{'from_id'} = $3;
				$set{'to_id'} = $4;
				$set{'id'} = $set{'to_id'};
				if ($set{'id'} =~ m/0{40}/) {
					$set{'id'} = $set{'from_id'};
				}
				if ($set{'id'} =~ m/0{40}/) {
					next;
				}
				push @files, \%set;
			} elsif ($line =~ m/^([0-9a-fA-F]{40})$/){
				if (%co) {
					if ($alternate) {
						print "<tr class=\"dark\">\n";
					} else {
						print "<tr class=\"light\">\n";
					}
					$alternate ^= 1;
					print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
					      "<td><i>" . esc_html(chop_str($co{'author_name'}, 15, 5)) . "</i></td>\n" .
					      "<td>" .
					      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'}),
					              -class => "list subject"},
					              esc_html(chop_str($co{'title'}, 50)) . "<br/>");
					while (my $setref = shift @files) {
						my %set = %$setref;
						print $cgi->a({-href => href(action=>"blob", hash_base=>$co{'id'},
						                             hash=>$set{'id'}, file_name=>$set{'file'}),
						              -class => "list"},
						              "<span class=\"match\">" . esc_html($set{'file'}) . "</span>") .
						      "<br/>\n";
					}
					print "</td>\n" .
					      "<td class=\"link\">" .
					      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})}, "commit") .
					      " | " .
					      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$co{'id'})}, "tree");
					print "</td>\n" .
					      "</tr>\n";
				}
				%co = parse_commit($1);
			}
		}
		close $fd;
	}
	print "</table>\n";
	git_footer_html();
}

sub git_shortlog {
	my $head = git_get_head_hash($project);
	if (!defined $hash) {
		$hash = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = git_get_references();

	my $limit = sprintf("--max-count=%i", (100 * ($page+1)));
	open my $fd, "-|", $GIT, "rev-list", $limit, $hash
		or die_error(undef, "Open git-rev-list failed");
	my @revlist = map { chomp; $_ } <$fd>;
	close $fd;

	my $paging_nav = format_paging_nav('shortlog', $hash, $head, $page, $#revlist);
	my $next_link = '';
	if ($#revlist >= (100 * ($page+1)-1)) {
		$next_link =
			$cgi->a({-href => href(action=>"shortlog", hash=>$hash, page=>$page+1),
			         -title => "Alt-n"}, "next");
	}


	git_header_html();
	git_print_page_nav('shortlog','', $hash,$hash,$hash, $paging_nav);
	git_print_header_div('summary', $project);

	git_shortlog_body(\@revlist, ($page * 100), $#revlist, $refs, $next_link);

	git_footer_html();
}

## ......................................................................
## feeds (RSS, OPML)

sub git_rss {
	# http://www.notestips.com/80256B3A007F2692/1/NAMO5P9UPQ
	open my $fd, "-|", $GIT, "rev-list", "--max-count=150", git_get_head_hash($project)
		or die_error(undef, "Open git-rev-list failed");
	my @revlist = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading git-rev-list failed");
	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print <<XML;
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/">
<channel>
<title>$project $my_uri $my_url</title>
<link>${\esc_html("$my_url?p=$project;a=summary")}</link>
<description>$project log</description>
<language>en</language>
XML

	for (my $i = 0; $i <= $#revlist; $i++) {
		my $commit = $revlist[$i];
		my %co = parse_commit($commit);
		# we read 150, we always show 30 and the ones more recent than 48 hours
		if (($i >= 20) && ((time - $co{'committer_epoch'}) > 48*60*60)) {
			last;
		}
		my %cd = parse_date($co{'committer_epoch'});
		open $fd, "-|", $GIT, "diff-tree", '-r', $co{'parent'}, $co{'id'} or next;
		my @difftree = map { chomp; $_ } <$fd>;
		close $fd or next;
		print "<item>\n" .
		      "<title>" .
		      sprintf("%d %s %02d:%02d", $cd{'mday'}, $cd{'month'}, $cd{'hour'}, $cd{'minute'}) . " - " . esc_html($co{'title'}) .
		      "</title>\n" .
		      "<author>" . esc_html($co{'author'}) . "</author>\n" .
		      "<pubDate>$cd{'rfc2822'}</pubDate>\n" .
		      "<guid isPermaLink=\"true\">" . esc_html("$my_url?p=$project;a=commit;h=$commit") . "</guid>\n" .
		      "<link>" . esc_html("$my_url?p=$project;a=commit;h=$commit") . "</link>\n" .
		      "<description>" . esc_html($co{'title'}) . "</description>\n" .
		      "<content:encoded>" .
		      "<![CDATA[\n";
		my $comment = $co{'comment'};
		foreach my $line (@$comment) {
			$line = decode("utf8", $line, Encode::FB_DEFAULT);
			print "$line<br/>\n";
		}
		print "<br/>\n";
		foreach my $line (@difftree) {
			if (!($line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)([0-9]{0,3})\t(.*)$/)) {
				next;
			}
			my $file = validate_input(unquote($7));
			$file = decode("utf8", $file, Encode::FB_DEFAULT);
			print "$file<br/>\n";
		}
		print "]]>\n" .
		      "</content:encoded>\n" .
		      "</item>\n";
	}
	print "</channel></rss>";
}

sub git_opml {
	my @list = git_get_projects_list();

	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print <<XML;
<?xml version="1.0" encoding="utf-8"?>
<opml version="1.0">
<head>
  <title>$site_name Git OPML Export</title>
</head>
<body>
<outline text="git RSS feeds">
XML

	foreach my $pr (@list) {
		my %proj = %$pr;
		my $head = git_get_head_hash($proj{'path'});
		if (!defined $head) {
			next;
		}
		$ENV{'GIT_DIR'} = "$projectroot/$proj{'path'}";
		my %co = parse_commit($head);
		if (!%co) {
			next;
		}

		my $path = esc_html(chop_str($proj{'path'}, 25, 5));
		my $rss  = "$my_url?p=$proj{'path'};a=rss";
		my $html = "$my_url?p=$proj{'path'};a=summary";
		print "<outline type=\"rss\" text=\"$path\" title=\"$path\" xmlUrl=\"$rss\" htmlUrl=\"$html\"/>\n";
	}
	print <<XML;
</outline>
</body>
</opml>
XML
}
