#!/usr/bin/perl

# Copyright (C) 2013
#     Benoit Person <benoit.person@ensimag.imag.fr>
#     Celestin Matte <celestin.matte@ensimag.imag.fr>
# License: GPL v2 or later

# Set of tools for git repo with a mediawiki remote.
# Documentation & bugtracker: https://github.com/moy/Git-Mediawiki/

use strict;
use warnings;

use Getopt::Long;
use URI::URL qw(url);
use LWP::UserAgent;
use HTML::TreeBuilder;

use Git;
use MediaWiki::API;
use Git::Mediawiki qw(clean_filename connect_maybe
					EMPTY HTTP_CODE_PAGE_NOT_FOUND);

# By default, use UTF-8 to communicate with Git and the user
binmode STDERR, ':encoding(UTF-8)';
binmode STDOUT, ':encoding(UTF-8)';

# Global parameters
my $verbose = 0;
sub v_print {
	if ($verbose) {
		return print {*STDERR} @_;
	}
	return;
}

# Preview parameters
my $file_name = EMPTY;
my $remote_name = EMPTY;
my $preview_file_name = EMPTY;
my $autoload = 0;
sub file {
	$file_name = shift;
	return $file_name;
}

my %commands = (
	'help' =>
		[\&help, {}, \&help],
	'preview' =>
		[\&preview, {
			'<>' => \&file,
			'output|o=s' => \$preview_file_name,
			'remote|r=s' => \$remote_name,
			'autoload|a' => \$autoload
		}, \&preview_help]
);

# Search for sub-command
my $cmd = $commands{'help'};
for (0..@ARGV-1) {
	if (defined $commands{$ARGV[$_]}) {
		$cmd = $commands{$ARGV[$_]};
		splice @ARGV, $_, 1;
		last;
	}
};
GetOptions( %{$cmd->[1]},
	'help|h' => \&{$cmd->[2]},
	'verbose|v'  => \$verbose);

# Launch command
&{$cmd->[0]};

############################# Preview Functions ################################

sub preview_help {
	print {*STDOUT} <<'END';
USAGE: git mw preview [--remote|-r <remote name>] [--autoload|-a]
                      [--output|-o <output filename>] [--verbose|-v]
                      <blob> | <filename>

DESCRIPTION:
Preview is an utiliy to preview local content of a mediawiki repo as if it was
pushed on the remote.

For that, preview searches for the remote name of the current branch's
upstream if --remote is not set. If that remote is not found or if it
is not a mediawiki, it lists all mediawiki remotes configured and asks
you to replay your command with the --remote option set properly.

Then, it searches for a file named 'filename'. If it's not found in
the current dir, it will assume it's a blob.

The content retrieved in the file (or in the blob) will then be parsed
by the remote mediawiki and combined with a template retrieved from
the mediawiki.

Finally, preview will save the HTML result in a file. and autoload it
in your default web browser if the option --autoload is present.

OPTIONS:
    -r <remote name>, --remote <remote name>
        If the remote is a mediawiki, the template and the parse engine
        used for the preview will be those of that remote.
        If not, a list of valid remotes will be shown.

    -a, --autoload
        Try to load the HTML output in a new tab (or new window) of your
        default web browser.

    -o <output filename>, --output <output filename>
        Change the HTML output filename. Default filename is based on the
        input filename with its extension replaced by '.html'.

    -v, --verbose
        Show more information on what's going on under the hood.
END
	exit;
}

sub preview {
	my $wiki;
	my ($remote_url, $wiki_page_name);
	my ($new_content, $template);
	my $file_content;

	if ($file_name eq EMPTY) {
		die "Missing file argument, see `git mw help`\n";
	}

	v_print("### Selecting remote\n");
	if ($remote_name eq EMPTY) {
		$remote_name = find_upstream_remote_name();
		if ($remote_name) {
			$remote_url = mediawiki_remote_url_maybe($remote_name);
		}

		if (! $remote_url) {
			my @valid_remotes = find_mediawiki_remotes();

			if ($#valid_remotes == 0) {
				print {*STDERR} "No mediawiki remote in this repo. \n";
				exit 1;
			} else {
				my $remotes_list = join("\n\t", @valid_remotes);
				print {*STDERR} <<"MESSAGE";
There are multiple mediawiki remotes, which of:
	${remotes_list}
do you want ? Use the -r option to specify the remote.
MESSAGE
			}

			exit 1;
		}
	} else {
		if (!is_valid_remote($remote_name)) {
			die "${remote_name} is not a remote\n";
		}

		$remote_url = mediawiki_remote_url_maybe($remote_name);
		if (! $remote_url) {
			die "${remote_name} is not a mediawiki remote\n";
		}
	}
	v_print("selected remote:\n\tname: ${remote_name}\n\turl: ${remote_url}\n");

	$wiki = connect_maybe($wiki, $remote_name, $remote_url);

	# Read file content
	if (! -e $file_name) {
		$file_content = git_cmd_try {
			Git::command('cat-file', 'blob', $file_name); }
			"%s failed w/ code %d";

		if ($file_name =~ /(.+):(.+)/) {
			$file_name = $2;
		}
	} else {
		open my $read_fh, "<", $file_name
			or die "could not open ${file_name}: $!\n";
		$file_content = do { local $/ = undef; <$read_fh> };
		close $read_fh
			or die "unable to close: $!\n";
	}

	v_print("### Retrieving template\n");
	($wiki_page_name = clean_filename($file_name)) =~ s/\.[^.]+$//;
	$template = get_template($remote_url, $wiki_page_name);

	v_print("### Parsing local content\n");
	$new_content = $wiki->api({
		action => 'parse',
		text => $file_content,
		title => $wiki_page_name
	}, {
		skip_encoding => 1
	}) or die "No response from remote mediawiki\n";
	$new_content = $new_content->{'parse'}->{'text'}->{'*'};

	v_print("### Merging contents\n");
	if ($preview_file_name eq EMPTY) {
		($preview_file_name = $file_name) =~ s/\.[^.]+$/.html/;
	}
	open(my $save_fh, '>:encoding(UTF-8)', $preview_file_name)
		or die "Could not open: $!\n";
	print {$save_fh} merge_contents($template, $new_content, $remote_url);
	close($save_fh)
		or die "Could not close: $!\n";

	v_print("### Results\n");
	if ($autoload) {
		v_print("Launching browser w/ file: ${preview_file_name}");
		system('git', 'web--browse', $preview_file_name);
	} else {
		print {*STDERR} "Preview file saved as: ${preview_file_name}\n";
	}

	exit;
}

# uses global scope variable: $remote_name
sub merge_contents {
	my $template = shift;
	my $content = shift;
	my $remote_url = shift;
	my ($content_tree, $html_tree, $mw_content_text);
	my $template_content_id = 'bodyContent';

	$html_tree = HTML::TreeBuilder->new;
	$html_tree->parse($template);

	$content_tree = HTML::TreeBuilder->new;
	$content_tree->parse($content);

	$template_content_id = Git::config("remote.${remote_name}.mwIDcontent")
		|| $template_content_id;
	v_print("Using '${template_content_id}' as the content ID\n");

	$mw_content_text = $html_tree->look_down('id', $template_content_id);
	if (!defined $mw_content_text) {
		print {*STDERR} <<"CONFIG";
Could not combine the new content with the template. You might want to
configure `mediawiki.IDContent` in your config:
	git config --add remote.${remote_name}.mwIDcontent <id>
and re-run the command afterward.
CONFIG
		exit 1;
	}
	$mw_content_text->delete_content();
	$mw_content_text->push_content($content_tree);

	make_links_absolute($html_tree, $remote_url);

	return $html_tree->as_HTML;
}

sub make_links_absolute {
	my $html_tree = shift;
	my $remote_url = shift;
	for (@{ $html_tree->extract_links() }) {
		my ($link, $element, $attr) = @{ $_ };
		my $url = url($link)->canonical;
		if ($url !~ /#/) {
			$element->attr($attr, URI->new_abs($url, $remote_url));
		}
	}
	return $html_tree;
}

sub is_valid_remote {
	my $remote = shift;
	my @remotes = git_cmd_try {
		Git::command('remote') }
		"%s failed w/ code %d";
	my $found_remote = 0;
	foreach my $remote (@remotes) {
		if ($remote eq $remote) {
			$found_remote = 1;
			last;
		}
	}
	return $found_remote;
}

sub find_mediawiki_remotes {
	my @remotes = git_cmd_try {
		Git::command('remote'); }
		"%s failed w/ code %d";
	my $remote_url;
	my @valid_remotes = ();
	foreach my $remote (@remotes) {
		$remote_url = mediawiki_remote_url_maybe($remote);
		if ($remote_url) {
			push(@valid_remotes, $remote);
		}
	}
	return @valid_remotes;
}

sub find_upstream_remote_name {
	my $current_branch = git_cmd_try {
		Git::command_oneline('symbolic-ref', '--short', 'HEAD') }
		"%s failed w/ code %d";
	return Git::config("branch.${current_branch}.remote");
}

sub mediawiki_remote_url_maybe {
	my $remote = shift;

	# Find remote url
	my $remote_url = Git::config("remote.${remote}.url");
	if ($remote_url =~ s/mediawiki::(.*)/$1/) {
		return url($remote_url)->canonical;
	}

	return;
}

sub get_template {
	my $url = shift;
	my $page_name = shift;
	my ($req, $res, $code, $url_after);

	$req = LWP::UserAgent->new;
	if ($verbose) {
		$req->show_progress(1);
	}

	$res = $req->get("${url}/index.php?title=${page_name}");
	if (!$res->is_success) {
		$code = $res->code;
		$url_after = $res->request()->uri(); # resolve all redirections
		if ($code == HTTP_CODE_PAGE_NOT_FOUND) {
			if ($verbose) {
				print {*STDERR} <<"WARNING";
Warning: Failed to retrieve '$page_name'. Create it on the mediawiki if you want
all the links to work properly.
Trying to use the mediawiki homepage as a fallback template ...
WARNING
			}

			# LWP automatically redirects GET request
			$res = $req->get("${url}/index.php");
			if (!$res->is_success) {
				$url_after = $res->request()->uri(); # resolve all redirections
				die "Failed to get homepage @ ${url_after} w/ code ${code}\n";
			}
		} else {
			die "Failed to get '${page_name}' @ ${url_after} w/ code ${code}\n";
		}
	}

	return $res->decoded_content;
}

############################## Help Functions ##################################

sub help {
	print {*STDOUT} <<'END';
usage: git mw <command> <args>

git mw commands are:
    help        Display help information about git mw
    preview     Parse and render local file into HTML
END
	exit;
}
